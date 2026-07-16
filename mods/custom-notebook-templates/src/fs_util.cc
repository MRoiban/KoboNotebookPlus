#include "fs_util.h"

#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QPainter>
#include <QString>
#include <QtGlobal>

#include <cstring>

namespace {

char const kTemplateRoot[] = "/mnt/onboard/.kobo/custom/templates/";
char const kCondorSuffix[] = "_condor.png";
qint64 const kMaximumAutomaticPngSize = 32 * 1024 * 1024;
int const kBackgroundWidth = 1404;
int const kBackgroundHeight = 1872;
int const kPickerIconSize = 280;

bool filesEqual(QString const& firstPath, QString const& secondPath) {
    QFileInfo const firstInfo(firstPath);
    QFileInfo const secondInfo(secondPath);
    if (!firstInfo.isFile() || !secondInfo.isFile()
            || firstInfo.size() != secondInfo.size()) {
        return false;
    }

    QFile first(firstPath);
    QFile second(secondPath);
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;

    while (!first.atEnd()) {
        QByteArray const firstBlock = first.read(64 * 1024);
        QByteArray const secondBlock = second.read(64 * 1024);
        if (firstBlock != secondBlock)
            return false;
    }
    return second.atEnd();
}

} // namespace

namespace cnt {
namespace fs_util {

double elapsedMs(QElapsedTimer const& timer) {
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

uint32_t stableFilenameHash(QByteArray const& value) {
    uint32_t hash = UINT32_C(2166136261);
    for (int i = 0; i < value.size(); ++i) {
        hash ^= static_cast<unsigned char>(value.at(i));
        hash *= UINT32_C(16777619);
    }
    return hash;
}

bool automaticSource(QFileInfo const& info) {
    QString const name = info.fileName();
    QString const lower = name.toLower();
    return info.isFile()
        && info.size() >= 8
        && info.size() <= kMaximumAutomaticPngSize
        && name.endsWith(QLatin1String(".png"))
        && !lower.endsWith(QLatin1String(kCondorSuffix))
        && !lower.endsWith(QLatin1String("-icon.png"))
        && !lower.endsWith(QLatin1String("_icon.png"));
}

bool safeId(QString const& id) {
    if (!id.startsWith(QLatin1String("Custom_")) || id.size() > 63)
        return false;

    for (int i = 0; i < id.size(); ++i) {
        ushort const c = id.at(i).unicode();
        bool const asciiLetter =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        bool const asciiDigit = c >= '0' && c <= '9';
        if (!(asciiLetter || asciiDigit || c == '_'))
            return false;
    }
    return true;
}

bool safeTemplatePath(QString const& path) {
    return path.startsWith(QLatin1String(kTemplateRoot))
        && !path.contains(QLatin1String(".."))
        && QFileInfo(path).isFile();
}

bool hasPngSignature(QString const& path) {
    static unsigned char const signature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray const header = file.read(static_cast<qint64>(sizeof(signature)));
    return header.size() == static_cast<int>(sizeof(signature))
        && memcmp(header.constData(), signature, sizeof(signature)) == 0;
}

bool syncCondorVariant(QString const& sourcePath, QString* condorPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String(kCondorSuffix));
    *condorPath = destination;

    if (filesEqual(sourcePath, destination))
        return true;

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    if (!QFile::copy(sourcePath, temporary))
        return false;
    if (QFile::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        return false;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return false;
    }
    return true;
}

bool writeBytesIfChanged(QString const& destination, QByteArray const& contents) {
    QFile existing(destination);
    if (existing.open(QIODevice::ReadOnly)) {
        bool const unchanged = existing.readAll() == contents;
        existing.close();
        if (unchanged)
            return true;
    }

    QString const temporary = destination + QLatin1String(".tmp");
    QFile::remove(temporary);
    QFile output(temporary);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (output.write(contents) != contents.size()) {
        output.close();
        QFile::remove(temporary);
        return false;
    }
    output.close();

    if (QFile::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        return false;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return false;
    }
    return true;
}

bool createPickerIcon(QString const& sourcePath, QString* iconPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String("-icon.png"));
    *iconPath = destination;

    QImage page;
    if (!page.load(sourcePath, "PNG"))
        return false;
    if (page.width() != kBackgroundWidth || page.height() != kBackgroundHeight)
        return false;

    QImage const fitted = page.scaled(
        kPickerIconSize,
        kPickerIconSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QImage icon(kPickerIconSize, kPickerIconSize, QImage::Format_RGB32);
    icon.fill(qRgb(255, 255, 255));

    QPainter painter(&icon);
    painter.drawImage(
        (kPickerIconSize - fitted.width()) / 2,
        (kPickerIconSize - fitted.height()) / 2,
        fitted);
    painter.end();

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !icon.save(&buffer, "PNG"))
        return false;
    buffer.close();
    return writeBytesIfChanged(destination, encoded);
}

} // namespace fs_util
} // namespace cnt
