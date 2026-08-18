#pragma once

#include "abi_types.h"

#include <QPointer>
#include <QString>
#include <QtGlobal>

class QMenu;
class QObject;
class QPixmap;
class QTimer;
class QWidget;
struct FirmwareApi;

namespace cnt {
namespace export_range {

enum { ExporterVtableWords = 15 };

enum ExportQuality {
    BestQuality,
    FastQuality,
};

struct RuntimeState {
    bool hooksReady;
    ExportQuality quality;
    QPointer<QObject> namingWidget;
    QString namingSourcePath;
    QPointer<QObject> widget;
    QString sourcePath;
    int startPage;
    int endPage;
    int pageCount;
    quint64 generation;
    QPointer<QTimer> expiration;
    void* exporterVtable[ExporterVtableWords];
    bool exporterVtableReady;

    RuntimeState()
        : hooksReady(false), quality(BestQuality),
          startPage(0), endPage(0), pageCount(0),
          generation(0), exporterVtableReady(false) {
        for (int i = 0; i < ExporterVtableWords; ++i)
            exporterVtable[i] = nullptr;
    }
};

void cleanupStaleFiles(char const* root, char const* qualityPath);
void clearPending(RuntimeState& state);
void noteExportMenuOpened(
    FirmwareApi& firmware,
    RuntimeState& state,
    QObject* controllerObject);

// Opens the same bounded, hierarchical native page picker used by Export
// range. Page numbers are one-based and inclusive. A false result with an
// empty error means the user cancelled the picker.
bool choosePage(
    FirmwareApi& firmware,
    QObject* controller,
    QWidget* anchor,
    QString const& label,
    int first,
    int last,
    int* selectedPage,
    QString* error);

void promptForQuality(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* controller,
    QObject* controllerObject);

bool prepareExporterVtable(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* stockVtable,
    IInkNotePadExporterExportVolume replacement);

void constructExporter(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* exporter);

void addMenuContribution(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* controller,
    QObject* controllerObject,
    QMenu* menu,
    QPixmap const& noIcon,
    int maximumPageCount);

void exportVolume(
    FirmwareApi& firmware,
    RuntimeState& state,
    void* exporter,
    void const* volume,
    QString const& destination,
    bool fullNotebook,
    int partIndex,
    char const* temporaryRoot,
    char const* qualityPath);

} // namespace export_range
} // namespace cnt
