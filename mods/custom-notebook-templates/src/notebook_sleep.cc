#include "notebook_sleep.h"

#include "cover_cache.h"
#include "firmware_api.h"
#include "firmware_resolver.h"
#include "layers_preview.h"
#include "notebook_widget.h"
#include "notebooksleepsettingsreceiver.h"
#include "pages.h"
#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <NickelHook.h>

#include <QBoxLayout>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMap>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRegion>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <dlfcn.h>
#include <new>

namespace {

enum FirmwarePin : uintptr_t {
    ShowSleepViewVma = 0xf3eb98,
    ShowPowerOffViewVma = 0xf3ea54,
    OnCoverLoadedVma = 0xf425b4,
    UpdateCoverVma = 0xf430b0,
    UpdateReadingStatusVma = 0xf42798,
    SetInfoPanelVisibleVma = 0xf3a114,
    SettingsPowerViewConstructorVma = 0x10455dc,
    CheckBoxConstructorVma = 0x1065824,
    DropDownConstructorVma = 0x106599c,
    CheckBoxSetTextVma = 0x10653a0,
    CheckBoxSetCheckedVma = 0x10653c0,
    ItemSetLabelVma = 0x10652c0,
    DropDownVma = 0x10653f8,
    DropDownAddItemVma = 0x10de390,
    DropDownSetCurrentIndexVma = 0x1113230,
};

enum LayoutPin {
    SettingItemObjectBytes = 0x7c,
    FallbackScreenWidth = 1404,
    FallbackScreenHeight = 1872,
    MaximumBackgroundPngBytes = 32 * 1024 * 1024,
};

template <typename Function>
void* functionPointer(Function function) {
    union {
        Function function;
        void* pointer;
    } value = {};
    value.function = function;
    return value.pointer;
}

template <typename Function>
bool installPinnedHook(
        void* handle,
        char const* symbol,
        uintptr_t vma,
        Function replacement,
        Function* original) {
    Function resolved = nullptr;
    if (!resolvePinned(handle, symbol, vma, &resolved))
        return false;
    *original = resolved;
    void* const hooked = nh_dlhook(
        handle, symbol, functionPointer(replacement));
    if (!pointerMatchesVma(hooked, vma))
        return false;
    union {
        void* pointer;
        Function function;
    } value = {};
    value.pointer = hooked;
    *original = value.function;
    return true;
}

static bool activeNotebookWidget(QWidget* widget) {
    if (!widget
            || !cnt::notebook_widget::isNotebookWidget(widget)
            || !widget->isVisible()
            || widget->visibleRegion().isEmpty()) {
        return false;
    }
    QWidget* const window = widget->window();
    return window && window->isVisible();
}

static QImage fitToPowerView(QImage const& source) {
    if (source.isNull())
        return QImage();
    QSize const target(FallbackScreenWidth, FallbackScreenHeight);
    if (source.size() == target)
        return source.convertToFormat(QImage::Format_RGB32);

    QImage const scaled = source.scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull())
        return QImage();
    QImage result(target, QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter painter(&result);
    painter.drawImage(
        (target.width() - scaled.width()) / 2,
        (target.height() - scaled.height()) / 2,
        scaled);
    return result;
}

static cnt::layers_preview::Dependencies previewDependencies() {
    cnt::layers_preview::Dependencies const dependencies = {
        &firmwareApi(),
        &layerState(),
        &coverState(),
        layerToolRoutingOperations(),
        layerPreviewPins()
    };
    return dependencies;
}

static QString condorVariantPath(QString const& basePath) {
    if (basePath.endsWith(QLatin1String("_condor.png"), Qt::CaseInsensitive))
        return basePath;
    if (!basePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        return QString();
    QString path = basePath;
    path.chop(4);
    path.append(QLatin1String("_condor.png"));
    return path;
}

static void clearBackgroundCache(
        cnt::notebook_sleep::RuntimeState& state) {
    state.cachedBackgroundType.clear();
    state.cachedBackgroundPath.clear();
    state.cachedBackgroundImage = QImage();
    state.cachedBackgroundModifiedMs = -1;
    state.cachedBackgroundSize = -1;
}

static QImage activePageBackground(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget) {
    FirmwareApi& firmware = firmwareApi();
    void* const backgroundWidget = widget
        ? cnt::notebook_widget::notePadBackgroundWidget(widget) : nullptr;
    if (!backgroundWidget || !firmware.backgroundType || !firmware.rendererMap)
        return QImage();

    QString const type = firmware.backgroundType(backgroundWidget);
    if (type.isEmpty())
        return QImage();

    // Background maps are initialized once per Nickel process. On the normal
    // repeated-sleep path, avoid even a map lookup or resource probe. External
    // papers retain the cheap mtime/size validation used by the other caches.
    if (state.cachedBackgroundType == type
            && !state.cachedBackgroundPath.isEmpty()
            && !state.cachedBackgroundImage.isNull()) {
        if (state.cachedBackgroundPath.startsWith(QLatin1String(":/")))
            return state.cachedBackgroundImage;
        QFileInfo const cachedInfo(state.cachedBackgroundPath);
        if (cachedInfo.isFile()
                && cachedInfo.size() == state.cachedBackgroundSize
                && cachedInfo.lastModified().toMSecsSinceEpoch()
                    == state.cachedBackgroundModifiedMs) {
            return state.cachedBackgroundImage;
        }
    }

    QString const basePath = firmware.rendererMap->value(type);
    if (basePath.isEmpty())
        return QImage();

    QString path = condorVariantPath(basePath);
    if (path.isEmpty() || !QFile::exists(path))
        path = basePath;
    bool const immutableResource = path.startsWith(QLatin1String(":/"));
    QFileInfo const info(path);
    qint64 const size = immutableResource ? -1 : info.size();
    qint64 const modifiedMs = immutableResource
        ? -1 : info.lastModified().toMSecsSinceEpoch();
    if (!QFile::exists(path)
            || (!immutableResource
                && (!info.isFile() || size <= 0
                    || size > MaximumBackgroundPngBytes))) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template image unavailable");
        return QImage();
    }

    if (state.cachedBackgroundType == type
            && state.cachedBackgroundPath == path
            && !state.cachedBackgroundImage.isNull()
            && (immutableResource
                || (state.cachedBackgroundModifiedMs == modifiedMs
                    && state.cachedBackgroundSize == size))) {
        return state.cachedBackgroundImage;
    }

    QImageReader reader(path);
    QSize const decodedSize = reader.size();
    if (decodedSize != QSize(FallbackScreenWidth, FallbackScreenHeight)) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template dimensions rejected");
        return QImage();
    }
    QImage const decoded = reader.read();
    if (decoded.isNull()) {
        clearBackgroundCache(state);
        cnt::trace("notebook-sleep: active template decode failed");
        return QImage();
    }

    // The device-specific paper PNG can be a transparent line overlay.
    // Flatten it once onto white and keep exactly one decoded template in
    // memory; repeated sleeps on the same paper avoid file I/O and decoding.
    QImage opaque(decodedSize, QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter backgroundPainter(&opaque);
    backgroundPainter.drawImage(0, 0, decoded);
    backgroundPainter.end();
    if (opaque.isNull()) {
        clearBackgroundCache(state);
        return QImage();
    }

    state.cachedBackgroundType = type;
    state.cachedBackgroundPath = path;
    state.cachedBackgroundImage = opaque;
    state.cachedBackgroundModifiedMs = modifiedMs;
    state.cachedBackgroundSize = size;
    return state.cachedBackgroundImage;
}

static QImage compositeActiveBackground(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget,
        QImage const& renderedPage,
        ExtentOpaque const& extent,
        cnt::layers_preview::LivePageView const& liveView,
        QPoint const& canvasOrigin) {
    if (renderedPage.isNull())
        return QImage();
    if (!std::isfinite(extent.left) || !std::isfinite(extent.top)
            || !std::isfinite(extent.right) || !std::isfinite(extent.bottom)
            || extent.right <= extent.left || extent.bottom <= extent.top) {
        cnt::trace("notebook-sleep: renderer extent rejected");
        return QImage();
    }
    RendererViewTransformOpaque const& transform = liveView.transform;
    if (!std::isfinite(transform.xx) || !std::isfinite(transform.xy)
            || !std::isfinite(transform.x0) || !std::isfinite(transform.yx)
            || !std::isfinite(transform.yy) || !std::isfinite(transform.y0)) {
        cnt::trace("notebook-sleep: renderer view transform rejected");
        return QImage();
    }

    // QTransform's six-argument constructor stores the cross terms in the
    // opposite textual order from atk::core::Transform. Spell out the
    // conversion so the equations remain:
    //
    //   screenX = xx*pageX + xy*pageY + x0 + canvasX
    //   screenY = yx*pageX + yy*pageY + y0 + canvasY
    QTransform const pageToScreen(
        transform.xx,
        transform.yx,
        transform.xy,
        transform.yy,
        transform.x0 + canvasOrigin.x(),
        transform.y0 + canvasOrigin.y());
    if (!pageToScreen.isInvertible()) {
        cnt::trace("notebook-sleep: renderer view transform is singular");
        return QImage();
    }
    QRectF const pageBounds(
        0.0, 0.0, FallbackScreenWidth, FallbackScreenHeight);

    // The exporter maps its MyScript page-space extent into the complete PNG.
    // Apply the live renderer's exact affine mapping to reverse that
    // normalization. This preserves the live zoom, independent X/Y device
    // scales, pan, and the canvas layout offset without a DPI approximation.
    QRectF const modelExtent(
        extent.left,
        extent.top,
        extent.right - extent.left,
        extent.bottom - extent.top);
    QRectF const pageExtentBounds = pageToScreen.mapRect(modelExtent);
    if (!pageExtentBounds.intersects(pageBounds)
            || pageExtentBounds.width() > FallbackScreenWidth * 4.0
            || pageExtentBounds.height() > FallbackScreenHeight * 4.0
            || pageExtentBounds.left() < -FallbackScreenWidth * 4.0
            || pageExtentBounds.top() < -FallbackScreenHeight * 4.0) {
        cnt::trace("notebook-sleep: renderer extent outside page");
        return QImage();
    }

    QImage result = activePageBackground(state, widget);
    if (result.isNull()) {
        result = QImage(
            QSize(FallbackScreenWidth, FallbackScreenHeight),
            QImage::Format_RGB32);
        result.fill(Qt::white);
    }
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    if (renderedPage.size() != pageExtentBounds.size().toSize())
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setWorldTransform(pageToScreen);
    painter.drawImage(
        modelExtent,
        renderedPage,
        QRectF(renderedPage.rect()));
    painter.end();
    cnt::trace("notebook-sleep: ink restored to renderer extent");
    return result;
}

static QImage exportedCurrentPage(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget) {
    QImage image;
    ExtentOpaque extent = {0.0f, 0.0f, 0.0f, 0.0f};
    cnt::layers_preview::LivePageView liveView = {};
    QString error;
    if (cnt::layers_preview::exportCurrentPageImage(
            previewDependencies(),
            widget,
            &image,
            &extent,
            &liveView,
            &error)) {
        QWidget* canvas = nullptr;
        void* const rawBackground = widget
            ? cnt::notebook_widget::notePadBackgroundWidget(widget) : nullptr;
        if (rawBackground) {
            canvas = qobject_cast<QWidget*>(
                reinterpret_cast<QObject*>(rawBackground));
        }
        QWidget* const window = widget ? widget->window() : nullptr;
        if (!canvas || !window || canvas->window() != window)
            canvas = widget;
        QPoint const canvasOrigin = canvas && window
            ? canvas->mapTo(window, QPoint(0, 0)) : QPoint(0, 0);
        if (!state.extentTraceRecorded) {
            state.extentTraceRecorded = true;
            RendererViewTransformOpaque const& transform = liveView.transform;
            cnt::trace(
                QLatin1String("notebook-sleep: renderer bounds left=")
                + QString::number(extent.left, 'f', 2)
                + QLatin1String(" top=") + QString::number(extent.top, 'f', 2)
                + QLatin1String(" right=") + QString::number(extent.right, 'f', 2)
                + QLatin1String(" bottom=") + QString::number(extent.bottom, 'f', 2)
                + QLatin1String(" png=") + QString::number(image.width())
                + QLatin1Char('x') + QString::number(image.height())
                + QLatin1String(" transform=[")
                + QString::number(transform.xx, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.xy, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.x0, 'f', 2) + QLatin1Char(';')
                + QString::number(transform.yx, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.yy, 'f', 4) + QLatin1Char(',')
                + QString::number(transform.y0, 'f', 2)
                + QLatin1String("] canvas=")
                + QString::number(canvasOrigin.x()) + QLatin1Char(',')
                + QString::number(canvasOrigin.y())
                + QLatin1Char(' ')
                + QString::number(canvas ? canvas->width() : 0)
                + QLatin1Char('x')
                + QString::number(canvas ? canvas->height() : 0)
                + QLatin1String(" unit=mm"));
        }
        return compositeActiveBackground(
            state, widget, image, extent, liveView, canvasOrigin);
    }
    if (!error.isEmpty())
        cnt::trace(QLatin1String("notebook-sleep: isolated export unavailable: ") + error);
    return QImage();
}

static QImage notebookCover(
        cnt::notebook_sleep::RuntimeState& state,
        QWidget* widget,
        QString const& notebookPath) {
    QString type;
    if (cnt::cover_cache::zipApisReady(coverState())
            && cnt::cover_cache::cachedNotebookCoverType(
                coverState(), notebookPath, &type, 0.0)) {
        QImage image = cnt::cover_cache::cachedRenderedCoverImage(
            coverState(), notebookPath, type);
        if (image.isNull())
            image = cnt::cover_cache::cleanCustomCoverImage(coverState(), type);
        if (!image.isNull())
            return image;
    }

    QImage const stock = cnt::cover_cache::cachedRenderedCoverImage(
        coverState(), notebookPath, QString());
    if (!stock.isNull())
        return stock;

    // When Kobo has not requested a thumbnail yet, page zero can still use
    // the full-page exporter without changing parts. Other pages preserve the
    // stock book cover until a page-zero thumbnail becomes available.
    cnt::pages::PageContext context;
    QString error;
    if (cnt::pages::loadPageContext(
            firmwareApi(), widget, maximumNotebookPages(), &context, &error)
            && context.index == 0) {
        return exportedCurrentPage(state, widget);
    }
    return QImage();
}

} // namespace

NotebookSleepSettingsReceiver::NotebookSleepSettingsReceiver(
        cnt::SettingsStore* settings,
        QWidget* selectorRow,
        cnt::NotebookSleepSettings const& initial,
        QObject* parent)
    : QObject(parent),
      settings_(settings),
      selectorRow_(selectorRow),
      enabled_(initial.enabled),
      mode_(initial.mode) {}

void NotebookSleepSettingsReceiver::enabledChanged(bool enabled) {
    enabled_ = enabled;
    if (selectorRow_)
        selectorRow_->setEnabled(enabled);
    persist();
}

void NotebookSleepSettingsReceiver::modeChanged(int index) {
    if (index != cnt::NotebookSleepCover
            && index != cnt::NotebookSleepCurrentPage) {
        return;
    }
    mode_ = static_cast<cnt::NotebookSleepImageMode>(index);
    persist();
}

void NotebookSleepSettingsReceiver::persist() {
    if (settings_)
        settings_->persistNotebookSleep(enabled_, mode_);
}

namespace cnt {
namespace notebook_sleep {

bool installHooks(RuntimeState& state) {
    void* const handle = dlopen(
        "libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        trace("notebook-sleep: libnickel handle unavailable");
        return false;
    }

    bool const core =
        installPinnedHook(
            handle,
            "_ZN22N3PowerWorkflowManager13showSleepViewEv",
            ShowSleepViewVma,
            _cnt_show_sleep_view_hook,
            &state.api.showSleepViewOriginal)
        && installPinnedHook(
            handle,
            "_ZN22N3PowerWorkflowManager16showPowerOffViewEv",
            ShowPowerOffViewVma,
            _cnt_show_power_off_view_hook,
            &state.api.showPowerOffViewOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController13onCoverLoadedERK6QImage",
            OnCoverLoadedVma,
            _cnt_power_cover_loaded_hook,
            &state.api.onCoverLoadedOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController11updateCoverEv",
            UpdateCoverVma,
            _cnt_power_update_cover_hook,
            &state.api.updateCoverOriginal)
        && installPinnedHook(
            handle,
            "_ZN19PowerViewController19updateReadingStatusEv",
            UpdateReadingStatusVma,
            _cnt_power_update_reading_status_hook,
            &state.api.updateReadingStatusOriginal)
        && installPinnedHook(
            handle,
            "_ZN25FullScreenDragonPowerView19setInfoPanelVisibleEb",
            SetInfoPanelVisibleVma,
            _cnt_power_set_info_panel_visible_hook,
            &state.api.setInfoPanelVisibleOriginal);
    state.coreHooksReady = core;
    if (core)
        trace("notebook-sleep: guarded power workflow hooks installed");
    else
        trace("notebook-sleep: power workflow hook validation failed; feature disabled");

    bool const nativeSettingsApis =
        resolvePinned(handle, "_ZN23SettingItemWithCheckBoxC1EP7QWidget",
            CheckBoxConstructorVma, &state.api.checkBoxConstructor)
        && resolvePinned(handle, "_ZN23SettingItemWithDropDownC1EP7QWidget",
            DropDownConstructorVma, &state.api.dropDownConstructor)
        && resolvePinned(handle, "_ZN23SettingItemWithCheckBox7setTextERK7QString",
            CheckBoxSetTextVma, &state.api.checkBoxSetText)
        && resolvePinned(handle, "_ZN23SettingItemWithCheckBox10setCheckedEb",
            CheckBoxSetCheckedVma, &state.api.checkBoxSetChecked)
        && resolvePinned(handle, "_ZN15SettingItemBase8setLabelERK7QString",
            ItemSetLabelVma, &state.api.itemSetLabel)
        && resolvePinned(handle, "_ZNK23SettingItemWithDropDown8dropDownEv",
            DropDownVma, &state.api.dropDown)
        && resolvePinned(
            handle,
            "_ZN24MultiSelectTouchDropDown7addItemERK7QStringRK8QVariantRK7QLocaleb",
            DropDownAddItemVma,
            &state.api.dropDownAddItem)
        && resolvePinned(handle, "_ZN13TouchDropDown15setCurrentIndexEi",
            DropDownSetCurrentIndexVma, &state.api.dropDownSetCurrentIndex);
    if (core && nativeSettingsApis
            && installPinnedHook(
                handle,
                "_ZN19N3SettingsPowerViewC1EP7QWidget",
                SettingsPowerViewConstructorVma,
                _cnt_settings_power_view_constructor_hook,
                &state.api.settingsPowerViewConstructorOriginal)) {
        state.settingsHookReady = true;
        trace("notebook-sleep: native power-settings hook installed");
    } else {
        trace("notebook-sleep: native power-settings hook unavailable");
    }
    dlclose(handle);
    return state.coreHooksReady;
}

void observeNotebookWidget(RuntimeState& state, QWidget* widget) {
    if (!widget || !notebook_widget::isNotebookWidget(widget))
        return;
    state.activeNotebook = widget;
    state.renderHookReady = true;
    trace("notebook-sleep: live notebook observed");
}

void prepareNotebookImage(RuntimeState& state) {
    state.pendingNotebookImage = false;
    state.pendingImage = QImage();
    if (!state.coreHooksReady)
        return;

    NotebookSleepSettings const configured =
        settingsStore().configuredNotebookSleep();
    if (!configured.enabled)
        return;
    QWidget* const widget = state.activeNotebook.data();
    if (!state.renderHookReady || !activeNotebookWidget(widget)) {
        trace("notebook-sleep: no visible notebook at power boundary; stock cover preserved");
        return;
    }

    try {
        QString const notebookPath = QFileInfo(
            firmwareApi().widgetFilePath(widget)).absoluteFilePath();
        if (!notebookPath.startsWith(QLatin1String("/mnt/onboard/"))
                || !notebookPath.endsWith(
                    QLatin1String(".nebo"), Qt::CaseInsensitive)) {
            trace("notebook-sleep: active notebook path rejected; stock cover preserved");
            return;
        }
        QImage image = configured.mode == NotebookSleepCurrentPage
            ? exportedCurrentPage(state, widget)
            : notebookCover(state, widget, notebookPath);
        image = fitToPowerView(image);
        if (image.isNull()) {
            trace("notebook-sleep: notebook image unavailable; stock cover preserved");
            return;
        }
        state.pendingImage = image;
        state.pendingNotebookImage = true;
        trace(configured.mode == NotebookSleepCurrentPage
            ? "notebook-sleep: current page armed for power view"
            : "notebook-sleep: notebook cover armed for power view");
    } catch (...) {
        state.pendingImage = QImage();
        state.pendingNotebookImage = false;
        trace("notebook-sleep: image preparation threw; stock cover preserved");
    }
}

void augmentPowerSettings(RuntimeState& state, QWidget* view) {
    if (!state.settingsHookReady || !view
            || view->findChild<QWidget*>(
                QLatin1String("_cntShowNotebookOnSleep"))) {
        return;
    }
    QWidget* const container = view->findChild<QWidget*>(
        QLatin1String("coverFullscreenOptions"));
    QVBoxLayout* const layout = container
        ? qobject_cast<QVBoxLayout*>(container->layout()) : nullptr;
    if (!container || !layout) {
        trace("notebook-sleep: coverFullscreenLayout not found; settings rows skipped");
        return;
    }

    void* toggleMemory = nullptr;
    void* selectorMemory = nullptr;
    bool toggleConstructed = false;
    bool selectorConstructed = false;
    try {
        toggleMemory = ::operator new(SettingItemObjectBytes);
        state.api.checkBoxConstructor(toggleMemory, container);
        toggleConstructed = true;
        selectorMemory = ::operator new(SettingItemObjectBytes);
        state.api.dropDownConstructor(selectorMemory, container);
        selectorConstructed = true;
    } catch (...) {
        if (toggleConstructed)
            reinterpret_cast<QWidget*>(toggleMemory)->deleteLater();
        else if (toggleMemory)
            ::operator delete(toggleMemory);
        if (selectorConstructed)
            reinterpret_cast<QWidget*>(selectorMemory)->deleteLater();
        else if (selectorMemory)
            ::operator delete(selectorMemory);
        trace("notebook-sleep: native settings row construction failed");
        return;
    }

    QWidget* const toggle = reinterpret_cast<QWidget*>(toggleMemory);
    QWidget* const selector = reinterpret_cast<QWidget*>(selectorMemory);
    toggle->setObjectName(QLatin1String("_cntShowNotebookOnSleep"));
    selector->setObjectName(QLatin1String("_cntNotebookSleepImage"));
    // Match Ui_N3SettingsPowerView::retranslateUi exactly: the inherited
    // SettingItemBase label owns the left column, while setText() labels only
    // the compact checkbox in the right column. Putting the setting name in
    // setText() centers the entire control instead of aligning it with Kobo's
    // native power rows.
    state.api.itemSetLabel(
        toggle, QLatin1String("Show notebook on sleep:"));
    state.api.checkBoxSetText(toggle, QLatin1String("On"));
    state.api.itemSetLabel(
        selector, QLatin1String("Notebook sleep image:"));

    void* const dropDown = state.api.dropDown(selector);
    if (!dropDown) {
        toggle->deleteLater();
        selector->deleteLater();
        trace("notebook-sleep: native selector missing; settings rows skipped");
        return;
    }
    QLocale const locale;
    state.api.dropDownAddItem(
        dropDown,
        QLatin1String("Cover"),
        QVariant(NotebookSleepCover),
        locale,
        false);
    state.api.dropDownAddItem(
        dropDown,
        QLatin1String("Current page"),
        QVariant(NotebookSleepCurrentPage),
        locale,
        false);

    NotebookSleepSettings const configured =
        settingsStore().configuredNotebookSleep();
    state.api.checkBoxSetChecked(toggle, configured.enabled);
    state.api.dropDownSetCurrentIndex(dropDown, configured.mode);
    selector->setEnabled(configured.enabled);

    NotebookSleepSettingsReceiver* const receiver =
        new NotebookSleepSettingsReceiver(
            &settingsStore(), selector, configured, view);
    bool const toggleConnected = QObject::connect(
        toggle,
        SIGNAL(toggled(bool)),
        receiver,
        SLOT(enabledChanged(bool)));
    bool const selectorConnected = QObject::connect(
        reinterpret_cast<QObject*>(dropDown),
        SIGNAL(currentIndexChanged(int)),
        receiver,
        SLOT(modeChanged(int)));
    if (!toggleConnected || !selectorConnected) {
        toggle->deleteLater();
        selector->deleteLater();
        receiver->deleteLater();
        trace("notebook-sleep: native settings signal connection failed");
        return;
    }
    layout->addWidget(toggle);
    layout->addWidget(selector);
    trace("notebook-sleep: native power-settings rows added");
}

} // namespace notebook_sleep
} // namespace cnt

extern "C" __attribute__((visibility("default")))
void _cnt_show_sleep_view_hook(void* manager) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    cnt::notebook_sleep::prepareNotebookImage(state);
    if (state.api.showSleepViewOriginal)
        state.api.showSleepViewOriginal(manager);
}

extern "C" __attribute__((visibility("default")))
void _cnt_show_power_off_view_hook(void* manager) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    cnt::notebook_sleep::prepareNotebookImage(state);
    if (state.api.showPowerOffViewOriginal)
        state.api.showPowerOffViewOriginal(manager);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_cover_loaded_hook(void* controller, QImage const& image) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.api.onCoverLoadedOriginal)
        return;
    if (state.pendingNotebookImage)
        cnt::trace("notebook-sleep: asynchronous cover replaced");
    state.api.onCoverLoadedOriginal(
        controller,
        state.pendingNotebookImage ? state.pendingImage : image);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_cover_hook(void* controller) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (state.pendingNotebookImage
            && !state.pendingImage.isNull()
            && state.api.onCoverLoadedOriginal) {
        // updateCover() normally connects imageReady to a QSlotObject which
        // stores onCoverLoaded's direct member-function address. That path
        // bypasses the symbol jump slot. Feed the prepared image through the
        // stock slot here and do not start a later book-cover load which could
        // overwrite it.
        state.api.onCoverLoadedOriginal(controller, state.pendingImage);
        cnt::trace("notebook-sleep: prepared image applied at updateCover");
        return;
    }
    if (state.api.updateCoverOriginal)
        state.api.updateCoverOriginal(controller);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_update_reading_status_hook(void* controller) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.pendingNotebookImage && state.api.updateReadingStatusOriginal)
        state.api.updateReadingStatusOriginal(controller);
}

extern "C" __attribute__((visibility("default")))
void _cnt_power_set_info_panel_visible_hook(void* view, bool visible) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (state.api.setInfoPanelVisibleOriginal) {
        state.api.setInfoPanelVisibleOriginal(
            view, state.pendingNotebookImage ? false : visible);
    }
}

extern "C" __attribute__((visibility("default")))
void _cnt_settings_power_view_constructor_hook(void* view, QWidget* parent) {
    cnt::notebook_sleep::RuntimeState& state = pluginState().notebookSleep;
    if (!state.api.settingsPowerViewConstructorOriginal)
        return;
    state.api.settingsPowerViewConstructorOriginal(view, parent);
    cnt::notebook_sleep::augmentPowerSettings(
        state, reinterpret_cast<QWidget*>(view));
}
