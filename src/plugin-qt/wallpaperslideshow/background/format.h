// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef FORMAT_H
#define FORMAT_H

#include <QObject>
#include <QFile>
#include <QMap>

class FormatPicture{

public:
    static QString getPictureType(QString file);
    static bool isVideoFile(QString file);

private:
    static QMap<QString,QString> typeMap;
    static QMap<QString,QString> videoTypeMap;
};


#endif // FORMAT_H
