// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencontroller_wl.h"
#include <QGuiApplication>
#include <QTimer>
#include <QDebug>
#include <wayland-client.h>
#include <cstring>

// ── OutputPower (existing) ─────────────────────────────────────────

OutputPower::OutputPower(::zwlr_output_power_v1 *obj)
    : QtWayland::zwlr_output_power_v1(obj) {}

OutputPower::~OutputPower() { destroy(); }

void OutputPower::zwlr_output_power_v1_mode(uint32_t mode) { Q_EMIT modeChanged(mode); }
void OutputPower::zwlr_output_power_v1_failed() {}

// ── OutputPowerManager (existing) ──────────────────────────────────

OutputPowerManager::OutputPowerManager()
    : QWaylandClientExtensionTemplate<OutputPowerManager>(1) {}

OutputPowerManager::~OutputPowerManager()
{
    if (isInitialized()) destroy();
}

void OutputPowerManager::instantiate() { initialize(); }

std::unique_ptr<OutputPower> OutputPowerManager::getOutputPower(wl_output *output)
{
    if (!isInitialized() || !output) return nullptr;
    auto *raw = get_output_power(output);
    if (!raw) return nullptr;
    return std::make_unique<OutputPower>(raw);
}

// ── OutputColorControl ─────────────────────────────────────────────

OutputColorControl::OutputColorControl(::treeland_output_color_control_v1 *obj)
    : QtWayland::treeland_output_color_control_v1(obj) {}

OutputColorControl::~OutputColorControl() { destroy(); }

void OutputColorControl::treeland_output_color_control_v1_brightness(wl_fixed_t brightness)
{
    m_brightness = wl_fixed_to_double(brightness);
    qWarning("[Power::WL] ColorControl: brightness event -> %.1f", m_brightness);
    Q_EMIT brightnessChanged(m_brightness);
}

void OutputColorControl::treeland_output_color_control_v1_color_temperature(uint32_t) {}

void OutputColorControl::treeland_output_color_control_v1_result(uint32_t) {}

// ── TreeLandOutputMgr ──────────────────────────────────────────────

TreeLandOutputMgr::TreeLandOutputMgr()
    : QWaylandClientExtensionTemplate<TreeLandOutputMgr>(2) {}

TreeLandOutputMgr::~TreeLandOutputMgr()
{
    if (isInitialized()) destroy();
}

void TreeLandOutputMgr::instantiate() { initialize(); }

OutputColorControl *TreeLandOutputMgr::getColorControl(wl_output *output)
{
    if (!isInitialized() || !output) return nullptr;
    auto *raw = get_color_control(output);
    if (!raw) return nullptr;
    return new OutputColorControl(raw);
}

// ── WaylandScreenController ────────────────────────────────────────

WaylandScreenController::WaylandScreenController(QObject *parent)
    : ScreenController(parent)
{
    auto *app = qGuiApp;
    if (!app) { qWarning("[Power::WL] Scrn: no QGuiApplication"); return; }
    auto *wlApp = app->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wlApp) { qWarning("[Power::WL] Scrn: no QWaylandApplication"); return; }

    m_display = wlApp->display();
    if (!m_display) { qWarning("[Power::WL] Scrn: no wl_display"); return; }

    m_manager.reset(new OutputPowerManager);
    m_manager->instantiate();
    if (!m_manager->isInitialized()) { qWarning("[Power::WL] Scrn: manager init failed"); return; }

    m_treeLandMgr.reset(new TreeLandOutputMgr);
    m_treeLandMgr->instantiate();
    if (m_treeLandMgr->isInitialized()) {
        wl_display_roundtrip(m_display);
        m_brightnessAvailable = true;
        qWarning("[Power::WL] Scrn: treeland brightness available");
    } else {
        qWarning("[Power::WL] Scrn: treeland brightness NOT available");
    }

    qWarning("[Power::WL] Scrn: manager ok, discovering outputs…");
    discoverOutputs();
    m_valid = true;
}

WaylandScreenController::~WaylandScreenController()
{
    for (auto *anim : m_brightnessAnims) {
        anim->stop();
        anim->deleteLater();
    }
    m_brightnessAnims.clear();
    m_outputs.clear();
    m_treeLandMgr.reset();
    m_manager.reset();
}

static void scOutputGlobal(void *data, wl_registry *r, uint32_t name,
                           const char *iface, uint32_t)
{
    auto *sc = static_cast<WaylandScreenController *>(data);
    if (std::strcmp(iface, wl_output_interface.name) == 0) {
        auto *wlo = static_cast<wl_output *>(
            wl_registry_bind(r, name, &wl_output_interface, 1));
        WaylandScreenController::Output out;
        out.wlOutput = wlo;
        out.power = sc->m_manager->getOutputPower(wlo);
        if (out.power) {
            QObject::connect(out.power.get(), &OutputPower::modeChanged,
                sc, [sc, idx = int(sc->m_outputs.size())](uint32_t m) {
                if (idx < int(sc->m_outputs.size())) {
                    sc->m_outputs[idx].currentMode = m;
                    Q_EMIT sc->modeChanged(idx, m == 0 ? ScreenController::Off : ScreenController::On);
                }
            });
        }
        if (sc->m_brightnessAvailable) {
            out.colorControl.reset(sc->m_treeLandMgr->getColorControl(wlo));
            if (out.colorControl) {
                QObject::connect(out.colorControl.get(), &OutputColorControl::brightnessChanged,
                    sc, [sc, idx = int(sc->m_outputs.size())](double v) {
                    if (idx < int(sc->m_outputs.size()))
                        Q_EMIT sc->brightnessChanged(idx, v);
                });
            }
        }
        qWarning("[Power::WL] Scrn: found output %u, brightness=%d",
                 name, out.colorControl != nullptr);
        sc->m_outputs.push_back(std::move(out));
    }
}

static void scOutputRemove(void *, wl_registry *, uint32_t) {}
static const wl_registry_listener kOutputListener = {scOutputGlobal, scOutputRemove};

void WaylandScreenController::discoverOutputs()
{
    qWarning("[Power::WL] Scrn: discoverOutputs start");
    auto *reg = wl_display_get_registry(m_display);
    wl_registry_add_listener(reg, &kOutputListener, this);
    wl_display_roundtrip(m_display);
    wl_display_roundtrip(m_display);
    qWarning("[Power::WL] Scrn: discoverOutputs done, found %d outputs", int(m_outputs.size()));
}

ScreenController::Mode WaylandScreenController::mode(int index) const
{
    if (index < 0 || size_t(index) >= m_outputs.size()) return On;
    return m_outputs[size_t(index)].currentMode == 0 ? Off : On;
}

void WaylandScreenController::setMode(int index, Mode m)
{
    if (index < 0 || size_t(index) >= m_outputs.size()) return;
    auto &out = m_outputs[size_t(index)];
    uint32_t wm = m == Off ? 0 : 1;
    if (out.power) {
        out.power->set_mode(wm);
        wl_display_flush(m_display);
    }
    out.currentMode = wm;
    Q_EMIT modeChanged(index, m);
}

double WaylandScreenController::brightness(int index) const
{
    if (index < 0 || size_t(index) >= m_outputs.size()) {
        qWarning("[Power::WL] brightness(%d): out of range (total=%d)", index, int(m_outputs.size()));
        return -1.0;
    }
    auto &out = m_outputs[size_t(index)];
    if (out.colorControl) {
        double b = out.colorControl->currentBrightness();
        qWarning("[Power::WL] brightness(%d) = %.1f", index, b);
        return b;
    }
    qWarning("[Power::WL] brightness(%d): no colorControl", index);
    return -1.0;
}

void WaylandScreenController::setBrightness(int index, double value)
{
    if (index < 0 || size_t(index) >= m_outputs.size()) {
        qWarning("[Power::WL] setBrightness(%d, %.1f): out of range (total=%d)", index, value, int(m_outputs.size()));
        return;
    }
    auto &out = m_outputs[size_t(index)];
    if (!out.colorControl) {
        qWarning("[Power::WL] setBrightness(%d, %.1f): no colorControl", index, value);
        return;
    }

    double current = out.colorControl->currentBrightness();
    if (current < 0.0 || std::abs(current - value) < 0.5) {
        out.colorControl->set_brightness(wl_fixed_from_double(value));
        out.colorControl->commit();
        wl_display_flush(m_display);
        return;
    }

    if (auto *old = m_brightnessAnims.take(index)) {
        old->disconnect(this);
        old->stop();
        old->deleteLater();
    }

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(400);
    anim->setStartValue(current);
    anim->setEndValue(value);
    anim->setEasingCurve(QEasingCurve::InOutCubic);

    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, index](const QVariant &v) {
        if (index < 0 || size_t(index) >= m_outputs.size()) return;
        auto &o = m_outputs[size_t(index)];
        if (o.colorControl) {
            o.colorControl->set_brightness(wl_fixed_from_double(v.toDouble()));
            o.colorControl->commit();
            wl_display_flush(m_display);
        }
    });

    m_brightnessAnims[index] = anim;
    anim->start();

    qWarning("[Power::WL] setBrightness(%d, %.1f -> %.1f) animated", index, current, value);
}
