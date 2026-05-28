// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef BACKGROUNDPRIVATE_H
#define BACKGROUNDPRIVATE_H
#include <QDir>
#include <QList>
#include <QDebug>
#include <QDateTime>
#include <QMutex>
#include <QVector>

#include <qvector.h>

class Backgrounds: public QObject
{
    Q_OBJECT
public:

    enum BackgroundType {
        BT_Solid = 0,
        BT_Custom,
        BT_Sys,
        BT_Live,
        BT_All
    };

    static Backgrounds* instance(QObject *parent = nullptr);

    ~Backgrounds();

    void refreshBackground();
    void clear();
    void sortByTime(QFileInfoList listFileInfo);
    QStringList getCustomBgFilesInDir(QString dir);
    QStringList getBgFilesInDir(QString dir);
    bool isFileInDirs(QString file, QStringList dirs);
    bool isBackgroundFile(QString file);
    QStringList listBackground();
    QStringList getBackground(BackgroundType type);

    static BackgroundType getBackgroundType(QString id);

    bool isLiveWallpaperFile(QString file);
    QStringList getLiveBgFilesInDir(QString dir);

private:
    void init();
    QStringList getSysBgFIles();
    QStringList getCustomBgFiles();
    QStringList getLiveBgFiles();
    Backgrounds(QObject *parent = nullptr);

private:
    QStringList backgrounds;
    QStringList solidBackgrounds;
    QStringList customBackgrounds;
    QStringList sysBackgrounds;
    QStringList liveBackgrounds;

    static QStringList systemWallpapersDir;
    static QStringList liveWallpapersDir;
    static QStringList uiSupportedFormats;
};

#endif // BACKGROUNDPRIVATE_H
