#include "notebook_widget.h"

#include <QObject>

#include <cstring>

namespace cnt {
namespace notebook_widget {
namespace {

// Verified IInkNotePadWidget offsets on firmware 4.38.23697: the guarded
// MyScript Editor object/control identity is mirrored at 0x3c/0x40, and
// BackgroundWidget is at 0x90. The plugin does not manufacture ownership from
// these raw words; live firmware owners remain in scope for every call.
enum NotePadWidgetOffset {
    EditorOffset = 0x3c,
    EditorControlOffset = 0x40,
    BackgroundWidgetOffset = 0x90,
};

} // namespace

bool isNotebookWidget(QObject* object) {
    return object
        && object->metaObject()
        && strcmp(object->metaObject()->className(), "IInkNotePadWidget") == 0;
}

QObject* findNotebookWidget(QObject* controller) {
    QObject* root = controller;
    for (QObject* current = controller; current; current = current->parent()) {
        if (isNotebookWidget(current))
            return current;
        root = current;
    }

    // The menu controller is normally parented directly to the note-pad
    // widget. Keep a guarded descendant fallback for this pinned firmware's
    // alternate ownership path.
    QObjectList const descendants = root->findChildren<QObject*>();
    for (int i = 0; i < descendants.size(); ++i) {
        if (isNotebookWidget(descendants.at(i)))
            return descendants.at(i);
    }
    return nullptr;
}

void* notePadEditor(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + EditorOffset);
}

void* notePadEditorControl(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + EditorControlOffset);
}

void* notePadBackgroundWidget(void* widget) {
    char* const bytes = static_cast<char*>(widget);
    return *reinterpret_cast<void**>(bytes + BackgroundWidgetOffset);
}

} // namespace notebook_widget
} // namespace cnt
