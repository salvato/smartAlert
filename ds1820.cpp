#include "ds1820.h"
#include <QDir>
#include <QStringList>
#include <QFile>


DS1820::DS1820() {
    tMin =  0.0;
    tMax = 30.0;
    bOnAlarm = false;
    sAlarmString = "No Alarm";
}


void
DS1820::setLimits(double minTemperature, double maxTemperature) {
    if(minTemperature < maxTemperature) {
        tMin = minTemperature;
        tMax = maxTemperature;
    }
}


bool
DS1820::isConnected() {
    bool bFound = false;
    QString s1WireDir = "/sys/bus/w1/devices/";
    QDir dir1Wire(s1WireDir);
    if(dir1Wire.exists()) {
        dir1Wire.setFilter(QDir::Dirs);
        QStringList filter;
        filter.append(QString("10-*"));
        filter.append(QString("28-*"));
        QStringList subDirs = dir1Wire.entryList(filter);

        if(subDirs.count() != 0) {
            for(int i=0; i<subDirs.count(); i++) {
                sSensorFilePath = dir1Wire.absolutePath() +
                                  QString("/") +
                                  subDirs.at(i) +
                                  QString("/w1_slave");
                QFile* pSensorFile = new QFile(sSensorFilePath);
                if(!pSensorFile->open(QIODevice::Text|QIODevice::ReadOnly)) {
                    delete pSensorFile;
                    pSensorFile = nullptr;
                    continue;
                }
                QString sTdata = QString(pSensorFile->readAll());
                if(sTdata.contains("YES")) {
                    bFound = true;
                    pSensorFile->close();
                    delete pSensorFile;
                    pSensorFile = nullptr;
                    break;
                }
                pSensorFile->close();
                delete pSensorFile;
                pSensorFile = nullptr;
            }
        }
    }
    return bFound;
}


// Return the Temperature read from DS1820 or a value
// lower than -300.0 to signal an erratic sensor
double
DS1820::readTemperature() {
    double temperature = -999.9;
    QFile SensorFile(sSensorFilePath);
    if(SensorFile.open(QIODevice::Text|QIODevice::ReadOnly)) {
        QString sTdata = QString(SensorFile.readAll());
        QByteArray ba = sTdata.toLocal8Bit();
        if(sTdata.contains("YES")) {
            int iPos = sTdata.indexOf("t=");
            if(iPos > 0) {
                temperature = double(sTdata.mid(iPos+2).toDouble()/1000.0);
            }
        }
        SensorFile.close();
    }
    if(temperature < tMin) {
        bOnAlarm = true;
        sAlarmString = "Temperature Lower than Minimum";
    }
    else if(temperature > tMax) {
        bOnAlarm = true;
        sAlarmString = "Temperature Greater than Maximum";
    }
    else {
        bOnAlarm = false;
        sAlarmString = "No Alarm";
    }

    return temperature;
}


bool
DS1820::isOnAlarm() {
    return bOnAlarm;
}
