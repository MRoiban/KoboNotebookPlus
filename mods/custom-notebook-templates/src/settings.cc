#include "settings.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutexLocker>
#include <QSaveFile>
#include <QString>

#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {

char const kTrace[] =
    "/mnt/onboard/.kobo/custom/templates/plugin-status.txt";
char const kTemplateRoot[] = "/mnt/onboard/.kobo/custom/templates/";
char const kEraserSizeSettings[] =
    "/mnt/onboard/.kobo/custom/templates/eraser-size.json";

} // namespace

namespace cnt {

void trace(char const* message, bool truncate) {
    int const flags = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND);
    int const fd = open(kTrace, flags, 0644);
    if (fd < 0)
        return;
    write(fd, message, strlen(message));
    write(fd, "\n", 1);
    fsync(fd);
    close(fd);
}

void trace(QString const& message) {
    QByteArray const utf8 = message.left(1024).toUtf8();
    trace(utf8.constData());
}

bool validEraserSizeIndex(int index) {
    return index >= 0 && index < 5;
}

void SettingsStore::rememberEraserSizeIndex(int index) {
    if (!validEraserSizeIndex(index))
        return;
    QMutexLocker locker(&mutex_);
    sizeIndex_ = index;
    loaded_ = true;
}

int SettingsStore::configuredEraserSizeIndex() {
    {
        QMutexLocker locker(&mutex_);
        if (loaded_)
            return sizeIndex_;
    }

    int loadedIndex = 2;
    bool validFile = false;
    QString const settingsPath = QLatin1String(kEraserSizeSettings);
    QFile file(settingsPath);
    QFileInfo const info(file);
    if (!info.exists()) {
        trace("eraser-size: no saved setting; using stock-compatible middle size");
    } else if (!info.isFile() || info.size() < 1 || info.size() > 4096) {
        trace("eraser-size: saved setting has invalid size; preserving file and using default");
    } else if (!file.open(QIODevice::ReadOnly)) {
        trace("eraser-size: saved setting unreadable; preserving file and using default");
    } else {
        QJsonParseError parseError;
        QJsonDocument const document = QJsonDocument::fromJson(
            file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError
                && document.isObject()) {
            QJsonValue const value = document.object().value(
                QLatin1String("index"));
            double const number = value.toDouble(-1.0);
            int const candidate = static_cast<int>(number);
            if (number == static_cast<double>(candidate)
                    && validEraserSizeIndex(candidate)) {
                loadedIndex = candidate;
                validFile = true;
            }
        }
        if (!validFile) {
            trace("eraser-size: saved setting malformed; preserving file and using default");
        }
    }

    {
        QMutexLocker locker(&mutex_);
        if (!loaded_) {
            sizeIndex_ = loadedIndex;
            loaded_ = true;
        }
        loadedIndex = sizeIndex_;
    }
    trace(QLatin1String("eraser-size: configured index=")
        + QString::number(loadedIndex)
        + (validFile
            ? QLatin1String(" restored") : QLatin1String(" default")));
    return loadedIndex;
}

bool SettingsStore::persistEraserSizeIndex(int index) {
    if (!validEraserSizeIndex(index))
        return false;
    rememberEraserSizeIndex(index);

    QDir root;
    if (!root.mkpath(QLatin1String(kTemplateRoot))) {
        trace("eraser-size: settings directory unavailable; selection kept in memory");
        return false;
    }
    QString const settingsPath = QLatin1String(kEraserSizeSettings);
    QSaveFile file(settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        trace("eraser-size: setting could not be opened; selection kept in memory");
        return false;
    }
    QJsonObject rootObject;
    rootObject.insert(QLatin1String("index"), index);
    QByteArray bytes = QJsonDocument(rootObject).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        trace("eraser-size: setting commit failed; selection kept in memory");
        return false;
    }
    trace(QLatin1String("eraser-size: selected index persisted=")
        + QString::number(index));
    return true;
}

} // namespace cnt

__attribute__((constructor(101))) static void traceLibraryLoad() {
    cnt::trace("plugin library loaded", true);
}
