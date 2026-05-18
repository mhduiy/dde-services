// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"
#include "batterymanager.h"
#include "systemdbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>
#include <QProcess>
#include <QFile>
#include <DConfig>

using namespace PowerDBus;
using namespace PowerFS;
using namespace PowerDConfig;

SystemPowerManager::SystemPowerManager(QDBusConnection *conn, const QString &svc,
                                       QObject *p)
    : QObject(p)
    , m_conn(conn)
{
    Q_UNUSED(svc);
}

bool SystemPowerManager::initialize()
{
    bool ok = m_conn->registerObject(kPath, this,
        QDBusConnection::ExportAllSlots |
        QDBusConnection::ExportAllSignals |
        QDBusConnection::ExportAllProperties);
    if (!ok) {
        qWarning("[Power::Sys] registerObject failed");
        return false;
    }

    initLidSwitch();
    initPowerSavingDConfig();
    initCpuGovernor();

    auto *battery = new BatteryManager(this, this);
    connect(battery, &BatteryManager::onBatteryChanged, this, [this](bool onBatt) {
        if (m_onBattery != onBatt) {
            m_onBattery = onBatt;
            Q_EMIT onBatteryChanged();
        }
    });
    connect(battery, &BatteryManager::batteryChanged, this, [battery]() {
        battery->probe();
    });

    return true;
}

void SystemPowerManager::initLidSwitch()
{
    SystemDBusProxy proxy(this);
    QString chassis = proxy.chassis();
    if (chassis != "laptop" && chassis != "convertible")
        return;

    m_hasLidSwitch = proxy.lidIsPresent();
    if (!m_hasLidSwitch) {
        // 后备: 尝试 /proc/acpi/button/lid/LID/state
        QFile f(kLidStatePath);
        if (f.exists()) {
            m_hasLidSwitch = true;
        }
    }

    if (!m_hasLidSwitch)
        return;

    Q_EMIT hasLidSwitchChanged();

    // 读取初始状态
    QDBusInterface upower(kUPowerService, kUPowerPath, "org.freedesktop.DBus.Properties",
                          QDBusConnection::systemBus());
    if (upower.isValid()) {
        QDBusReply<QVariant> reply = upower.call("Get", kUPowerService, "LidIsClosed");
        if (reply.isValid()) {
            bool closed = reply.value().toBool();
            m_lidClosed = closed;
            Q_EMIT lidClosedChanged();
            handleLidSwitchEvent(closed);
        }
    }

    // 持续监听 UPower 的 PropertiesChanged 信号，确保每次合盖/开盖都能收到通知
    QDBusConnection::systemBus().connect(
        kUPowerService, kUPowerPath,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        this, SLOT(onUPowerPropertiesChanged(QString,QVariantMap,QStringList)));
}

void SystemPowerManager::onUPowerPropertiesChanged(const QString &interface,
                                                    const QVariantMap &changed,
                                                    const QStringList &)
{
    if (interface != QLatin1String(kUPowerService))
        return;

    if (changed.contains("LidIsClosed")) {
        bool closed = changed.value("LidIsClosed").toBool();
        qWarning("[Power::Sys] UPower LidIsClosed changed: %d", int(closed));
        if (m_lidClosed != closed) {
            m_lidClosed = closed;
            Q_EMIT lidClosedChanged();
        }
        handleLidSwitchEvent(closed);
    }
}

void SystemPowerManager::handleLidSwitchEvent(bool closed)
{
    if (closed) {
        qWarning("[Power::Sys] >>> LidClosed signal <<<");
        Q_EMIT LidClosed();
    } else {
        qWarning("[Power::Sys] >>> LidOpened signal <<<");
        Q_EMIT LidOpened();
    }
}

void SystemPowerManager::initPowerSavingDConfig()
{
    auto *dc = Dtk::Core::DConfig::create(kAppId, kPowerName, "", this);
    if (!dc) return;

    auto load = [dc, this](const QString &k) {
        QVariant v = dc->value(k);
        if (k == QLatin1String(kPowerSavingModeEnabled))
            setPowerSavingModeEnabled(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeAuto))
            setPowerSavingModeAuto(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeAutoWhenBatteryLow))
            setPowerSavingModeAutoWhenBatteryLow(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeBrightnessDropPercent))
            setPowerSavingModeBrightnessDropPercent(v.toUInt());
        else if (k == QLatin1String(kPowerSavingModeAutoBatteryPercent))
            setPowerSavingModeAutoBatteryPercent(v.toUInt());
    };

    load(kPowerSavingModeEnabled);
    load(kPowerSavingModeAuto);
    load(kPowerSavingModeAutoWhenBatteryLow);
    load(kPowerSavingModeBrightnessDropPercent);
    load(kPowerSavingModeAutoBatteryPercent);

    connect(dc, &Dtk::Core::DConfig::valueChanged, this,
            [load](const QString &key) {
        load(key);
    });
}

void SystemPowerManager::initCpuGovernor()
{
    QFile gf("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (gf.open(QIODevice::ReadOnly)) {
        QString gov = gf.readAll().trimmed();
        gf.close();
        if (!gov.isEmpty())
            setCpuGovernor(gov);
    }

    QFile af("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors");
    if (af.open(QIODevice::ReadOnly)) {
        QStringList avail = QString(af.readAll()).split(' ', Qt::SkipEmptyParts);
        af.close();
        bool hp = avail.contains("performance") || avail.contains("ondemand");
        bool ps = avail.contains("powersave") || avail.contains("ondemand");
        if (m_hpSupported != hp) {
            m_hpSupported = hp;
            Q_EMIT isHighPerformanceSupportedChanged();
        }

        if (m_psSupported != ps) {
            m_psSupported = ps;
            Q_EMIT isPowerSaveSupportedChanged();
        }
    }

    QFile bf("/sys/devices/system/cpu/cpufreq/boost");
    if (bf.open(QIODevice::ReadOnly)) {
        setCpuBoost(bf.readAll().trimmed() == "1");
        bf.close();
    }
}

void SystemPowerManager::updateHasBattery(bool has)
{
    if (m_hasBattery != has) {
        m_hasBattery = has;
        Q_EMIT hasBatteryChanged();
    }
}

void SystemPowerManager::updateBatteryInfo(double pct, uint status,
                                            quint64 tte, quint64 ttf, double cap)
{
    if (m_batteryPercentage != pct) { 
        m_batteryPercentage = pct;
        Q_EMIT batteryPercentageChanged();
    }

    if (m_batteryStatus != status) {
        m_batteryStatus = status;
        Q_EMIT batteryStatusChanged();
    }

    if (m_batteryTimeToEmpty != tte)
    {
        m_batteryTimeToEmpty = tte;
        Q_EMIT batteryTimeToEmptyChanged();
    }

    if (m_batteryTimeToFull != ttf) { 
        m_batteryTimeToFull = ttf; 
        Q_EMIT batteryTimeToFullChanged(); 
    }
    if (m_batteryCapacity != cap) { 
        m_batteryCapacity = cap; 
        Q_EMIT batteryCapacityChanged(); 
    }
}

QList<QDBusObjectPath> SystemPowerManager::GetBatteries() {
    return {};
}

void SystemPowerManager::Refresh() 
{
    RefreshBatteries();
    RefreshMains();
}

void SystemPowerManager::RefreshBatteries()
{

}

void SystemPowerManager::RefreshMains()
{

}

void SystemPowerManager::setMode(const QString &v)
{
    static const QStringList valid = {"balance", "powersave", "performance"};
    if (!valid.contains(v)) { 
        qWarning("[Power::Sys] invalid mode: %s", qPrintable(v)); 
        return;
    }

    if (m_mode == v) return;
    m_mode = v;
    Q_EMIT modeChanged();

    QString dspc;
    if (v == "performance") {
        dspc = "performance";
    } else if (v == "powersave") {
        dspc = "saving";
    } else {
        dspc = "balance";
    }

    qWarning("[Power::Sys] setMode %s → deepin-power-control set %s", qPrintable(v), qPrintable(dspc));
    QProcess::startDetached("/usr/sbin/deepin-power-control", {"set", dspc});

    setPowerSavingModeEnabled(v == "powersave");
}

void SystemPowerManager::SetCpuGovernor(const QString &gov)
{
    setCpuGovernor(gov);
}

void SystemPowerManager::SetCpuBoost(bool on)
{
    setCpuBoost(on);
}

void SystemPowerManager::LockCpuFreq(const QString &gov, int lockTime)
{
    Q_UNUSED(gov); Q_UNUSED(lockTime);
}
