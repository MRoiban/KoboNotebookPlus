#pragma once

#include "settings.h"

#include <QObject>
#include <QPointer>

class QWidget;

class NotebookSleepSettingsReceiver : public QObject {
    Q_OBJECT

public:
    NotebookSleepSettingsReceiver(
        cnt::SettingsStore* settings,
        QWidget* selectorRow,
        cnt::NotebookSleepSettings const& initial,
        QObject* parent = nullptr);

public slots:
    void enabledChanged(bool enabled);
    void modeChanged(int index);

private:
    void persist();

    cnt::SettingsStore* settings_;
    QPointer<QWidget> selectorRow_;
    bool enabled_;
    cnt::NotebookSleepImageMode mode_;
};
