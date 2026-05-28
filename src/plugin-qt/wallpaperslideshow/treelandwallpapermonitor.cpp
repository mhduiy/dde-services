// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "treelandwallpapermonitor.h"

#ifdef Enable_Treeland

#include <QtWaylandClient/QWaylandClientExtension>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformnativeinterface.h>
#include <QGuiApplication>
#include <QScreen>

#include "wayland-treeland-wallpaper-manager-unstable-v1-client-protocol.h"
#include "qwayland-treeland-wallpaper-manager-unstable-v1.h"

Q_LOGGING_CATEGORY(DdeWSPTreelandMonitor, "dde-ws-treeland-monitor")

class WallpaperContext : public QWaylandClientExtensionTemplate<WallpaperContext>,
                         public QtWayland::treeland_wallpaper_v1
{
    Q_OBJECT
public:
    explicit WallpaperContext(struct ::treeland_wallpaper_v1 *context)
        : QWaylandClientExtensionTemplate<WallpaperContext>(1)
        , QtWayland::treeland_wallpaper_v1(context)
    {
    }

Q_SIGNALS:
    void wallpaperChanged(const QString &fileSource);

protected:
    void treeland_wallpaper_v1_changed(uint32_t role, uint32_t source_type, const QString &file_source) override
    {
        Q_UNUSED(role)
        Q_UNUSED(source_type)
        qCWarning(DdeWSPTreelandMonitor) << "wallpaper changed event from compositor:" << file_source;
        Q_EMIT wallpaperChanged(file_source);
    }

    void treeland_wallpaper_v1_failed(const QString &file_source, uint32_t error) override
    {
        qCWarning(DdeWSPTreelandMonitor) << "wallpaper failed:" << file_source << "error:" << error;
    }
};

class WallpaperManager : public QWaylandClientExtensionTemplate<WallpaperManager>,
                         public QtWayland::treeland_wallpaper_manager_v1
{
    Q_OBJECT
public:
    explicit WallpaperManager(QObject *parent = nullptr)
        : QWaylandClientExtensionTemplate<WallpaperManager>(1)
    {
        qCWarning(DdeWSPTreelandMonitor) << "WallpaperManager created, platform:" << QGuiApplication::platformName();
        if (QGuiApplication::platformName() == QLatin1String("wayland")) {
            auto *waylandIntegration = static_cast<QtWaylandClient::QWaylandIntegration *>(
                QGuiApplicationPrivate::platformIntegration());
            if (!waylandIntegration) {
                qCWarning(DdeWSPTreelandMonitor) << "waylandIntegration is nullptr";
                return;
            }
            m_waylandDisplay = waylandIntegration->display();
            if (!m_waylandDisplay) {
                qCWarning(DdeWSPTreelandMonitor) << "waylandDisplay is nullptr";
                return;
            }
            qCWarning(DdeWSPTreelandMonitor) << "WallpaperManager adding registry listener";
            addListener();
        } else {
            qCWarning(DdeWSPTreelandMonitor) << "not on wayland, skipping registry listener";
        }
        setParent(parent);
    }

private:
    void addListener()
    {
        if (!m_waylandDisplay) {
            qCWarning(DdeWSPTreelandMonitor) << "waylandDisplay is nullptr, skip addListener";
            return;
        }
        m_waylandDisplay->addRegistryListener(&handleListenerGlobal, this);
    }

    static void handleListenerGlobal(void *data, wl_registry *registry, uint32_t id,
                                      const QString &interface, uint32_t version)
    {
        qCWarning(DdeWSPTreelandMonitor) << "registry global:" << interface << "id:" << id << "version:" << version;
        if (interface == treeland_wallpaper_manager_v1_interface.name) {
            auto *manager = static_cast<WallpaperManager *>(data);
            if (!manager) {
                qCWarning(DdeWSPTreelandMonitor) << "WallpaperManager is nullptr";
                return;
            }
            qCWarning(DdeWSPTreelandMonitor) << "found treeland_wallpaper_manager_v1, initializing";
            manager->init(registry, id, version);
        }
    }

    QtWaylandClient::QWaylandDisplay *m_waylandDisplay = nullptr;
};

TreelandWallpaperMonitor::TreelandWallpaperMonitor(QObject *parent)
    : QObject(parent)
{
}

TreelandWallpaperMonitor::~TreelandWallpaperMonitor()
{
    qDeleteAll(m_wallpaperContexts);
    m_wallpaperContexts.clear();
}

void TreelandWallpaperMonitor::init()
{
    qCWarning(DdeWSPTreelandMonitor) << "TreelandWallpaperMonitor::init()";
    if (m_wallpaperManager.isNull()) {
        m_wallpaperManager.reset(new WallpaperManager(this));
        connect(m_wallpaperManager.get(), &WallpaperManager::activeChanged, this, [this]() {
            qCWarning(DdeWSPTreelandMonitor) << "WallpaperManager activeChanged, isActive:" << m_wallpaperManager->isActive();
            if (m_wallpaperManager->isActive()) {
                const auto screens = qApp->screens();
                qCWarning(DdeWSPTreelandMonitor) << "creating wallpaper contexts for" << screens.size() << "screen(s)";
                for (auto *screen : screens) {
                    getOrCreateWallpaperContext(screen->name());
                }
                Q_EMIT ready();
            }
        });
    }
}

bool TreelandWallpaperMonitor::isActive() const
{
    bool active = !m_wallpaperManager.isNull() && m_wallpaperManager->isActive();
    qCWarning(DdeWSPTreelandMonitor) << "isActive:" << active;
    return active;
}

QString TreelandWallpaperMonitor::getCurrentWallpaper(const QString &monitorName) const
{
    const QString &wp = m_currentWallpapers.value(monitorName);
    qCWarning(DdeWSPTreelandMonitor) << "getCurrentWallpaper for" << monitorName << ":" << wp;
    return wp;
}

WallpaperContext *TreelandWallpaperMonitor::getOrCreateWallpaperContext(const QString &monitorName)
{
    qCWarning(DdeWSPTreelandMonitor) << "getOrCreateWallpaperContext for" << monitorName;
    if (m_wallpaperContexts.contains(monitorName)) {
        qCWarning(DdeWSPTreelandMonitor) << "wallpaper context already exists for" << monitorName;
        return m_wallpaperContexts.value(monitorName);
    }

    auto *native = QGuiApplication::platformNativeInterface();
    const auto screens = qApp->screens();
    for (auto *screen : screens) {
        if (screen->name() == monitorName) {
            auto *output = reinterpret_cast<wl_output *>(
                native->nativeResourceForScreen("output", screen));

            if (!output) {
                qCWarning(DdeWSPTreelandMonitor) << "wl_output is null for" << monitorName;
                break;
            }

            qCWarning(DdeWSPTreelandMonitor) << "calling get_treeland_wallpaper for" << monitorName;
            auto *wallpaperObj = m_wallpaperManager->get_treeland_wallpaper(output, nullptr);
            if (wallpaperObj) {
                auto *ctx = new WallpaperContext(wallpaperObj);
                m_wallpaperContexts.insert(monitorName, ctx);
                qCWarning(DdeWSPTreelandMonitor) << "wallpaper context created for" << monitorName;

                connect(ctx, &WallpaperContext::wallpaperChanged, this,
                    [this, monitorName](const QString &fileSource) {
                        qCWarning(DdeWSPTreelandMonitor) << "wallpaper changed for" << monitorName << ":" << fileSource;
                        m_currentWallpapers[monitorName] = fileSource;
                        Q_EMIT wallpaperChanged(monitorName, fileSource);
                    });

                return ctx;
            } else {
                qCWarning(DdeWSPTreelandMonitor) << "get_treeland_wallpaper returned null for" << monitorName;
            }
            break;
        }
    }

    qCWarning(DdeWSPTreelandMonitor) << "failed to create wallpaper context for" << monitorName;
    return nullptr;
}

#include "treelandwallpapermonitor.moc"

#endif // Enable_Treeland
