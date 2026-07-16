#include "abi_types.h"
#include "cover_cache.h"
#include "firmware_api.h"

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
#include "covers.h"
#include "fs_util.h"
#include "notebook_widget.h"
#include "page_io.h"
#include "pages.h"
#include "settings.h"
#include "templates.h"
#include "visibility.h"

using cnt::trace;
using cnt::validEraserSizeIndex;

namespace {

#include "plugin_state.h"
#include "parts/resolve.cc.inc"
#include "parts/firmware_resolve.cc.inc"
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
