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

#pragma once

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <QDateTime>
#include <curl/curl.h>
#include <pigpiod_if2.h> // The header for using GPIO pins on Raspberry
#include "ds1820.h"

QT_FORWARD_DECLARE_CLASS(QFile)


struct
upload_status {
    QStringList* pPayload;
    int          lines_read;
};


class MainWindow : public QCoreApplication
{
    Q_OBJECT

public:
    MainWindow(int &argc, char **argv);
    ~MainWindow();
    int exec();

public:
    QStringList          payloadText;
    struct upload_status upload_ctx;

public slots:
    void onTimeToUpdateStatus();
    void onTimeToResendAlarm();
    void restoreSettings();

protected:
    size_t payloadSource(void *ptr, size_t size, size_t nmemb, void *userp);
    void   buildPayload(QString sSubject, QString sMessage);
    bool   logRotate(QString sLogFileName);
    void   saveSettings();
    void   logMessage(QString sMessage);
    bool   sendMail(QString sSubject, QString sMessage);

protected:
    CURL*              pCurl;
    CURLcode           res;
    struct curl_slist* pRecipients;

private:
    QFile*             pLogFile;
    QString            sLogFileName;
    QTimer             updateTimer;
    int32_t            updateInterval;
    QTimer             resendTimer;
    int32_t            resendInterval;
    QDateTime          startTime;
    QDateTime          rotateLogTime;
    QDateTime          currentTime;
    int                gpioHostHandle;
    uint               gpioSensorPin;
    bool               bOnAlarm;
    bool               bAlarmMessageSent;
    QString            sTdata;
    double             dMaxTemperature;

    QString            sUsername;
    QString            sPassword;
    QString            sMailServer;
    QString            sTo;
    QString            sCc;
    QString            sCc1;
    QString            sMessageText;
    DS1820*            pTemperatureSensor;
};
