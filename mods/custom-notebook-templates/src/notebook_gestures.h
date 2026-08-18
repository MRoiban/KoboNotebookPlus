#pragma once

#include <QPointer>

class QObject;
class QWidget;
struct FirmwareApi;

namespace cnt {
namespace notebook_gestures {

struct RuntimeState {
    QPointer<QWidget> activeNotebook;
    QPointer<QObject> observer;
    bool actionsReady = false;
};

void invalidateNotebookWidget(
    RuntimeState& state,
    QWidget* widget);
void observeNotebookWidget(
    FirmwareApi& api,
    RuntimeState& state,
    QWidget* widget);

} // namespace notebook_gestures
} // namespace cnt
