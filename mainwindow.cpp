// MIT License

// Copyright (c) 2020 Gabriele Salvato

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <syslog.h>
#include <signal.h>

#include "mainwindow.h"
#include "pigpiod_if2.h"


size_t
payloadSource(void *ptr, size_t size, size_t nmemb, void *userp) {
    struct upload_status* upload_ctx = static_cast<struct upload_status*>(userp);
    if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1))
        return 0;
    if(upload_ctx->lines_read >= upload_ctx->pPayload->count())
        return 0;
    QString sLine = QString("%1\r\n").arg(upload_ctx->pPayload->at(upload_ctx->lines_read));
    size_t len = qMin(size_t(sLine.length()), size*nmemb);
    memcpy(ptr, sLine.toLatin1().constData(), len);
    upload_ctx->lines_read++;
    return len;
}


MainWindow::MainWindow(int &argc, char **argv)
    : QCoreApplication(argc, argv)
    , pRecipients(nullptr)
    , pLogFile(nullptr)
    , pTemperatureSensor(nullptr)
{
    gpioHostHandle = -1;
    gpioSensorPin  = 23; // BCM 23: pin 16 in the 40 pins GPIO connector
    // DS18B20 connected to BCM  4: pin 7  in the 40 pins GPIO connector
    res            = CURLE_OK;
    bOnAlarm          = false;
    bAlarmMessageSent = false;

    updateInterval = 60 * 1000;      // 60 sec (in ms)
    resendInterval = 30 * 60 * 1000; // 30 min (in ms)

    // Build the log file pathname
    sLogFileName = QString("smartAlertLog.txt");
    QString sLogDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if(!sLogDir.endsWith(QString("/"))) sLogDir+= QString("/");
    sLogFileName = sLogDir+sLogFileName;

    if(!logRotate(sLogFileName)) { // If unable to open Log File then Log to syslog
        QString sAppName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        // See the syslog(3) man page
        openlog(sAppName.toLatin1().constData(), LOG_PID, LOG_USER);
    }

    restoreSettings();
}


int
MainWindow::exec() {
    // Initialize the GPIO handler
    char host[] = "localhost";
    char port[] = "8888";
    gpioHostHandle = pigpio_start(&host[0], &port[0]);
    if(gpioHostHandle >= 0) {
        if(set_mode(gpioHostHandle, gpioSensorPin, PI_INPUT) < 0) {
            logMessage(QString("Unable to initialize GPIO%1 as Output")
                       .arg(gpioSensorPin));
            gpioHostHandle = -1;
        }
        else if(set_pull_up_down(gpioHostHandle, gpioSensorPin, PI_PUD_UP) < 0) {
            logMessage(QString("Unable to set GPIO%1 Pull-Up")
                       .arg(gpioSensorPin));
            gpioHostHandle = -1;
        }
    }
    else {
        logMessage(QString("Unable to initialize the Pi GPIO."));
    }

    // Check for the presence of a Temperature Sensor
    pTemperatureSensor = new DS1820();
    if(!pTemperatureSensor->isConnected()) {
        logMessage(QString("No Temperature Sensor Found"));
        delete pTemperatureSensor;
        pTemperatureSensor = nullptr;
    }
    else {
        pTemperatureSensor->setLimits(0.0, dMaxTemperature);
        logMessage(QString("Temperature: %1, %2")
                   .arg(double(startTime.secsTo(QDateTime::currentDateTime())/3600.0))
                   .arg(pTemperatureSensor->readTemperature()));
        connect(&updateTimer, SIGNAL(timeout()),
                this, SLOT(onTimeToUpdateStatus()));
    }
    connect(&resendTimer, SIGNAL(timeout()),
        this, SLOT(onTimeToResendAlarm()));

    startTime = QDateTime::currentDateTime();
    rotateLogTime = startTime;

    // Check the System Status every minute
    updateTimer.start(updateInterval);

#ifndef QT_DEBUG
    logMessage("Smart Alert System Started");
    if(sendMail("Smart Alert System [INFO]",
                "Smart Alert System Has Been Restarted"))
        logMessage("Smart Alert System [INFO]: Message Sent");
    else
        logMessage("Smart Alert System [INFO]: Unable to Send the Message");
#endif
    return QCoreApplication::exec();
}


MainWindow::~MainWindow() {
    logMessage("Switching Off the Program");
    updateTimer.stop();
    resendTimer.stop();

#ifndef QT_DEBUG
    if(sendMail("Smart Alert System [INFO]",
                "Smart Alert Has Been Switched Off"))
        logMessage("Message Sent");
    else
        logMessage("Unable to Send the Switched Off Message");
#endif

    if(gpioHostHandle >= 0)
        pigpio_stop(gpioHostHandle);
    if(pLogFile) {
        if(pLogFile->isOpen()) {
            pLogFile->flush();
        }
        pLogFile->deleteLater();
        pLogFile = nullptr;
    }
    closelog();
}


bool
MainWindow::logRotate(QString sLogFileName) {
    // Rotate 5 previous logs, removing the oldest, to avoid data loss
    QFileInfo checkFile(sLogFileName);
    if(checkFile.exists() && checkFile.isFile()) {
#ifdef QT_DEBUG
        qDebug() << "Rotating Log File";
#endif
        QDir renamed;
        renamed.remove(sLogFileName+QString("_4.txt"));
        for(int i=4; i>0; i--) {
            renamed.rename(sLogFileName+QString("_%1.txt").arg(i-1),
                           sLogFileName+QString("_%1.txt").arg(i));
        }
        if(pLogFile) {
            if(pLogFile->isOpen())
                pLogFile->close();
            delete pLogFile;
            pLogFile = nullptr;
        }
        renamed.rename(sLogFileName,
                       sLogFileName+QString("_0.txt"));
    }
    // Open the new log file
    pLogFile = new QFile(sLogFileName);
    if (!pLogFile->open(QIODevice::WriteOnly)) {
        logMessage(QString("Unable to open file %1: %2.")
                   .arg(sLogFileName).arg(pLogFile->errorString()));
        delete pLogFile;
        pLogFile = Q_NULLPTR;
        return false;
    }
    return true;
}


void
MainWindow::logMessage(QString sMessage) {
    QString sDebugMessage = currentTime.currentDateTime().toString("MM dd yyyy hh:mm:ss") +
            QString(": ") +
            sMessage;
#ifdef QT_DEBUG
    qDebug() << sDebugMessage;
#endif
    if(pLogFile) {
        if(pLogFile->isOpen()) {
            pLogFile->write(sDebugMessage.toLatin1().constData());
            pLogFile->write("\n");
            pLogFile->flush();
        }
        else
            syslog(LOG_ALERT|LOG_USER, "%s", sMessage.toLatin1().constData());
    }
    else
        syslog(LOG_ALERT|LOG_USER, "%s", sMessage.toLatin1().constData());
}


void
MainWindow::restoreSettings() {
    QSettings* pSettings = new QSettings();

    sUsername    = pSettings->value("Username:",        "").toString();
    sPassword    = pSettings->value("Password:",        "").toString();
    sMailServer  = pSettings->value("Mail Server:",     "").toString();
    sTo          = pSettings->value("To:",              "").toString();
    sCc          = pSettings->value("Cc:",              "").toString();
    sMessageText = pSettings->value("Message to Send:", "").toString();

    dMaxTemperature = pSettings->value("Alarm Threshold", "28.0").toDouble();

    logMessage("Settings Changed. New Values Are:");
    logMessage(QString("Username: %1").arg(sUsername));
    logMessage(QString("Mail Server: %1").arg(sMailServer));
    logMessage(QString("To: %1").arg(sTo));
    if(sCc != QString())
        logMessage(QString("Cc: %1").arg(sCc));
    logMessage(QString("Threshold: %1").arg(dMaxTemperature));

    delete pSettings;
}


void
MainWindow::buildPayload(QString sSubject, QString sMessage) {
    payloadText.clear();

    payloadText.append(QString("Date: %1")
                       .arg(currentTime.currentDateTime().toString(Qt::RFC2822Date)));
    payloadText.append(QString("To: %1")
                       .arg(sTo));
    payloadText.append(QString("From: %1@%2")
                       .arg(sUsername)
                       .arg(sMailServer));
    if(sCc != QString()) {
        payloadText.append(QString("Cc: <%1>")
                           .arg(sCc));
    }

    QString sMessageId = QString("Message-ID: <%1@smart_alert_system>")
            .arg(currentTime.currentDateTime().toString().replace(QChar(' '), QChar('#')));
    payloadText.append(sMessageId);
    payloadText.append(QString("Subject: %1")
                       .arg(sSubject));
    // empty line to divide headers from body, see RFC5322
    payloadText.append(QString());
    // Body
    payloadText.append(currentTime.currentDateTime().toString());
    QStringList sMessageBody = sMessage.split("\n");
    payloadText.append(sMessageBody);
}


bool
MainWindow::sendMail(QString sSubject, QString sMessage) {
    buildPayload(sSubject, sMessage);
    upload_ctx.lines_read = 0;
    upload_ctx.pPayload = &payloadText;

    pCurl = curl_easy_init();
    if(pCurl) {
        QString mailserverURL = QString("smtps://%1")
                                .arg(sMailServer);

        curl_easy_setopt(pCurl, CURLOPT_URL, mailserverURL.toLatin1().constData());

        curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 1);
        curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, 1L);

        QString sMailFrom = QString("<%1@%2>")
                            .arg(sUsername)
                            .arg(sMailServer);

        curl_easy_setopt(pCurl, CURLOPT_MAIL_FROM, sMailFrom.toLatin1().constData());
        curl_easy_setopt(pCurl, CURLOPT_USERNAME,  sUsername.toLatin1().constData());
        curl_easy_setopt(pCurl, CURLOPT_PASSWORD,  sPassword.toLatin1().constData());

        pRecipients = curl_slist_append(pRecipients, sTo.toLatin1().constData());
        if(sCc != QString()) {
            pRecipients = curl_slist_append(pRecipients, (QString("<%1>").arg(sCc)).toLatin1().constData());
        }

        curl_easy_setopt(pCurl, CURLOPT_MAIL_RCPT, pRecipients);

        curl_easy_setopt(pCurl, CURLOPT_READFUNCTION, ::payloadSource);
        curl_easy_setopt(pCurl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(pCurl, CURLOPT_UPLOAD, 1L);

#ifdef QT_DEBUG
        curl_easy_setopt(pCurl, CURLOPT_VERBOSE, 1L);
#endif

        // Send the message
        res = curl_easy_perform(pCurl);
        if(res != CURLE_OK)
            logMessage(QString("curl_easy_perform() failed: %1")
                       .arg(curl_easy_strerror(res)));

        // Free the list of recipients
        curl_slist_free_all(pRecipients);
        pRecipients = nullptr;
        curl_easy_cleanup(pCurl);
    }
    return (res==CURLE_OK);
}


void
MainWindow::onTimeToUpdateStatus() {
    bOnAlarm = false;
    // Check if it's time (every 7 days) to rotate log:
    if(rotateLogTime.daysTo(QDateTime::currentDateTime()) > 7) {
        logRotate(sLogFileName);
        rotateLogTime = QDateTime::currentDateTime();
    }
    if(pTemperatureSensor) {
        logMessage(QString("Temperature: %1, %2")
                   .arg(double(startTime.secsTo(QDateTime::currentDateTime())/3600.0))
                   .arg(pTemperatureSensor->readTemperature()));
        bOnAlarm |= pTemperatureSensor->isOnAlarm();
    }
    if(bOnAlarm  && !bAlarmMessageSent) {
        logMessage("TEMPERATURE ALARM !");
        if(sendMail("Smart Alert System [ALARM!]",
                    sMessageText))
        {
            bAlarmMessageSent = true;
            logMessage("Smart Alert System [ALARM!]: Message Sent");
            // Start a timer to retransmit the alarm message every 30 minutes
            resendTimer.start(resendInterval);
        }
        else {
            logMessage("PS Temperature Alarm System [ALARM!]: Unable to Send the Message");
        }
    }
}


void
MainWindow::onTimeToResendAlarm() {
    if(!bOnAlarm) {
        logMessage("Temperature Alarm Ceased");
        if(sendMail("Smart Alert System [INFO!]",
                    "Temperature Alarm Ceased"))
            logMessage("Smart Alert System [INFO!]: Message Sent");
        else
            logMessage("Smart Alert System [INFO!]: Unable to Send the Message");
        resendTimer.stop();
        bAlarmMessageSent = false;
    }
    else { // Still on Alarm !
        logMessage("TEMPERATURE ALARM STILL ON!");
        if(sendMail("Smart Alert System [ALARM!]",
                    sMessageText))
            logMessage("Smart Alert System [ALARM!]: Message Sent");
        else
            logMessage("Smart Alert System [ALARM!]: Unable to Send the Message");
    }
}
