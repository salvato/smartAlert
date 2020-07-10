#pragma once

#include <QString>


class DS1820
{
public:
    DS1820();
    bool isConnected();
    bool isOnAlarm();
    double readTemperature();
    void setLimits(double minTemperature, double maxTemperature);

private:
    QString sSensorFilePath;
    double tMin, tMax;
    bool bOnAlarm;
    QString sAlarmString;
};

