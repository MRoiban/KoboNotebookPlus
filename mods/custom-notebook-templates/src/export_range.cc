#include "export_range.h"

#include "export_naming.h"
#include "exportrangereceiver.h"
#include "firmware_api.h"
#include "notebook_widget.h"
#include "pages.h"
#include "settings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QPixmap>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>

namespace cnt {
namespace export_range {
namespace {

int const kPickerChoiceCount = 10;
int const kNativeMenuBytes = 0x90;
uintptr_t const kControllerViewOffset = 0x10;

struct ScopedIInkString {
    IInkStringStorage value;

    ScopedIInkString(FirmwareApi& firmware, QByteArray const& utf8) {
        value.impl = nullptr;
        firmware.iinkStringCtor(&value, utf8.constData());
    }

    ~ScopedIInkString() {
        delete value.impl;
    }
};

struct PageInterval {
    int first;
    int last;
};

class ControllerMenuViewGuard {
public:
    ControllerMenuViewGuard(QObject* controller, QMenu* menu)
        : controller_(controller), original_(), view_(nullptr), active_(false) {
        static_assert(sizeof(QPointer<QObject>) == sizeof(void*) * 2,
            "unexpected Qt 5 QPointer ABI");
        if (!controller_ || !menu)
            return;
        view_ = reinterpret_cast<QPointer<QObject>*>(
            reinterpret_cast<char*>(controller) + kControllerViewOffset);
        original_ = *view_;
        *view_ = menu;
        active_ = true;
    }

    ~ControllerMenuViewGuard() {
        if (active_ && controller_ && view_)
            *view_ = original_;
    }

private:
    QPointer<QObject> controller_;
    QPointer<QObject> original_;
    QPointer<QObject>* view_;
    bool active_;
};

QVector<PageInterval> splitInterval(int first, int last) {
    QVector<PageInterval> result;
    int const count = last - first + 1;
    if (count <= 0)
        return result;
    int const groups = std::min(kPickerChoiceCount, count);
    int const groupSize = (count + groups - 1) / groups;
    for (int page = first; page <= last; page += groupSize) {
        PageInterval const interval = {
            page,
            std::min(last, page + groupSize - 1)
        };
        result.append(interval);
    }
    return result;
}

QString choiceText(QString const& label, PageInterval interval) {
    if (interval.first == interval.last) {
        return label + QLatin1String(": page ")
            + QString::number(interval.first);
    }
    return label + QLatin1String(": pages ")
        + QString::number(interval.first)
        + QChar(0x2013)
        + QString::number(interval.last);
}

QMenu* createNativePopup(
        FirmwareApi& firmware,
        QWidget* anchor,
        QString* error) {
    if (!anchor || !firmware.nickelTouchMenuConstructor
            || !firmware.nickelTouchMenuSetAlignment
            || !firmware.touchMenuSetCustomPopupPositionOffset
            || !firmware.nickelTouchMenuPopupPosition
            || !firmware.createIInkMenuItem
            || !firmware.addWidgetActionOriginal) {
        *error = QLatin1String(
            "Page selection unavailable: native menu API is missing.");
        return nullptr;
    }

    void* storage = nullptr;
    bool constructed = false;
    try {
        storage = ::operator new(kNativeMenuBytes);
        firmware.nickelTouchMenuConstructor(storage, anchor, 0);
        constructed = true;
        firmware.nickelTouchMenuSetAlignment(
            storage, Qt::AlignHCenter | Qt::AlignBottom);
        QPoint const offset(0, 14);
        firmware.touchMenuSetCustomPopupPositionOffset(storage, offset);
        QMenu* const popup = reinterpret_cast<QMenu*>(storage);
        popup->setAttribute(static_cast<Qt::WidgetAttribute>(55), true);
        return popup;
    } catch (...) {
        if (constructed)
            delete reinterpret_cast<QMenu*>(storage);
        else if (storage)
            ::operator delete(storage);
        *error = QLatin1String(
            "Page selection unavailable: native menu creation failed.");
        return nullptr;
    }
}

int showIntervalPopup(
        FirmwareApi& firmware,
        QObject* controller,
        QWidget* anchor,
        QString const& label,
        QVector<PageInterval> const& intervals,
        QString* error) {
    QMenu* const popup = createNativePopup(firmware, anchor, error);
    if (!popup)
        return -1;

    int selected = -1;
    QPixmap noIcon;
    for (int i = 0; i < intervals.size(); ++i) {
        QWidget* const row = firmware.createIInkMenuItem(
            controller,
            popup,
            choiceText(label, intervals.at(i)),
            noIcon,
            false);
        if (!row) {
            delete popup;
            *error = QLatin1String(
                "Page selection unavailable: a page-choice row could not be created.");
            return -1;
        }
        ExportRangeChoiceReceiver* const receiver =
            new ExportRangeChoiceReceiver(i, &selected, popup);
        firmware.addWidgetActionOriginal(
            controller,
            popup,
            row,
            receiver,
            SLOT(activate()),
            true,
            true,
            false);
    }

    popup->ensurePolished();
    QPoint const position = firmware.nickelTouchMenuPopupPosition(popup, anchor);
    ControllerMenuViewGuard const activeMenu(controller, popup);
    popup->exec(position);
    return selected;
}

int showChoicePopup(
        FirmwareApi& firmware,
        QObject* controller,
        QWidget* anchor,
        QStringList const& choices,
        QString* error) {
    QMenu* const popup = createNativePopup(firmware, anchor, error);
    if (!popup)
        return -1;

    int selected = -1;
    QPixmap noIcon;
    for (int i = 0; i < choices.size(); ++i) {
        QWidget* const row = firmware.createIInkMenuItem(
            controller,
            popup,
            choices.at(i),
            noIcon,
            false);
        if (!row) {
            delete popup;
            *error = QLatin1String(
                "Export quality unavailable: a choice row could not be created.");
            return -1;
        }
        ExportRangeChoiceReceiver* const receiver =
            new ExportRangeChoiceReceiver(i, &selected, popup);
        firmware.addWidgetActionOriginal(
            controller,
            popup,
            row,
            receiver,
            SLOT(activate()),
            true,
            true,
            false);
    }

    popup->ensurePolished();
    QPoint const position = firmware.nickelTouchMenuPopupPosition(popup, anchor);
    ControllerMenuViewGuard const activeMenu(controller, popup);
    popup->exec(position);
    return selected;
}

bool choosePageImpl(
        FirmwareApi& firmware,
        QObject* controller,
        QWidget* anchor,
        QString const& label,
        int first,
        int last,
        int* selectedPage,
        QString* error) {
    while (first < last) {
        QVector<PageInterval> const intervals = splitInterval(first, last);
        int const selected = showIntervalPopup(
            firmware, controller, anchor, label, intervals, error);
        if (selected < 0 || selected >= intervals.size())
            return false;
        first = intervals.at(selected).first;
        last = intervals.at(selected).last;
    }
    if (first < 1)
        return false;
    *selectedPage = first;
    return true;
}

void showError(FirmwareApi& firmware, QObject* widget, QString const& error) {
    trace(QLatin1String("export-range: ") + error);
    if (widget && firmware.showErrorPopup)
        firmware.showErrorPopup(widget, error);
}

bool writeQualityMarker(
        RuntimeState const& state,
        char const* qualityPath) {
    if (!qualityPath || !*qualityPath)
        return false;
    QString const path = QLatin1String(qualityPath);
    QFileInfo const marker(path);
    QDir directory;
    if (!directory.mkpath(marker.absolutePath()))
        return false;

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QByteArray const value = state.quality == FastQuality
        ? QByteArray("fast\n")
        : QByteArray("best\n");
    if (output.write(value) != value.size()) {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}

QString namingSourcePath(
        FirmwareApi& firmware,
        RuntimeState const& state,
        bool rangeExport) {
    if (rangeExport)
        return state.sourcePath;
    if (!state.namingWidget
            || state.namingSourcePath.isEmpty()
            || !firmware.widgetFilePath) {
        return QString();
    }
    QString const current = firmware.widgetFilePath(state.namingWidget.data());
    return current == state.namingSourcePath ? current : QString();
}

QString renamedExportDestination(
        QString const& originalDestination,
        QString const& sourcePath,
        int startPage,
        int endPage) {
    QByteArray const destinationUtf8 = originalDestination.toUtf8();
    if (!export_naming::canRewriteDestination(
            destinationUtf8.constData())) {
        return originalDestination;
    }

    QFileInfo const original(originalDestination);
    QString const notebookName = sourcePath.isEmpty()
        ? QString()
        : QFileInfo(sourcePath).completeBaseName();
    QByteArray const notebookUtf8 = notebookName.toUtf8();
    QByteArray const originalUtf8 = original.fileName().toUtf8();
    QByteArray const date =
        QDate::currentDate().toString(QLatin1String("ddMMyyyy")).toLatin1();
    export_naming::FileName const requested =
        export_naming::buildFileName(
            notebookUtf8.constData(),
            originalUtf8.constData(),
            date.constData(),
            startPage,
            endPage);
    if (requested.isEmpty())
        return originalDestination;

    QDir const directory(original.path());
    QString candidate = directory.filePath(QString::fromUtf8(
        requested.bytes, static_cast<int>(requested.length)));
    if (!QFileInfo(candidate).exists())
        return candidate;

    for (unsigned int index = 2; index <= 9999; ++index) {
        export_naming::FileName const numbered =
            export_naming::addCollisionSuffix(
                requested.bytes, index);
        if (numbered.isEmpty())
            return originalDestination;
        candidate = directory.filePath(QString::fromUtf8(
            numbered.bytes, static_cast<int>(numbered.length)));
        if (!QFileInfo(candidate).exists())
            return candidate;
    }
    return originalDestination;
}

void armPending(
        RuntimeState& state,
        QObject* widget,
        QString const& sourcePath,
        int startPage,
        int endPage,
        int pageCount) {
    clearPending(state);
    state.namingWidget = widget;
    state.namingSourcePath = sourcePath;
    state.widget = widget;
    state.sourcePath = sourcePath;
    state.startPage = startPage;
    state.endPage = endPage;
    state.pageCount = pageCount;
    quint64 const generation = state.generation;

    QTimer* const timer = new QTimer(widget);
    state.expiration = timer;
    timer->setSingleShot(true);
    timer->setInterval(5 * 60 * 1000);
    QObject::connect(timer, &QTimer::timeout, [&state, generation]() {
        if (state.generation == generation) {
            trace("export-range: pending request expired");
            clearPending(state);
        }
    });
    QObject::connect(widget, &QObject::destroyed, [&state, generation]() {
        if (state.generation == generation)
            clearPending(state);
    });
    timer->start();
}

QString uniqueTemporaryPath(char const* root) {
    QString const stamp = QDateTime::currentDateTimeUtc()
        .toString(QLatin1String("yyyyMMdd-HHmmsszzz"));
    QString const prefix = QLatin1String(root) + stamp
        + QLatin1Char('-')
        + QString::number(QCoreApplication::applicationPid());
    QString path = prefix + QLatin1String(".nebo");
    for (int suffix = 1; QFileInfo(path).exists(); ++suffix) {
        path = prefix + QLatin1Char('-') + QString::number(suffix)
            + QLatin1String(".nebo");
    }
    return path;
}

bool stageRangeNotebook(
        FirmwareApi& firmware,
        QObject* widget,
        QString const& sourcePath,
        int startPage,
        int endPage,
        int pageCount,
        char const* temporaryRoot,
        QString* temporaryPath,
        QString* error) {
    QFileInfo const source(sourcePath);
    if (!widget
            || sourcePath != firmware.widgetFilePath(widget)
            || !sourcePath.startsWith(QLatin1String("/mnt/onboard/"))
            || !sourcePath.endsWith(QLatin1String(".nebo"), Qt::CaseInsensitive)
            || !source.isFile()
            || source.isSymLink()
            || source.size() <= 0
            || pageCount < 1
            || startPage < 1
            || startPage > endPage
            || endPage > pageCount) {
        *error = QLatin1String(
            "Export range failed: the notebook or selected pages changed.");
        return false;
    }

    QDir root;
    if (!root.mkpath(QLatin1String(temporaryRoot))) {
        *error = QLatin1String(
            "Export range failed: the temporary folder could not be created.");
        return false;
    }

    QString const path = uniqueTemporaryPath(temporaryRoot);
    if (!QFile::copy(sourcePath, path)) {
        *error = QLatin1String(
            "Export range failed: the temporary notebook could not be created.");
        return false;
    }

    bool succeeded = false;
    try {
        void* const editor = notebook_widget::notePadEditor(widget);
        SharedEngine const engine = editor
            ? firmware.editorGetEngine(editor)
            : SharedEngine();
        ScopedIInkString packagePath(firmware, path.toUtf8());
        SharedPackage package = engine && packagePath.value.impl
            ? firmware.engineOpenPackage(
                engine.get(), packagePath.value, 0)
            : SharedPackage();
        if (!package || firmware.packagePartCount(package.get()) != pageCount) {
            *error = QLatin1String(
                "Export range failed: the temporary notebook could not be opened.");
        } else {
            int const first = startPage - 1;
            int const last = endPage - 1;
            for (int index = pageCount - 1; index > last; --index) {
                SharedPart const part = firmware.packageGetPart(
                    package.get(), index);
                if (!part)
                    throw std::runtime_error("missing trailing range part");
                firmware.packageRemovePart(package.get(), part);
            }
            for (int index = first - 1; index >= 0; --index) {
                SharedPart const part = firmware.packageGetPart(
                    package.get(), index);
                if (!part)
                    throw std::runtime_error("missing leading range part");
                firmware.packageRemovePart(package.get(), part);
            }

            int const expectedCount = endPage - startPage + 1;
            if (firmware.packagePartCount(package.get()) != expectedCount)
                throw std::runtime_error("range page count mismatch");
            firmware.packageSave(package.get());
            succeeded = firmware.packagePartCount(package.get()) == expectedCount;
            if (!succeeded) {
                *error = QLatin1String(
                    "Export range failed: the temporary page set was not saved.");
            }
        }
    } catch (std::exception const&) {
        *error = QLatin1String(
            "Export range failed: the notebook engine rejected the temporary copy.");
    } catch (...) {
        *error = QLatin1String(
            "Export range failed: unexpected notebook error.");
    }

    if (!succeeded) {
        QFile::remove(path);
        return false;
    }
    *temporaryPath = path;
    return true;
}

struct VolumeShim {
    void** vtable;
    QString url;
};

void shimVolumeUrl(QString* result, void const* volume) {
    VolumeShim const* const shim = static_cast<VolumeShim const*>(volume);
    new (result) QString(shim->url);
}

void callOriginalForTemporary(
        FirmwareApi& firmware,
        void* exporter,
        QString const& temporaryPath,
        QString const& destination) {
    // Firmware 4.38 exportVolume reads only Volume's virtual URL getter at
    // slot +0x1c before copying that QString into its own async state. Keep the
    // shim local and expose no unverified Volume surface.
    typedef void (*VolumeUrlGetter)(QString*, void const*);
    union VolumeUrlGetterAddress {
        VolumeUrlGetter function;
        void* pointer;
    } getter;
    getter.function = shimVolumeUrl;
    void* vtable[8] = {};
    vtable[7] = getter.pointer;
    VolumeShim const shim = {
        vtable,
        QLatin1String("file://") + temporaryPath
    };
    firmware.iInkNotePadExporterExportVolumeOriginal(
        exporter, &shim, destination, true, 0);
}

} // namespace

bool choosePage(
        FirmwareApi& firmware,
        QObject* controller,
        QWidget* anchor,
        QString const& label,
        int first,
        int last,
        int* selectedPage,
        QString* error) {
    if (!error)
        return false;
    error->clear();
    if (!controller || !anchor || !selectedPage
            || first < 1 || last < first) {
        *error = QLatin1String(
            "Page selection unavailable: page range is invalid.");
        return false;
    }
    return choosePageImpl(
        firmware,
        controller,
        anchor,
        label,
        first,
        last,
        selectedPage,
        error);
}

void cleanupStaleFiles(char const* root, char const* qualityPath) {
    if (qualityPath && *qualityPath)
        QFile::remove(QLatin1String(qualityPath));
    QDir directory{QLatin1String(root)};
    if (!directory.exists())
        return;
    QStringList const entries = directory.entryList(
        QStringList() << QLatin1String("*.nebo"),
        QDir::Files | QDir::NoSymLinks);
    for (QString const& entry : entries)
        directory.remove(entry);
}

void clearPending(RuntimeState& state) {
    ++state.generation;
    state.widget.clear();
    state.sourcePath.clear();
    state.startPage = 0;
    state.endPage = 0;
    state.pageCount = 0;
    if (state.expiration) {
        state.expiration->stop();
        state.expiration->deleteLater();
    }
    state.expiration.clear();
}

void noteExportMenuOpened(
        FirmwareApi& firmware,
        RuntimeState& state,
        QObject* controllerObject) {
    if (!state.sourcePath.isEmpty()) {
        trace("export-range: cancelled pending request cleared on menu reopen");
        clearPending(state);
    }
    state.namingWidget.clear();
    state.namingSourcePath.clear();
    QObject* const widget =
        notebook_widget::findNotebookWidget(controllerObject);
    if (!widget || !firmware.widgetFilePath)
        return;
    QString const path = firmware.widgetFilePath(widget);
    if (!path.startsWith(QLatin1String("/mnt/onboard/"))
            || !path.endsWith(
                QLatin1String(".nebo"), Qt::CaseInsensitive)) {
        return;
    }
    state.namingWidget = widget;
    state.namingSourcePath = path;
}

void promptForQuality(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* controller,
        QObject* controllerObject) {
    state.quality = BestQuality;
    if (!state.hooksReady || !controller || !controllerObject
            || !firmware.abstractNickelMenuControllerPopupFromWidget) {
        trace("export-quality: chooser unavailable; defaulting to best");
        return;
    }

    QWidget* const anchor =
        firmware.abstractNickelMenuControllerPopupFromWidget(controllerObject);
    if (!anchor) {
        trace("export-quality: popup anchor unavailable; defaulting to best");
        return;
    }

    QString error;
    QStringList choices;
    choices << QLatin1String("Fast") << QLatin1String("Best quality");
    int const selected = showChoicePopup(
        firmware,
        controllerObject,
        anchor,
        choices,
        &error);
    if (selected == 0)
        state.quality = FastQuality;
    else
        state.quality = BestQuality;

    if (selected < 0 && !error.isEmpty())
        trace(QLatin1String("export-quality: ") + error);
    trace(state.quality == FastQuality
        ? "export-quality: fast selected"
        : "export-quality: best selected");
}

bool prepareExporterVtable(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* stockVtable,
        IInkNotePadExporterExportVolume replacement) {
    state.exporterVtableReady = false;
    if (!stockVtable
            || !firmware.iInkNotePadExporterExportVolumeOriginal
            || !replacement) {
        return false;
    }

    memcpy(
        state.exporterVtable,
        stockVtable,
        sizeof(state.exporterVtable));
    // The Itanium ABI address point is vtable+8. exportVolume is virtual slot
    // +0x30, hence absolute word 14 in this exact 0x3c-byte firmware vtable.
    int const exportVolumeWord = 14;
    IInkNotePadExporterExportVolumeAddress original;
    original.pointer = state.exporterVtable[exportVolumeWord];
    if (original.function
            != firmware.iInkNotePadExporterExportVolumeOriginal) {
        trace("export-range: exporter vtable rejected unexpected export slot");
        return false;
    }

    IInkNotePadExporterExportVolumeAddress replacementAddress;
    replacementAddress.function = replacement;
    state.exporterVtable[exportVolumeWord] = replacementAddress.pointer;
    state.exporterVtableReady = true;
    trace("export-range: exporter vtable clone verified");
    return true;
}

void constructExporter(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* exporter) {
    if (!firmware.iInkNotePadExporterConstructorOriginal)
        return;
    firmware.iInkNotePadExporterConstructorOriginal(exporter);
    if (!exporter || !state.exporterVtableReady
            || !firmware.iInkNotePadExporterVtable) {
        return;
    }

    void* const stockAddressPoint = static_cast<char*>(
        firmware.iInkNotePadExporterVtable) + 8;
    void** const vptr = reinterpret_cast<void**>(exporter);
    if (*vptr != stockAddressPoint) {
        trace("export-range: constructed exporter had unexpected vptr");
        return;
    }
    *vptr = state.exporterVtable + 2;
    trace("export-range: exporter instance armed");
}

void addMenuContribution(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* controller,
        QObject* controllerObject,
        QMenu* menu,
        QPixmap const& noIcon,
        int maximumPageCount) {
    if (!state.hooksReady || !controller || !controllerObject || !menu)
        return;

    ExportRangeMenuReceiver* const receiver = new ExportRangeMenuReceiver(
        controllerObject, menu);
    FirmwareApi* const firmwarePointer = &firmware;
    RuntimeState* const statePointer = &state;
    QObject::connect(
        receiver,
        &ExportRangeMenuReceiver::exportRangeRequested,
        [firmwarePointer, statePointer, maximumPageCount](
                QObject* target) {
            QObject* const widget = notebook_widget::findNotebookWidget(target);
            if (!widget || widget->property("_cnt_export_range_active").toBool())
                return;
            widget->setProperty("_cnt_export_range_active", true);

            FirmwareApi& firmware = *firmwarePointer;
            pages::PageContext context;
            QString error;
            if (!pages::loadPageContext(
                    firmware,
                    widget,
                    maximumPageCount,
                    &context,
                    &error)) {
                widget->setProperty("_cnt_export_range_active", false);
                showError(firmware, widget, QLatin1String(
                    "Export range unavailable: current pages could not be verified."));
                return;
            }

            QWidget* const anchor =
                firmware.abstractNickelMenuControllerPopupFromWidget(target);
            int startPage = 0;
            int endPage = 0;
            bool const selected = anchor
                && choosePage(
                    firmware,
                    target,
                    anchor,
                    QLatin1String("Start"),
                    1,
                    context.count,
                    &startPage,
                    &error)
                && choosePage(
                    firmware,
                    target,
                    anchor,
                    QLatin1String("End"),
                    startPage,
                    context.count,
                    &endPage,
                    &error);
            widget->setProperty("_cnt_export_range_active", false);
            if (!selected) {
                if (!error.isEmpty())
                    showError(firmware, widget, error);
                else
                    trace("export-range: page selection cancelled");
                return;
            }

            QString const sourcePath = firmware.widgetFilePath(widget);
            armPending(
                *statePointer,
                widget,
                sourcePath,
                startPage,
                endPage,
                context.count);
            trace(QLatin1String("export-range: selected pages ")
                + QString::number(startPage)
                + QChar(0x2013)
                + QString::number(endPage));
            firmware.iInkExportMenuControllerExportNotebook(target, true);
        });

    QWidget* const item = firmware.createIInkMenuItem(
        controller,
        menu,
        QLatin1String("Export range"),
        noIcon,
        false);
    if (!item) {
        receiver->deleteLater();
        trace("export-range: native menu item creation failed");
        return;
    }
    firmware.addWidgetActionOriginal(
        controller,
        menu,
        item,
        receiver,
        SLOT(activate()),
        true,
        true,
        false);
    trace("export-range: native export-menu item added");
}

void exportVolume(
        FirmwareApi& firmware,
        RuntimeState& state,
        void* exporter,
        void const* volume,
        QString const& destination,
        bool fullNotebook,
        int partIndex,
        char const* temporaryRoot,
        char const* qualityPath) {
    if (!firmware.iInkNotePadExporterExportVolumeOriginal)
        return;

    bool const rangeExport = state.hooksReady
        && !state.sourcePath.isEmpty()
        && state.widget
        && fullNotebook;
    QString routedDestination = destination;
    try {
        routedDestination = renamedExportDestination(
            destination,
            namingSourcePath(firmware, state, rangeExport),
            rangeExport ? state.startPage : 0,
            rangeExport ? state.endPage : 0);
        trace(QLatin1String("export-name: ")
            + QFileInfo(routedDestination).fileName());
    } catch (...) {
        routedDestination = destination;
        trace("export-name: rename failed; stock destination preserved");
    }

    if (!rangeExport) {
        if (state.hooksReady && !writeQualityMarker(state, qualityPath))
            trace("export-quality: marker write failed; launcher will use stock best");
        // The pinned stock implementation copies the destination QString into
        // its asynchronous state before returning (BN 0x61400).
        firmware.iInkNotePadExporterExportVolumeOriginal(
            exporter, volume, routedDestination, fullNotebook, partIndex);
        return;
    }

    QPointer<QObject> const widget = state.widget;
    QString const sourcePath = state.sourcePath;
    int const startPage = state.startPage;
    int const endPage = state.endPage;
    int const pageCount = state.pageCount;
    clearPending(state);

    // IInkNotePadWidget::exportNotebook(bool) forwards the current page index
    // to IInkExportNoteController even when fullNotebook is true. A range may
    // therefore arrive with any valid partIndex; the explicit pending request
    // and fullNotebook flag are the routing authority here.
    trace(QLatin1String("export-range: intercepting stock full export at page index ")
        + QString::number(partIndex));

    QString temporaryPath;
    QString error;
    if (!stageRangeNotebook(
            firmware,
            widget,
            sourcePath,
            startPage,
            endPage,
            pageCount,
            temporaryRoot,
            &temporaryPath,
            &error)) {
        showError(firmware, widget, error);
        if (!QMetaObject::invokeMethod(
                reinterpret_cast<QObject*>(exporter),
                "failure",
                Qt::DirectConnection)) {
            trace("export-range: stock failure signal could not be invoked");
        }
        return;
    }

    QObject* const exporterObject = reinterpret_cast<QObject*>(exporter);
    ExportRangeCleanupReceiver* const cleanup =
        new ExportRangeCleanupReceiver(temporaryPath, exporterObject);
    QObject::connect(
        exporterObject, SIGNAL(success()), cleanup, SLOT(cleanup()));
    QObject::connect(
        exporterObject, SIGNAL(failure()), cleanup, SLOT(cleanup()));
    QObject::connect(
        exporterObject,
        SIGNAL(destroyed(QObject*)),
        cleanup,
        SLOT(cleanup()));

    trace(QLatin1String("export-range: temporary notebook ready with ")
        + QString::number(endPage - startPage + 1)
        + QLatin1String(" pages"));
    if (!writeQualityMarker(state, qualityPath))
        trace("export-quality: marker write failed; launcher will use stock best");
    callOriginalForTemporary(
        firmware, exporter, temporaryPath, routedDestination);
}

} // namespace export_range
} // namespace cnt
