// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "lidswitchhandler.h"
#include "powermanager.h"
#include "../powerconstants.h"
#include "screen/screencontroller.h"
#include "idle/idlewatcher.h"

#include <QDBusConnection>
#include <QDebug>

using namespace PowerDBus;

LidSwitchHandler::LidSwitchHandler(QObject *parent)
    : QObject(parent)
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(1500);

    connect(m_debounce, &QTimer::timeout, this, [this]() {
        qWarning("[Power::Lid] debounce triggered, opened=%d", int(m_pendingOpen));
        doLidStateChanged(m_pendingOpen);
    });

    auto bus = QDBusConnection::systemBus();
    bus.connect(kService, kPath, kInterface, "LidClosed",
                this, SLOT(onLidClosed()));
    bus.connect(kService, kPath, kInterface, "LidOpened",
                this, SLOT(onLidOpened()));
}

void LidSwitchHandler::onLidClosed()
{
    qWarning("[Power::Lid] >>> LidClosed <<<");
    m_pendingOpen = false;
    m_debounce->start();
}

void LidSwitchHandler::onLidOpened()
{
    qWarning("[Power::Lid] >>> LidOpened <<<");
    m_pendingOpen = true;
    m_debounce->start();
}

void LidSwitchHandler::doLidStateChanged(bool opened)
{
    auto *m = qobject_cast<PowerManager *>(parent());
    if (!m) return;

    if (!opened) {
        // 合盖
        m->SetPrepareSuspend(PS_LidClose);
        bool onBattery = m->onBattery();
        int32_t action = onBattery ? m->batteryLidClosedAction()
                                   : m->linePowerLidClosedAction();
        qWarning("[Power::Lid] closing, onBattery=%d action=%d", int(onBattery), int(action));

        switch (action) {
        case PA_Shutdown:
            m->doShutdown();
            break;
        case PA_Suspend:
            if (m->useWayland())
                m->doSuspend();
            else
                m->doSuspendByFront();
            break;
        case PA_Hibernate:
            if (m->useWayland())
                m->doHibernate();
            else
                m->doHibernate();
            break;
        case PA_TurnOffScreen:
            m->doTurnOffScreen();
            break;
        case PA_Lock:
            m->doLock();
            break;
        case PA_DoNothing:
            return;
        default:
            break;
        }

        if (action != PA_TurnOffScreen)
            m->setBlackScreenActive(true);
    } else {
        // 开盖
        qWarning("[Power::Lid] opening");

        if (m->useWayland())
            m->SetPrepareSuspend(PS_Resume);

        if (auto *iw = m->idleWatcher())
            iw->simulateActivity();

        m->setBlackScreenActive(false);
        m->setDPMSModeOn();
    }
}
