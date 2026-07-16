#include "covers.h"

#include "fs_util.h"
#include "settings.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QtGlobal>

#include <NickelHook.h>

#include <cstdint>

namespace cnt {
namespace covers {
namespace {

char const kCoverRoot[] = "/mnt/onboard/.kobo/custom/covers/";
int const kMaximumCustomCovers = 32;
int const kBackgroundWidth = 1404;
int const kBackgroundHeight = 1872;

} // namespace

void loadAutomaticCovers(
        QMap<QString, QString>& rendererMap,
        QVector<templates::CustomTemplate>& customCovers) {
    QDir directory(QString::fromLatin1(kCoverRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (int i = 0; i < files.size(); ++i) {
        if (customCovers.size() >= kMaximumCustomCovers) {
            trace("covers: cover limit reached");
            break;
        }

        QFileInfo const& info = files.at(i);
        if (!fs_util::automaticSource(info))
            continue;

        QString const sourcePath = info.absoluteFilePath();
        if (!fs_util::hasPngSignature(sourcePath)) {
            trace("covers: invalid PNG skipped");
            continue;
        }

        QString label = info.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        label.replace(QLatin1Char('-'), QLatin1Char(' '));
        label = label.simplified().left(64);
        if (label.isEmpty())
            continue;

        uint32_t const hash =
            fs_util::stableFilenameHash(info.fileName().toUtf8());
        QString const id = QString::fromLatin1("Custom_Cover_%1")
            .arg(hash, 8, 16, QLatin1Char('0'));
        if (rendererMap.contains(id)) {
            trace("covers: identifier collision skipped");
            continue;
        }

        QString iconPath;
        if (!fs_util::createPickerIcon(sourcePath, &iconPath)) {
            trace("covers: could not generate picker icon");
            nh_log("cover '%s' must be a readable %d x %d PNG",
                qPrintable(info.fileName()), kBackgroundWidth, kBackgroundHeight);
            continue;
        }

        QString condorPath;
        if (!fs_util::syncCondorVariant(sourcePath, &condorPath)) {
            trace("covers: could not prepare renderer copy");
            continue;
        }

        rendererMap.insert(id, sourcePath);
        customCovers.append(
            templates::CustomTemplate{id, label, iconPath, sourcePath});
        trace("covers: PNG cover loaded");
        nh_log("automatically loaded notebook cover '%s' as '%s'",
            qPrintable(info.fileName()), qPrintable(label));
    }
}

} // namespace covers
} // namespace cnt
