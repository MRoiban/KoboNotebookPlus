#pragma once

#include "layers.h"

class QObject;
class QString;
struct FirmwareApi;

namespace cnt {
namespace cover_cache {
struct State;
}

namespace layers_state {

struct LayoutStorage {
    // atk::core::Layout contains a shared document-layout pointer followed by
    // an atk::core::Page. The pinned destructor accesses the Page at +0x8.
    // Keep generous aligned storage so firmware owns construction/destruction.
    alignas(8) unsigned char bytes[128];
    bool constructed;
    FirmwareApi* firmware;

    explicit LayoutStorage(FirmwareApi& firmwareApi);
    ~LayoutStorage();

private:
    LayoutStorage(LayoutStorage const&);
    LayoutStorage& operator=(LayoutStorage const&);
};

QString nativeDocumentLayerId(FirmwareApi& firmware);
bool loadLayoutForPart(
    FirmwareApi& firmware,
    SharedPart const& part,
    LayoutStorage* layout,
    QString* error);
bool nativeLayerExists(
    FirmwareApi& firmware,
    LayoutStorage const& layout,
    QString const& id,
    bool* exists,
    QString* error);
void traceSerializedLayerProbe(
    cover_cache::State& coverState,
    layers::LayerState const& state,
    QString const& id,
    char const* phase);
bool saveLayerState(layers::LayerState const& state, QString* error);
bool loadLayerContext(
    FirmwareApi& firmware,
    layers::RuntimeState const& runtime,
    QObject* controller,
    int maximumNotebookLayers,
    layers::LayerContext* context,
    QString* error);

} // namespace layers_state
} // namespace cnt
