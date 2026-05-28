// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QMap>
#include <QScopedPointer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(DdeWSPTreelandMonitor)

#ifdef Enable_Treeland

class WallpaperManager;
class WallpaperContext;

class TreelandWallpaperMonitor : public QObject
{
    Q_OBJECT
public:
    explicit TreelandWallpaperMonitor(QObject *parent = nullptr);
    ~TreelandWallpaperMonitor();

    void init();
    bool isActive() const;
    QString getCurrentWallpaper(const QString &monitorName) const;

Q_SIGNALS:
    void wallpaperChanged(const QString &monitorName, const QString &url);
    void ready();

private:
    WallpaperContext *getOrCreateWallpaperContext(const QString &monitorName);

    QScopedPointer<WallpaperManager> m_wallpaperManager;
    QMap<QString, WallpaperContext *> m_wallpaperContexts;
    QMap<QString, QString> m_currentWallpapers;
};

#endif // Enable_Treeland
