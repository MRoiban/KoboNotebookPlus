#pragma once

#include <QMap>
#include <QString>
#include <QVector>

#include <cstdint>

namespace cnt {
namespace templates {

struct CustomTemplate {
    QString id;
    QString label;
    QString icon;
    QString backgroundBase;
};

struct TemplateRuntimeState {
    QVector<CustomTemplate> customTemplates;
};

bool locateRendererMap(
    void* backgroundOptionsOriginal,
    QMap<QString, QString>*& rendererMap,
    uintptr_t& iinknoteBase);
bool loadManifest(
    QMap<QString, QString>& rendererMap,
    TemplateRuntimeState& state);
void loadAutomaticTemplates(
    QMap<QString, QString>& rendererMap,
    TemplateRuntimeState& state);

} // namespace templates
} // namespace cnt
