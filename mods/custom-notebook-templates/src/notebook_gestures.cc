#include "notebook_gestures.h"

#include "firmware_api.h"
#include "notebook_widget.h"
#include "passive_input_router.h"
#include "plugin_runtime.h"
#include "settings.h"
#include "three_finger_swipe.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cstdint>

namespace cnt {
namespace notebook_gestures {
namespace {

enum NotebookGestureActionId {
    NoNotebookGestureAction = 0,
    UndoNotebookGestureAction = 1,
    RedoNotebookGestureAction = 2,
};

double notebookSpan(QWidget const* notebook) {
    if (!notebook)
        return 1404.0;
    int const width = notebook->width();
    int const height = notebook->height();
    int const span = std::min(width, height);
    return span > 0 ? static_cast<double>(span) : 1404.0;
}

class NotebookGestureRouter final
    : public passive_input::PassiveInputRouter {
public:
    NotebookGestureRouter(
            FirmwareApi& api,
            RuntimeState& state,
            QObject* parent)
        : PassiveInputRouter(parent),
          api_(api),
          state_(state),
          horizontalThreeFingerSwipe_(
              threeFingerSwipeConfigForSpan(1404.0),
              passive_input::GestureAction{
                  static_cast<std::uint32_t>(
                      UndoNotebookGestureAction) },
              passive_input::GestureAction{
                  static_cast<std::uint32_t>(
                      RedoNotebookGestureAction) }) {
        gestures().registerRecognizer(horizontalThreeFingerSwipe_);
    }

    void setNotebook(QWidget* notebook) {
        // renderVolume can replace the editor/control behind the same widget.
        // Treat every completed render as a new lifecycle epoch.
        rebindTarget(notebook);
        state_.activeNotebook = notebook;
    }

protected:
    void prepareTouchSequence(QWidget* notebook) override {
        ThreeFingerSwipeConfig const config =
            threeFingerSwipeConfigForSpan(notebookSpan(notebook));
        horizontalThreeFingerSwipe_.setConfig(config);
    }

    void dispatchAction(
            QWidget* notebook,
            passive_input::GestureAction const& action) override {
        if (!state_.actionsReady
                || !notebook
                || notebook != state_.activeNotebook.data()
                || !notebook->isVisible()
                || !notebook->isEnabled()
                || !notebook_widget::isNotebookWidget(notebook)
                || !notebook_widget::notePadEditor(notebook)
                || !notebook_widget::notePadEditorControl(notebook)) {
            return;
        }

        try {
            QElapsedTimer actionTimer;
            actionTimer.start();
            if (action.id == static_cast<std::uint32_t>(
                    UndoNotebookGestureAction)
                    && api_.notePadUndo) {
                trace("notebook-gesture: undo dispatch begin");
                api_.notePadUndo(notebook);
                trace(QLatin1String(
                    "notebook-gesture: undo dispatch complete ms=")
                    + QString::number(actionTimer.elapsed()));
            } else if (action.id == static_cast<std::uint32_t>(
                    RedoNotebookGestureAction)
                    && api_.notePadRedo) {
                trace("notebook-gesture: redo dispatch begin");
                api_.notePadRedo(notebook);
                trace(QLatin1String(
                    "notebook-gesture: redo dispatch complete ms=")
                    + QString::number(actionTimer.elapsed()));
            }
        } catch (...) {
            trace("notebook-gesture: action threw");
        }
    }

private:
    FirmwareApi& api_;
    RuntimeState& state_;
    ThreeFingerSwipeRecognizer horizontalThreeFingerSwipe_;
};

} // namespace

void invalidateNotebookWidget(
        RuntimeState& state,
        QWidget*) {
    NotebookGestureRouter* const router =
        static_cast<NotebookGestureRouter*>(state.observer.data());
    if (router)
        router->setNotebook(nullptr);
    else
        state.activeNotebook.clear();
}

void observeNotebookWidget(
        FirmwareApi& api,
        RuntimeState& state,
        QWidget* widget) {
    if (!state.actionsReady
            || !widget
            || !notebook_widget::isNotebookWidget(widget)) {
        return;
    }

    QCoreApplication* const application = QCoreApplication::instance();
    if (!application)
        return;

    if (!state.observer) {
        NotebookGestureRouter* const router =
            new NotebookGestureRouter(api, state, application);
        application->installEventFilter(router);
        state.observer = router;
        trace("notebook-gesture: passive input router installed");
    }

    NotebookGestureRouter* const router =
        static_cast<NotebookGestureRouter*>(state.observer.data());
    router->setNotebook(widget);
}

} // namespace notebook_gestures
} // namespace cnt
