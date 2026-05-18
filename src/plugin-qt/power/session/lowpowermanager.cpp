// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "lowpowermanager.h"
#include "powermanager.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusConnection>
#include <QProcess>

using namespace PowerFS;
using namespace PowerDConfig;

LowPowerManager::LowPowerManager(PowerManager *powerManager, QObject *parent)
    : QObject(parent), m_powerManager(powerManager)
{
    connect(m_powerManager, &PowerManager::batteryPercentageChanged,
            this, &LowPowerManager::updateWarnLevel);
    connect(m_powerManager, &PowerManager::onBatteryChanged,
            this, &LowPowerManager::updateWarnLevel);
}

void LowPowerManager::initConfig(Dtk::Core::DConfig *config)
{
    m_config = config;
    if (!m_config) return;

    connect(m_config, &Dtk::Core::DConfig::valueChanged,
            this, &LowPowerManager::onConfigChanged);

    m_usePercentageForPolicy = m_config->value(kUsePercentageForPolicy, true).toBool();
    m_lowPowerNotifyThreshold = m_config->value(kLowPowerNotifyThreshold, 0).toInt();
    m_percentageAction = m_config->value(kPercentageAction, 0).toInt();
    m_timeToEmptyLow = static_cast<quint64>(m_config->value(kTimeToEmptyLow, 0).toLongLong());
    m_timeToEmptyDanger = static_cast<quint64>(m_config->value(kTimeToEmptyDanger, 0).toLongLong());
    m_timeToEmptyCritical = static_cast<quint64>(m_config->value(kTimeToEmptyCritical, 0).toLongLong());
    m_timeToEmptyAction = static_cast<quint64>(m_config->value(kTimeToEmptyAction, 0).toLongLong());
}

void LowPowerManager::onConfigChanged(const QString &key)
{
    if (!m_config) return;

    if (key == QLatin1String(kUsePercentageForPolicy))
        m_usePercentageForPolicy = m_config->value(kUsePercentageForPolicy, true).toBool();
    else if (key == QLatin1String(kLowPowerNotifyThreshold))
        m_lowPowerNotifyThreshold = m_config->value(kLowPowerNotifyThreshold, 0).toInt();
    else if (key == QLatin1String(kPercentageAction))
        m_percentageAction = m_config->value(kPercentageAction, 0).toInt();
    else if (key == QLatin1String(kTimeToEmptyLow))
        m_timeToEmptyLow = static_cast<quint64>(m_config->value(kTimeToEmptyLow, 0).toLongLong());
    else if (key == QLatin1String(kTimeToEmptyDanger))
        m_timeToEmptyDanger = static_cast<quint64>(m_config->value(kTimeToEmptyDanger, 0).toLongLong());
    else if (key == QLatin1String(kTimeToEmptyCritical))
        m_timeToEmptyCritical = static_cast<quint64>(m_config->value(kTimeToEmptyCritical, 0).toLongLong());
    else if (key == QLatin1String(kTimeToEmptyAction))
        m_timeToEmptyAction = static_cast<quint64>(m_config->value(kTimeToEmptyAction, 0).toLongLong());
    else
        return;

    updateWarnLevel();
}

uint LowPowerManager::getWarnLevel(double percentage, quint64 timeToEmpty)
{
    auto *m = qobject_cast<PowerManager *>(parent());
    if (!m || !m->onBattery())
        return None;

    if (m_usePercentageForPolicy) {
        if (percentage == 0.0)
            return None;

        if (percentage <= m_lowPowerNotifyThreshold) {
            if (m_percentageAction > 0 && percentage <= m_percentageAction)
                return Action;
            if (percentage <= 10.0)
                return Critical;
            if (percentage <= 15.0)
                return Danger;
            if (percentage <= 20.0)
                return Low;
            if (percentage <= 25.0)
                return Remind;
            return None;
        }

        return None;
    } else {
        if (timeToEmpty > m_timeToEmptyLow || timeToEmpty == 0)
            return None;
        if (timeToEmpty > m_timeToEmptyDanger)
            return Low;
        if (timeToEmpty > m_timeToEmptyCritical)
            return Danger;
        if (timeToEmpty > m_timeToEmptyAction)
            return Critical;
        return Action;
    }
}

void LowPowerManager::updateWarnLevel()
{
    auto *m = qobject_cast<PowerManager *>(parent());
    if (!m || !m->onBattery()) {
        if (m_currentLevel != None) {
            m_currentLevel = None;
            handleLevelChanged(None);
        }
        disableTicker();
        closeLowPower();
        return;
    }

    double pct = 100.0;
    auto bat = m->batteryPercentage();
    if (!bat.isEmpty())
        pct = bat.first();

    quint64 tte = m->batteryTimeToEmpty();

    uint newLevel = getWarnLevel(pct, tte);
    if (newLevel == m_currentLevel)
        return;

    m_currentLevel = newLevel;
    handleLevelChanged(newLevel);
}

void LowPowerManager::handleLevelChanged(uint level)
{
    disableTicker();

    switch (level) {
    case Action: {
        auto *m = qobject_cast<PowerManager *>(parent());
        if (m && m->scheduledShutdownState())
            m->scheduledShutdown(SchedInit);  // matches Go: m.scheduledShutdown(Init)
        playBatterySound();
        sendNotify(tr("Battery critically low"));
        startCountTicker();
        break;
    }
    case Critical:
    case Danger:
    case Low:
    case Remind:
        playBatterySound();
        sendNotify(tr("Battery low, please plug in"));
        break;
    case None: {
        closeLowPower();
        auto *m = qobject_cast<PowerManager *>(parent());
        if (m) {
            m->closeNotify();
            if (m->scheduledShutdownState())
                m->scheduledShutdown(SchedInit);
        }
        break;
    }
    }
}

void LowPowerManager::startCountTicker()
{
    m_count = 0;
    m_countTicker = new QTimer(this);

    connect(m_countTicker, &QTimer::timeout, this, [this]() {
        m_count++;

        auto *m = qobject_cast<PowerManager *>(parent());
        if (!m) {
            disableTicker();
            return;
        }

        if (m_count == 3) {
            if (m->sleepLock())
                lockWaitShow(5000, false);
        } else if (m_count == 4) {
            showLowPower();
        } else if (m_count >= 5) {
            disableTicker();
            if (m->lowPowerAction() == 1)
                m->doHibernate();
            else
                m->doSuspend();
        }
    });

    m_countTicker->start(1000);
}

void LowPowerManager::lockWaitShow(int timeoutMs, bool autoStartAuth)
{
    auto *m = qobject_cast<PowerManager *>(parent());
    if (!m) return;

    m->doLock(autoStartAuth);

    auto *pollTimer = new QTimer(this);
    auto *endTimer = new QTimer(this);
    endTimer->setSingleShot(true);

    connect(pollTimer, &QTimer::timeout, this, [pollTimer, endTimer]() {
        QDBusInterface sm(PowerDBus::kSessionManager, PowerDBus::kSessionPath,
                          PowerDBus::kSessionManager, QDBusConnection::sessionBus());
        if (sm.isValid() && sm.property("Locked").toBool()) {
            pollTimer->stop();
            endTimer->stop();
            delete pollTimer;
            delete endTimer;
        }
    });

    connect(endTimer, &QTimer::timeout, this, [pollTimer, endTimer]() {
        pollTimer->stop();
        delete pollTimer;
        delete endTimer;
    });

    pollTimer->start(300);
    endTimer->start(timeoutMs);
}

void LowPowerManager::playBatterySound()
{
    QProcess::startDetached("paplay", {"/usr/share/sounds/deepin/stereo/battery-low.ogg"});
}

void LowPowerManager::disableTicker()
{
    if (m_countTicker) {
        m_countTicker->stop();
        delete m_countTicker;
        m_countTicker = nullptr;
    }
}

void LowPowerManager::sendNotify(const QString &body)
{
    m_powerManager->sendNotify("", body);
}

void LowPowerManager::showLowPower()
{
    QProcess::startDetached(kLowPowerCmd, {"--raise"});
}

void LowPowerManager::closeLowPower()
{
    QProcess::startDetached(kLowPowerCmd, {"--quit"});
}
