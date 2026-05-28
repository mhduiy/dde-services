// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "format.h"
#include <QMimeDatabase>

QMap<QString,QString> FormatPicture::typeMap{
    {"image/jpeg","jpeg"},
    {"image/bmp", "bmp"},
    {"image/png","png"},
    {"image/tiff","tiff"},
    {"image/gif","jpeg"}
};

QMap<QString,QString> FormatPicture::videoTypeMap{
    {"video/mp4", "mp4"},
    {"video/webm", "webm"},
    {"video/x-matroska", "mkv"},
    {"video/ogg", "ogg"},
    {"video/x-msvideo", "avi"},
    {"video/quicktime", "mov"}
};

QString FormatPicture::getPictureType(QString file)
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(file);
    for(auto iter : typeMap.keys())
    {
        if(mime.name().startsWith(iter))
        {
            return typeMap[iter];
        }
    }

    return "";
}

bool FormatPicture::isVideoFile(QString file)
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(file);
    for(auto iter : videoTypeMap.keys())
    {
        if(mime.name().startsWith(iter))
        {
            return true;
        }
    }

    return false;
}
