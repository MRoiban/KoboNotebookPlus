#pragma once

class QObject;

namespace cnt {
namespace notebook_widget {

bool isNotebookWidget(QObject* object);
QObject* findNotebookWidget(QObject* controller);
void* notePadEditor(void* widget);
void* notePadEditorControl(void* widget);
void* notePadBackgroundWidget(void* widget);

} // namespace notebook_widget
} // namespace cnt
