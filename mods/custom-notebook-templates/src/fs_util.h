#pragma once

class QByteArray;
class QElapsedTimer;
class QString;

namespace cnt {
namespace fs_util {

double elapsedMs(QElapsedTimer const& timer);
bool safeId(QString const& id);
bool safeTemplatePath(QString const& path);
bool hasPngSignature(QString const& path);
bool syncCondorVariant(QString const& sourcePath, QString* condorPath);
bool writeBytesIfChanged(QString const& destination, QByteArray const& contents);
bool createPickerIcon(QString const& sourcePath, QString* iconPath);

} // namespace fs_util
} // namespace cnt
