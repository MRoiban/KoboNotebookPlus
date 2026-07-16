#line 1458 "src/customnotebooktemplates.cc"

// Kobo composites the page template above the live pen layer: every built-in
// *_condor.png paper is transparent except for its line work, so fresh ink
// stays visible through the paper while writing. A fully opaque user PNG
// (white background baked in) hides new strokes until the next full page
// render. Recreate the built-in contract for automatic templates: a source
// with no real transparency is converted so white becomes transparent paper
// and darkness becomes black at matching opacity. Composited over the white
// page this is pixel-identical to the source. A source that already carries
// transparency is copied verbatim, exactly as before.
static bool syncTemplateOverlay(QString const& sourcePath, QString* condorPath) {
    QString destination = sourcePath;
    destination.chop(4);
    destination.append(QLatin1String(kCondorSuffix));
    *condorPath = destination;

    QImage page;
    if (!page.load(sourcePath, "PNG"))
        return false;
    if (page.width() != kBackgroundWidth || page.height() != kBackgroundHeight)
        return false;

    QImage overlay = page.convertToFormat(QImage::Format_ARGB32);
    if (overlay.isNull())
        return false;

    if (page.hasAlphaChannel()) {
        for (int y = 0; y < overlay.height(); ++y) {
            QRgb const* const row =
                reinterpret_cast<QRgb const*>(overlay.constScanLine(y));
            for (int x = 0; x < overlay.width(); ++x) {
                if (qAlpha(row[x]) != 255)
                    return syncCondorVariant(sourcePath, condorPath);
            }
        }
    }

    for (int y = 0; y < overlay.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < overlay.width(); ++x)
            row[x] = qRgba(0, 0, 0, 255 - qGray(row[x]));
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !overlay.save(&buffer, "PNG"))
        return false;
    buffer.close();
    if (!writeBytesIfChanged(destination, encoded))
        return false;
    trace("automatic: opaque paper converted to transparent overlay");
    return true;
}

static uint32_t stableFilenameHash(QByteArray const& value) {
    uint32_t hash = UINT32_C(2166136261);
    for (int i = 0; i < value.size(); ++i) {
        hash ^= static_cast<unsigned char>(value.at(i));
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool automaticSource(QFileInfo const& info) {
    QString const name = info.fileName();
    QString const lower = name.toLower();
    return info.isFile()
        && info.size() >= 8
        && info.size() <= kMaximumAutomaticPngSize
        && name.endsWith(QLatin1String(".png"))
        && !lower.endsWith(QLatin1String(kCondorSuffix))
        && !lower.endsWith(QLatin1String("-icon.png"))
        && !lower.endsWith(QLatin1String("_icon.png"));
}

static bool locateRendererMap() {
    void* const original = nh_symptr(backgroundOptionsOriginal);
    Dl_info image = {};

    if (!original || !dladdr(original, &image) || !image.dli_fbase) {
        trace("renderer map: could not locate libiinknote");
        nh_log("could not locate libiinknote image base");
        return false;
    }

    uintptr_t const base = reinterpret_cast<uintptr_t>(image.dli_fbase);
    uintptr_t const function = reinterpret_cast<uintptr_t>(original) & ~uintptr_t(1);
    if (function - base != kBackgroundOptionsVma) {
        trace("renderer map: backgroundOptions VMA mismatch");
        nh_log("unsupported libiinknote: backgroundOptions offset is 0x%lx, expected 0x%lx",
            static_cast<unsigned long>(function - base),
            static_cast<unsigned long>(kBackgroundOptionsVma));
        return false;
    }

    rendererMap = reinterpret_cast<QMap<QString, QString>*>(base + kRendererMapVma);
    if (rendererMap->size() != kExpectedBuiltinMapSize) {
        trace("renderer map: built-in count mismatch");
        nh_log("unsupported libiinknote: renderer map has %d entries, expected %d",
            rendererMap->size(), kExpectedBuiltinMapSize);
        rendererMap = nullptr;
        return false;
    }
    iinknoteBase = base;
    trace("renderer map: verified");
    return true;
}

static bool loadManifest() {
    QFile file;
    file.setFileName(QLatin1String(kManifest));
    if (!file.exists()) {
        trace("manifest: not found");
        nh_log("manifest not found at %s; no custom templates loaded", kManifest);
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        trace("manifest: open failed");
        nh_log("could not open manifest at %s", kManifest);
        return false;
    }

    QByteArray const raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument const document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        trace("manifest: invalid JSON");
        nh_log("invalid manifest JSON: %s", qPrintable(parseError.errorString()));
        return false;
    }

    QJsonArray const entries = document.object().value(QLatin1String("templates")).toArray();
    if (entries.size() > kMaximumCustomTemplates) {
        trace("manifest: too many entries");
        nh_log("manifest has %d entries; maximum is %d",
            entries.size(), kMaximumCustomTemplates);
        return false;
    }

    QVector<CustomTemplate> parsed;
    QMap<QString, bool> seen;
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject const item = entries.at(i).toObject();
        QString const id = item.value(QLatin1String("id")).toString();
        QString const label = item.value(QLatin1String("label")).toString();
        QString const icon = item.value(QLatin1String("icon")).toString();
        QString const background = item.value(QLatin1String("background")).toString();

        if (!safeId(id) || label.isEmpty() || label.size() > 64) {
            trace("manifest: invalid id or label");
            nh_log("manifest entry %d has an invalid id or label", i);
            return false;
        }
        if (seen.contains(id) || rendererMap->contains(id)) {
            trace("manifest: identifier collision");
            nh_log("manifest entry %d collides with template id '%s'", i, qPrintable(id));
            return false;
        }
        if (!safeTemplatePath(icon) || !icon.endsWith(QLatin1String(".png"))) {
            trace("manifest: invalid icon");
            nh_log("manifest entry %d has an invalid or missing icon", i);
            return false;
        }
        if (!safeTemplatePath(background) || !background.endsWith(QLatin1String(kCondorSuffix))) {
            trace("manifest: invalid background");
            nh_log("manifest entry %d needs an existing *_condor.png background", i);
            return false;
        }

        QString backgroundBase = background;
        backgroundBase.chop(static_cast<int>(sizeof(kCondorSuffix) - 1));
        backgroundBase.append(QLatin1String(".png"));

        CustomTemplate value = {id, label, icon, backgroundBase};
        parsed.append(value);
        seen.insert(id, true);
    }

    for (int i = 0; i < parsed.size(); ++i) {
        CustomTemplate const& value = parsed.at(i);
        rendererMap->insert(value.id, value.backgroundBase);
        customTemplates.append(value);
        nh_log("loaded custom notebook template '%s' as '%s'",
            qPrintable(value.id), qPrintable(value.label));
    }
    trace("manifest: templates inserted into renderer map");
    return true;
}

static void loadAutomaticTemplates() {
    QDir directory(QString::fromLatin1(kTemplateRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (int i = 0; i < files.size(); ++i) {
        if (customTemplates.size() >= kMaximumCustomTemplates) {
            trace("automatic: template limit reached");
            break;
        }

        QFileInfo const& info = files.at(i);
        if (!automaticSource(info))
            continue;

        QString const sourcePath = info.absoluteFilePath();
        if (!hasPngSignature(sourcePath)) {
            trace("automatic: invalid PNG skipped");
            nh_log("automatic template '%s' does not have a PNG signature",
                qPrintable(info.fileName()));
            continue;
        }

        QString label = info.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        label.replace(QLatin1Char('-'), QLatin1Char(' '));
        label = label.simplified().left(64);
        if (label.isEmpty())
            continue;

        uint32_t const hash = stableFilenameHash(info.fileName().toUtf8());
        QString const id = QString::fromLatin1("Custom_Auto_%1")
            .arg(hash, 8, 16, QLatin1Char('0'));
        if (rendererMap->contains(id)) {
            trace("automatic: identifier collision skipped");
            continue;
        }

        QString iconPath;
        if (!createPickerIcon(sourcePath, &iconPath)) {
            trace("automatic: could not generate picker icon");
            nh_log("automatic template '%s' must be a readable %d x %d PNG",
                qPrintable(info.fileName()), kBackgroundWidth, kBackgroundHeight);
            continue;
        }

        QString condorPath;
        if (!syncTemplateOverlay(sourcePath, &condorPath)) {
            trace("automatic: could not prepare renderer copy");
            nh_log("could not create renderer copy '%s'", qPrintable(condorPath));
            continue;
        }

        rendererMap->insert(id, sourcePath);
        customTemplates.append(CustomTemplate{id, label, iconPath, sourcePath});
        trace("automatic: PNG template loaded");
        nh_log("automatically loaded notebook template '%s' as '%s'",
            qPrintable(info.fileName()), qPrintable(label));
    }
}
