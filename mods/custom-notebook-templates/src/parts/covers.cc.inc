#line 1710 "src/customnotebooktemplates.cc"

static void loadAutomaticCovers() {
    QDir directory(QString::fromLatin1(kCoverRoot));
    QFileInfoList const files = directory.entryInfoList(
        QDir::Files | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (int i = 0; i < files.size(); ++i) {
        if (customCovers.size() >= kMaximumCustomCovers) {
            trace("covers: cover limit reached");
            break;
        }

        QFileInfo const& info = files.at(i);
        if (!automaticSource(info))
            continue;

        QString const sourcePath = info.absoluteFilePath();
        if (!hasPngSignature(sourcePath)) {
            trace("covers: invalid PNG skipped");
            continue;
        }

        QString label = info.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        label.replace(QLatin1Char('-'), QLatin1Char(' '));
        label = label.simplified().left(64);
        if (label.isEmpty())
            continue;

        uint32_t const hash = stableFilenameHash(info.fileName().toUtf8());
        QString const id = QString::fromLatin1("Custom_Cover_%1")
            .arg(hash, 8, 16, QLatin1Char('0'));
        if (rendererMap->contains(id)) {
            trace("covers: identifier collision skipped");
            continue;
        }

        QString iconPath;
        if (!createPickerIcon(sourcePath, &iconPath)) {
            trace("covers: could not generate picker icon");
            nh_log("cover '%s' must be a readable %d x %d PNG",
                qPrintable(info.fileName()), kBackgroundWidth, kBackgroundHeight);
            continue;
        }

        QString condorPath;
        if (!syncCondorVariant(sourcePath, &condorPath)) {
            trace("covers: could not prepare renderer copy");
            continue;
        }

        rendererMap->insert(id, sourcePath);
        customCovers.append(CustomTemplate{id, label, iconPath, sourcePath});
        trace("covers: PNG cover loaded");
        nh_log("automatically loaded notebook cover '%s' as '%s'",
            qPrintable(info.fileName()), qPrintable(label));
    }
}
