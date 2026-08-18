#pragma once

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>

class ExportRangeMenuReceiver : public QObject {
    Q_OBJECT

public:
    explicit ExportRangeMenuReceiver(
            QObject* controller,
            QObject* parent = nullptr)
        : QObject(parent), controller_(controller) {}

signals:
    void exportRangeRequested(QObject* controller);

public slots:
    void activate() {
        if (controller_)
            emit exportRangeRequested(controller_.data());
    }

private:
    QPointer<QObject> controller_;
};

// One receiver is parented to each native choice row. The selection pointer
// belongs to the nested QMenu::exec() frame and therefore outlives every row.
class ExportRangeChoiceReceiver : public QObject {
    Q_OBJECT

public:
    ExportRangeChoiceReceiver(
            int choice,
            int* selection,
            QObject* parent = nullptr)
        : QObject(parent), choice_(choice), selection_(selection) {}

public slots:
    void activate() {
        if (selection_)
            *selection_ = choice_;
    }

private:
    int choice_;
    int* selection_;
};

class ExportRangeCleanupReceiver : public QObject {
    Q_OBJECT

public:
    ExportRangeCleanupReceiver(
            QString const& path,
            QObject* parent = nullptr)
        : QObject(parent), path_(path) {}

public slots:
    void cleanup() {
        if (path_.isEmpty())
            return;
        QFile::remove(path_);
        path_.clear();
    }

private:
    QString path_;
};
