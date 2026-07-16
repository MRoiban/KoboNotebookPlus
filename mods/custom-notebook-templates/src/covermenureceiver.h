#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class CoverMenuReceiver : public QObject {
    Q_OBJECT

public:
    explicit CoverMenuReceiver(QObject* controller, QObject* parent = nullptr)
        : QObject(parent), controller_(controller) {}

    QObject* controller() const { return controller_.data(); }

signals:
    void coverRequested(QObject* controller);
    void duplicatePageRequested(QObject* controller);
    void movePageEarlierRequested(QObject* controller);
    void movePageLaterRequested(QObject* controller);
    void layersRequested(QObject* controller);

public slots:
    void activate() {
        if (controller_)
            emit coverRequested(controller_.data());
    }

    void activateDuplicatePage() {
        if (controller_)
            emit duplicatePageRequested(controller_.data());
    }

    void activateMovePageEarlier() {
        if (controller_)
            emit movePageEarlierRequested(controller_.data());
    }

    void activateMovePageLater() {
        if (controller_)
            emit movePageLaterRequested(controller_.data());
    }

    void activateLayers() {
        if (controller_)
            emit layersRequested(controller_.data());
    }

private:
    QPointer<QObject> controller_;
};

class LayerMenuReceiver : public QObject {
    Q_OBJECT

public:
    explicit LayerMenuReceiver(
        QObject* controller,
        QString const& layerId = QString(),
        QObject* parent = nullptr)
        : QObject(parent), controller_(controller), layerId_(layerId) {}

signals:
    void activateRequested(QObject* controller, QString const& layerId);
    void addRequested(QObject* controller);
    void deleteRequested(QObject* controller);
    void refreshRequested(QObject* controller);

public slots:
    void activateLayer() {
        if (controller_ && !layerId_.isEmpty())
            emit activateRequested(controller_.data(), layerId_);
    }

    void addLayer() {
        if (controller_)
            emit addRequested(controller_.data());
    }

    void deleteLayer() {
        if (controller_)
            emit deleteRequested(controller_.data());
    }

    void refreshPreviews() {
        if (controller_)
            emit refreshRequested(controller_.data());
    }

private:
    QPointer<QObject> controller_;
    QString layerId_;
};
