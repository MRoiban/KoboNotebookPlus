#pragma once

#include <QMap>
#include <QString>
#include <QVector>

#include "templates.h"

namespace cnt {
namespace covers {

void loadAutomaticCovers(
    QMap<QString, QString>& rendererMap,
    QVector<templates::CustomTemplate>& customCovers);

} // namespace covers
} // namespace cnt
