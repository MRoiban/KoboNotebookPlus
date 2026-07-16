#include <QAction>
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QPixmap>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <map>
#include <new>
#include <string>
#include <stdexcept>
#include <vector>
#include <sys/statvfs.h>
#include <unistd.h>

#include <NickelHook.h>

#include "covermenureceiver.h"
#include "fs_util.h"
#include "settings.h"

using cnt::trace;
using cnt::validEraserSizeIndex;

namespace {

#include "abi_types.h"
#include "firmware_api.h"
#include "plugin_state.h"
#include "parts/resolve.cc.inc"
#include "parts/templates.cc.inc"
#include "parts/covers.cc.inc"
#include "parts/cover_cache.cc.inc"
#include "parts/pages.cc.inc"
#include "parts/layers_state.cc.inc"
#include "parts/layers_eraser.cc.inc"
#include "parts/layers_service.cc.inc"
#include "parts/layers_preview.cc.inc"
#include "parts/menus.cc.inc"
#include "parts/visibility.cc.inc"
#include "parts/eraser_menu.cc.inc"
#include "parts/hook_install.cc.inc"

} // namespace

#include "parts/hooks.cc.inc"
