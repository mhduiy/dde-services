// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powersaveplan.h"
#include "powermanager.h"
#include "../powerconstants.h"
#include "idle/idlewatcher.h"
#include "screen/screencontroller.h"
#include <qlogging.h>

#include <QDBusInterface>
#include <QFile>
#include <algorithm>

using namespace PowerDBus;
using namespace PowerFS;

static bool canAdd(const QString &type, int delay,
                   const QVector<PowerSavePlan::MetaTask> &tasks)
{
    if (tasks.isEmpty())
        return true;
    if (type == QLatin1String("sleep"))
        return true;
    if (type == QLatin1String("screenSaverStart")) {
        int min = tasks.first().delay;
        for (const auto &t : tasks)
            if (t.delay < min) min = t.delay;
        return delay <= min;
    }
    if (type == QLatin1String("screenBlack")) {
        if (delay < tasks.first().delay)
            return true;
        if (delay == tasks.first().delay && tasks.last().name == QLatin1String("lock"))
            return true;
        return false;
    }
    return false;
}

PowerSavePlan::PowerSavePlan(PowerManager *powerManager, QObject *parent)
    : QObject(parent), m_powerManager(powerManager)
{
}

void PowerSavePlan::Start()
{
    Reset();
}

void PowerSavePlan::Reset()
{
    if (!m_powerManager)
        return;

    if (m_powerManager->onBattery())
        OnBattery();
    else
        OnLinePower();
}

void PowerSavePlan::OnBattery()
{
    qWarning("[Power::PSP] OnBattery");
    if (!m_powerManager) return;
    Update(m_powerManager->batteryScreensaverDelay(), m_powerManager->batteryLockDelay(),
           m_powerManager->batteryScreenBlackDelay(), m_powerManager->batterySleepDelay());
}

void PowerSavePlan::OnLinePower()
{
    qWarning("[Power::PSP] OnLinePower");
    if (!m_powerManager) return;
    Update(m_powerManager->linePowerScreensaverDelay(), m_powerManager->linePowerLockDelay(),
           m_powerManager->linePowerScreenBlackDelay(), m_powerManager->linePowerSleepDelay());
}

void PowerSavePlan::Update(int screenSaverStartDelay, int lockDelay,
                           int screenBlackDelay, int sleepDelay)
{
    interruptTasks();
    m_metaTasks.clear();

    qWarning("[Power::PSP] Update: ss=%ds lock=%ds black=%ds sleep=%ds",
             screenSaverStartDelay, lockDelay, screenBlackDelay, sleepDelay);

    if (sleepDelay > 0 && canAdd("sleep", sleepDelay, m_metaTasks)) {
        m_metaTasks.append({sleepDelay, 0, "sleep", [this]{
            makeSystemSleep();
        }});
    }

    if (screenSaverStartDelay > 0 && canAdd("screenSaverStart", screenSaverStartDelay, m_metaTasks)) {
        m_metaTasks.append({screenSaverStartDelay, 0, "screenSaverStart", [this]{
            startScreensaver();
        }});
    }

    if (lockDelay > 0) {
        m_metaTasks.append({lockDelay, 0, "lock", [this]{
            doLock();
        }});
    }

    if (screenBlackDelay > 0 && canAdd("screenBlack", screenBlackDelay, m_metaTasks)) {
        m_metaTasks.append({screenBlackDelay, 0, "screenBlack", [this]{
            screenBlack();
        }});
    }

    int min = 0;
    for (const auto &t : m_metaTasks) {
        if (t.delay < min || min == 0)  {
            min = t.delay;
        }
    }

    qWarning("[Power::PSP] minDelay=%ds, useWayland=%d", min, m_powerManager ? m_powerManager->useWayland() : 0);
    setScreenSaverTimeout(min);

    for (auto &t : m_metaTasks) {
        int nSecs = t.delay - min;
        t.realDelay = nSecs > 0 ? nSecs * 1000 : 1;
    }

    if (m_isIdle) {
        HandleIdleOn();
    }
}

void PowerSavePlan::HandleIdleOn()
{
    qWarning() << "[Power::PSP1] HandleIdleOn, onBattery=" << m_powerManager->onBattery()
               << ", tasks=" << m_metaTasks.size();

    if (!m_powerManager) {
        return; 
    }

    if (m_powerManager->shouldIgnoreIdleOn()) {
        qWarning("[Power::PSP] HandleIdleOn: IGNORE (prepareSuspend=%d)",
                 m_powerManager->prepareSuspendState());
        return;
    }

    m_isIdle = true;
    qWarning("[Power::PSP] HandleIdleOn, onBattery=%d, tasks=%d", m_powerManager->onBattery(), int(m_metaTasks.size()));

    for (const auto &t : m_metaTasks) {
        qWarning("[Power::PSP]   scheduling '%s' in %dms", qPrintable(t.name), t.realDelay);
        scheduleTask(t);
    }

    if (QFile::exists(kNoSuspendFile) && m_powerManager->screenBlackLock())
        m_powerManager->screenController()->setAllModes(ScreenController::On);
}

void PowerSavePlan::HandleIdleOff()
{
    qWarning("[Power::PSP] HandleIdleOff");
    m_isIdle = false;
    interruptTasks();
    if (!m_powerManager) return;
    if (m_powerManager->screenController()) {
        m_powerManager->screenController()->setAllModes(ScreenController::On);
    }
    resetBrightness();
}

void PowerSavePlan::scheduleTask(const MetaTask &t)
{
    qWarning() << "schieduleTask" << t.name << "scheduled to run in" << t.realDelay << "ms";
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, t.fn);
    timer->start(t.realDelay > 0 ? t.realDelay : 100);
    m_timers.append(timer);
}

void PowerSavePlan::interruptTasks()
{
    for (auto *t : m_timers) {
        t->stop();
        t->deleteLater();
    }
    m_timers.clear();
}

void PowerSavePlan::setScreenSaverTimeout(int seconds)
{
    if (!m_powerManager)
        return;
    auto *iw = m_powerManager->idleWatcher();
    if (iw && m_powerManager->useWayland()) {
        iw->setTimeout(static_cast<uint32_t>(seconds));
        return;
    }

    QDBusInterface iface(kScreensaver, kScreensaverPath, kScreensaver,
                          QDBusConnection::sessionBus());
    iface.call("SetTimeout", static_cast<uint32_t>(seconds), 0u, false);
}

void PowerSavePlan::startScreensaver()
{
    if (qEnvironmentVariable("DESKTOP_CAN_SCREENSAVER") == "N")
        return;
    if (!m_allowScreenSaver)
        return;
    QDBusInterface iface(kScreensaver, kScreensaverPath, kScreensaver,
                          QDBusConnection::sessionBus());
    iface.call("Start");
    m_screensaverRunning = true;
}

void PowerSavePlan::stopScreensaver()
{
    if (!m_screensaverRunning)
        return;
    QDBusInterface iface(kScreensaver, kScreensaverPath, kScreensaver,
                          QDBusConnection::sessionBus());
    iface.call("Stop");
    m_screensaverRunning = false;
}

void PowerSavePlan::doLock()
{
    qWarning("[Power::PSP] >>> doLock <<<");
    if (m_powerManager)
        m_powerManager->doLock();
}

void PowerSavePlan::screenBlack()
{
    qWarning("[Power::PSP] >>> screenBlack <<<");
    if (!m_powerManager) return;
    saveCurrentBrightness();
    if (auto *sc = m_powerManager->screenController())
        sc->setAllModes(ScreenController::Off);
    if (m_powerManager->screenBlackLock()) {
        QTimer::singleShot(200, this, [this]() {
            doLock();
        });
    }
}

void PowerSavePlan::makeSystemSleep()
{
    qWarning("[Power::PSP] >>> makeSystemSleep <<<");
    qWarning() << "[Power::PSP] canSuspend=" << m_powerManager->useWayland() << ", prepareSuspendState=" << m_powerManager->prepareSuspendState();
    // stopScreensaver();
    // qWarning("[Power::PSP] makeSystemSleep called");
    if (m_powerManager) {
        if (m_powerManager->useWayland()) {
            m_powerManager->doSuspend();
        }
        else {
            m_powerManager->doSuspendByFront();
        }
    }
}

// ── Brightness ────────────────────────────────────────────────────

void PowerSavePlan::onPowerSavingModeEnabledChanged(bool enabled)
{
    m_psmEnabled = enabled;
    qWarning("[Power::PSP] PowerSavingModeEnabled changed to %d, drop=%u",
             enabled, m_psmDrop);

    if (enabled)
        applyBrightnessDrop();
    else
        resetBrightness();
}

void PowerSavePlan::onBrightnessDropPercentChanged(uint value)
{
    m_psmDrop = value;
    qWarning("[Power::PSP] BrightnessDropPercent changed to %u, enabled=%d",
             value, m_psmEnabled);

    if (m_psmEnabled)
        applyBrightnessDrop();
}

void PowerSavePlan::saveCurrentBrightness()
{
    m_oldBrightness.clear();
    if (!m_powerManager) return;

    auto *sc = m_powerManager->screenController();
    if (!sc || !sc->supportsBrightness()) {
        m_oldBrightness["default"] = 1.0; // legacy placeholder
        return;
    }

    int n = sc->outputCount();
    for (int i = 0; i < n; ++i) {
        double b = sc->brightness(i);
        if (b >= 0.0) {
            m_oldBrightness[QString::number(i)] = b;
            qWarning("[Power::PSP] saveBrightness: output %d = %.1f", i, b);
        }
    }

    if (m_oldBrightness.isEmpty())
        m_oldBrightness["default"] = 1.0;
}

void PowerSavePlan::resetBrightness()
{
    qWarning("[Power::PSP] resetBrightness, saved=%d entries", int(m_oldBrightness.size()));

    if (!m_powerManager) {
        m_oldBrightness.clear();
        return;
    }

    auto *sc = m_powerManager->screenController();
    if (!sc || !sc->supportsBrightness()) {
        m_oldBrightness.clear();
        return;
    }

    for (auto it = m_oldBrightness.cbegin(); it != m_oldBrightness.cend(); ++it) {
        if (it.key() == QLatin1String("default"))
            continue;
        bool ok = false;
        int idx = it.key().toInt(&ok);
        if (ok) {
            sc->setBrightness(idx, it.value());
            qWarning("[Power::PSP] restoreBrightness: output %d -> %.1f", idx, it.value());
        }
    }
    m_oldBrightness.clear();
}

void PowerSavePlan::applyBrightnessDrop()
{
    if (!m_powerManager) return;

    auto *sc = m_powerManager->screenController();
    if (!sc || !sc->supportsBrightness()) {
        qWarning("[Power::PSP] applyBrightnessDrop: brightness not supported");
        return;
    }

    // Save original brightness if we haven't already
    if (m_oldBrightness.isEmpty())
        saveCurrentBrightness();

    int n = sc->outputCount();
    double ratio = 1.0 - static_cast<double>(m_psmDrop) / 100.0;
    ratio = std::clamp(ratio, 0.1, 1.0); // never drop below 10%

    qWarning("[Power::PSP] applyBrightnessDrop: ratio=%.2f, outputs=%d", ratio, n);

    for (int i = 0; i < n; ++i) {
        QString key = QString::number(i);
        double original = m_oldBrightness.value(key, -1.0);
        if (original < 0.0) continue;

        double target = original * ratio;
        target = std::clamp(target, 10.0, 100.0);

        sc->setBrightness(i, target);
        qWarning("[Power::PSP] drop brightness: output %d: %.1f -> %.1f", i, original, target);
    }
}
