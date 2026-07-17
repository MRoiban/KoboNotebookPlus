#!/usr/bin/env python3
"""Verify every firmware-pinned symbol used by the notebook layer prototype."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import subprocess
import sys


EXPECTED = {
    "_ZN8myscript4iink10EditorImpl9getEngineEv": 0x425BB0,
    "_ZN8myscript4iink10EditorImpl16getConfigurationEv": 0x425CF0,
    "_ZN8myscript4iink10EditorImpl11getRendererEv": 0x425C00,
    "_ZNK8myscript4iink15ContentPartImpl5getIdEv": 0x3F5BFC,
    "_ZNK3atk4core4Page6layoutEv": 0x9F990C,
    "_ZN3atk4core6LayoutD1Ev": 0x3E6BE4,
    "_ZN3atk4core6Layout19appendLayerWithNameERKSs": 0x9DA780,
    "_ZN3atk4core6Layout11removeLayerERKSs": 0x9D9BE8,
    "_ZNK3atk4core6Layout7_layoutEv": 0x9DA8C8,
    "_ZNK8myscript8document6Layout8getLayerERKSs": 0x4B3D00,
    "_ZNK8myscript8document13LayerIterator7isAtEndEv": 0x4B3E88,
    "_ZN8myscript6engine13ManagedObjectD1Ev": 0x3E5390,
    "_ZN8myscript4iink12RendererImpl10getBackendEv": 0x49E318,
    "_ZNK8myscript4iink12RendererImpl16getViewTransformEv": 0x49DF8C,
    "_ZN3atk4core8Renderer16restrictToLayersERKSt6vectorISsSaISsEE": 0xA57714,
    "_ZN3snt14PageController11exportToPNGESt10shared_ptrIN3atk4core3BoxEERKSsS1_INS3_16RendererListenerEEj": 0x5130C4,
    "_ZNSt12__shared_ptrIN8myscript4iink18BackendImageDrawerELN9__gnu_cxx12_Lock_policyE2EEC2ISaIS2_EIRSt10shared_ptrINS1_10EngineImplEERS8_INS1_13IImagePainterEERS8_INS1_17ConfigurationImplEEEEESt19_Sp_make_shared_tagRKT_DpOT0_": 0x443EAC,
    "_ZN8myscript4iink18BackendImageDrawer9drawImageEN3atk4core4PageESt10shared_ptrINS3_8RendererEEPNS0_8IBackendENS3_6ExtentENS3_9SelectionESs": 0x3E7B8C,
    "_ZN3snt14PageController19setRendererListenerESt10shared_ptrIN3atk4core16RendererListenerEE": 0x505498,
    "_ZNK3snt14PageController15inputDispatcherEv": 0x501960,
    "_ZNK3snt23PlatformInputDispatcher14getCurrentToolEv": 0x5BC80C,
    "_ZN3snt14DrawingBackend7getToolEN3atk4core4Tool4KindERKSs": 0x5589CC,
    "_ZN3snt14DrawingBackend4initEN3atk4core4PageERKNS2_13ActiveBackend13ConfigurationE": 0x55D6F4,
    "_ZSt20dynamic_pointer_castIN3snt14ToolDispatcherEN3atk4core4ToolEESt10shared_ptrIT_ERKS5_IT0_E": 0x522D44,
    "_ZN3snt14ToolDispatcher15restrictToLayerERKSs": 0x5C4C90,
    "_ZN3atk4core4Tool15restrictToLayerERKSs": 0xA84358,
    "_ZNK3atk4core4Tool15restrictedLayerEv": 0xA842F0,
    "_ZN3atk4core3Pen7penDownERKNS0_11PointerInfoE": 0xA793EC,
    "_ZN3atk4core8SmartPen7penDownERKNS0_11PointerInfoE": 0xA83C00,
    "_ZN3atk4core10InkSampler14setStrokeLayerEjRKSs": 0xA9B710,
    "_ZN3atk4core6Eraser7penDownERKNS0_11PointerInfoE": 0xA78668,
    "_ZN3atk4core6Eraser5penUpERKNS0_11PointerInfoE": 0xA76DA8,
    "_ZN3atk4core6Eraser19selectionFromPointsERKSt6vectorINS0_11PointerInfoESaIS3_EE": 0xA77470,
    "_ZN3atk4core6Eraser15updateSelectionERKNS0_9SelectionERKSt6vectorINS0_11PointerInfoESaIS6_EE": 0xA75CE4,
    "_ZN3atk4core6Eraser15setEraserPolicyENS1_12EraserPolicyE": 0xA75A94,
    "_ZNK3atk4core6Eraser12eraserPolicyEv": 0xA75AA4,
    "_ZN3atk4core6Eraser9setRadiusEf": 0xA75B2C,
    "_ZNK3atk4core6Eraser6radiusEv": 0xA75ADC,
    "_ZN3snt13PenStyleUtils29eraserWidthFromThicknessRatioEff": 0x69C278,
    "_ZNK3snt10LayoutGrid7lineGapEv": 0x6051FC,
    "_ZN3atk7diagram13DiagramEraser7penDownERKNS_4core11PointerInfoE": 0x75B670,
    "_ZN3atk7diagram13DiagramEraser15updateSelectionERKNS_4core9SelectionERKSt6vectorINS2_11PointerInfoESaIS7_EE": 0x75B73C,
    "_ZN3atk7diagram13DiagramEraser14eraseSelectionEv": 0x75C224,
    "_ZN3atk7diagram7Diagram20removeEraseSelectionERKNS_4core9SelectionERSsNS2_6Eraser12EraserPolicyEbNS0_14RemoveItemFlagE": 0x706508,
    "_ZN3atk7diagram20DiagramActiveBackend7getToolENS_4core4Tool4KindERKSs": 0x728B7C,
    "_ZN3atk7diagram10DiagramPen7penDownERKNS_4core11PointerInfoE": 0x7AF1E4,
    "_ZN3atk7diagram10DiagramPen15restrictToLayerERKSs": 0x7B0E6C,
    "_ZNK3snt19CompositeBoxFactory11mainBackendEv": 0x5B81B8,
    "_ZNK3snt19CompositeBoxFactory8backendsEv": 0x5B83E0,
    "_ZN3atk4core9Selection10selectNoneEv": 0x9E1C18,
    "_ZN3atk4core9Selection11selectLayerERKSsNS1_4ModeE": 0x9E1EC4,
    "_ZN3atk4core9Selection13selectPolygonEPKNS0_5PointEiNS1_4ModeE": 0x9E27B8,
    "_ZN3atk4core9Selection24adjustToStrokeBoundariesEf": 0x9E2D70,
    "_ZNK3atk4core9Selection7isEmptyEv": 0x9E30F0,
    "_ZN3atk4core6Eraser14strokerPolygonEfRKSt6vectorINS0_11PointerInfoESaIS3_EE": 0xA75FB0,
    "_ZNK3atk4core8Selector16computeSelectionEv": 0xA7CE70,
    "_ZN13DrawingEraser19selectionFromPointsERKSt6vectorIN3atk4core11PointerInfoESaIS3_EE": 0x421314,
    "_ZN3snt13DrawingEraser19selectionFromPointsERKSt6vectorIN3atk4core11PointerInfoESaIS4_EE": 0x562838,
    "_ZTVN3snt14DrawingBackendE": 0xD8A190,
    "_ZTVN3snt10DrawingPenE": 0xD8A540,
    "_ZTVN3snt12DrawingBrushE": 0xD8A3A0,
    "_ZTVN3snt13DrawingEraserE": 0xD8A478,
    "_ZTV13DrawingEraser": 0xD856E0,
    "_ZTVN3atk7diagram13DiagramEraserE": 0xD8EB30,
    "_ZTVN3atk4core6EraserE": 0xD97370,
    "_ZTVN3snt13TextEraserSNTE": 0xD8B470,
    "_ZTVN3atk4math10MathEraserE": 0xD94470,
    "_ZTVN3atk4text10TextEraserE": 0xD95C10,
    "_ZTVN3atk7diagram10DiagramPenE": 0xD8FFB0,
    "_ZTVN3atk7diagram18DraftActiveBackendE": 0xD90288,
    "_ZTVN3snt15DrawingSelectorE": 0xD8A620,
    "_ZN8myscript4iink18ContentPackageImpl4saveEv": 0x3F1960,
    "_ZN3atk4core8Document4saveEv": 0x9F1584,
    "_ZNK8myscript8document13IDocumentPriv6save2_Ev": 0xB59E84,
    "_ZNK8myscript8document4Page6getId_Ev": 0xB098A4,
    "_ZN3atk4core11Transaction13commitAsGhostEv": 0xAB6350,
    "_ZNK8myscript8document6Layout9addLayer_ERKSs": 0xAF0CAC,
    "_ZNK8myscript4iink6StringcvSsEv": 0x3E52A8,
    "_ZN3atk4core6Layout19DOCUMENT_LAYER_NAMEE": 0x21CB994,
    "_ZN3atk4core6Layout28BACKGROUND_OBJECT_LAYER_NAMEE": 0x21CB998,
    "_ZTVN8myscript4iink11NeboBackendE": 0xD871A0,
}

# Live stroke routing calls atk::core::Tool::restrictToLayer virtually via slot
# +0x54 (restrictedLayer() is +0x58) on concrete DrawingBackend caches. The
# all exact eraser adapters replace selectionFromPoints at +0x78. The two
# DrawingErasers intersect their stock hit selection. Raw-content DiagramEraser
# rebuilds the stock geometry in Selector's verified layer-first order, using
# the exact core strokerPolygon and selectPolygon APIs. DraftActiveBackend's
# +0x2c getter also returns cached DiagramPen for kind 0 so writing remains
# synchronized after a tool switch. Pin those identities and source vtable
# sizes as well as the Pen, Brush, Eraser, Selector, base, and dispatcher slots.
# Object vptr points at vtable_symbol + 8; the ARM Thumb low bit is masked when
# comparing to the function VMA.
VTABLE_SLOTS = {
    # vtable symbol VMA: {slot offset: expected function VMA}
    0xD977A8: {  # vtable for atk::core::Tool
        0x54: 0xA84358,  # Tool::restrictToLayer
        0x58: 0xA842F0,  # Tool::restrictedLayer
    },
    0xD8B538: {  # vtable for snt::ToolDispatcher
        0x54: 0x5C4C90,  # ToolDispatcher::restrictToLayer
        0x58: 0x5C3464,  # ToolDispatcher::restrictedLayer
    },
    0xD8A190: {  # vtable for snt::DrawingBackend
        0x2C: 0x5589CC,  # DrawingBackend::getTool
    },
    0xD8A540: {  # vtable for snt::DrawingPen
        0x3C: 0xA793EC,  # atk::core::Pen::penDown
        0x54: 0xA84358,  # Tool::restrictToLayer
        0x58: 0xA842F0,  # Tool::restrictedLayer
    },
    0xD8A3A0: {  # vtable for snt::DrawingBrush
        0x3C: 0xA793EC,  # atk::core::Pen::penDown
        0x54: 0xA84358,  # Tool::restrictToLayer
        0x58: 0xA842F0,  # Tool::restrictedLayer
    },
    0xD8A478: {  # vtable for snt::DrawingEraser
        0x3C: 0xA78668,  # atk::core::Eraser::penDown
        0x54: 0xA84358,  # stores the active ID consumed by the plugin adapter
        0x58: 0xA842F0,  # adapter reads Tool::restrictedLayer
        0x78: 0x562838,  # DrawingEraser::selectionFromPoints
    },
    0xD856E0: {  # vtable for global DrawingEraser used by dispatcher backends
        0x3C: 0xA78668,  # atk::core::Eraser::penDown
        0x54: 0xA84358,  # stores the active ID consumed by the plugin adapter
        0x58: 0xA842F0,  # adapter reads Tool::restrictedLayer
        0x78: 0x421314,  # DrawingEraser::selectionFromPoints
    },
    0xD8EB30: {  # vtable for atk::diagram::DiagramEraser on raw-content pages
        0x3C: 0x75B670,  # DiagramEraser::penDown -> core Eraser::penDown
        0x54: 0xA84358,  # stores the active ID consumed by the plugin adapter
        0x58: 0xA842F0,  # adapter reads Tool::restrictedLayer
        0x78: 0xA77470,  # atk::core::Eraser::selectionFromPoints
        0x7C: 0x75B73C,  # DiagramEraser::updateSelection
        0x80: 0x75C224,  # DiagramEraser::eraseSelection
    },
    0xD8FFB0: {  # vtable for cached atk::diagram::DiagramPen
        0x3C: 0x7AF1E4,  # DiagramPen::penDown
        0x54: 0x7B0E6C,  # updates Tool restriction and shared Diagram state
        0x58: 0xA842F0,  # Tool::restrictedLayer
    },
    0xD90288: {  # vtable for atk::diagram::DraftActiveBackend
        0x2C: 0x728B7C,  # DiagramActiveBackend::getTool
    },
    0xD8A620: {  # vtable for snt::DrawingSelector
        0x54: 0xA84358,  # Tool::restrictToLayer
        0x58: 0xA842F0,  # Tool::restrictedLayer
    },
}

VTABLE_SIZES = {
    "_ZTV13DrawingEraser": 148,
    "_ZTVN3atk7diagram13DiagramEraserE": 148,
    "_ZTVN3atk7diagram10DiagramPenE": 152,
    "_ZTVN3atk7diagram18DraftActiveBackendE": 428,
    "_ZTVN3snt13DrawingEraserE": 148,
}

# Exact Thumb instructions. The first two preserve the disproven final-
# Selection evidence. The next five pin Diagram backend tool caches. The last
# six prove the layer-first erase fix: core Eraser loads width at +0xcc and
# view scale at +0x110, multiplies them, and selects its polygon with Replace;
# Selector selects the layer with Replace and its polygon with Intersection.
# The next four pin the DiagramPen pen-down guard's exact downstream chain:
# DiagramPen -> SmartPen -> core Pen, followed by core Pen's virtual
# restrictedLayer slot load and InkSampler::setStrokeLayer call. The final
# custom-layer pins prove core Eraser calls +0x7c, forwards its boolean into
# renderer update, calls +0x80 on pen-up, and DiagramEraser sends the committed
# +0xd0 Selection to Diagram::removeEraseSelection.
# Binary Ninja address = ELF VMA +0x10000.
PINNED_INSTRUCTIONS = {
    0xA788EA: bytes.fromhex("05f1d000"),
    0x75C228: bytes.fromhex("00f1d004"),
    0x728C4E: bytes.fromhex("736e"),
    0x728C76: bytes.fromhex("f36d"),
    0x7B0E76: bytes.fromhex("d3f26ffa"),
    0x7B0E7E: bytes.fromhex("fbf74fff"),
    0x7B0E88: bytes.fromhex("6030"),
    0xA7748C: bytes.fromhex("d1ed337a"),
    0xA77492: bytes.fromhex("d1ed448a"),
    0xA774A8: bytes.fromhex("67eea88a"),
    0xA7764C: bytes.fromhex("0023"),
    0xA7CED0: bytes.fromhex("0022"),
    0xA7D07E: bytes.fromhex("0223"),
    0x7AF41E: bytes.fromhex("d4f2effb"),
    0xA83CF8: bytes.fromhex("f5f778fb"),
    0xA794B0: bytes.fromhex("936d"),
    0xA794BE: bytes.fromhex("22f027f9"),
    # core Eraser::updateSelection ABI the diagram layer-commit relies on:
    # empty-selection guard (cbz r0), the Eraser+0xd0 committed-selection
    # formation (adds r4, #0xd0), and the combine mode-1 argument (movs r2, #1).
    0xA75CFA: bytes.fromhex("28b1"),
    0xA75D08: bytes.fromhex("d034"),
    0xA75D16: bytes.fromhex("0122"),
    # The native eraser policy lives at Eraser+0xc4. ToolDispatcher maps the
    # UI value "stroke" to 0 and "precise" to 1. DrawingEraser and
    # DiagramEraser both read that field and, only for policy 0, expand the
    # polygon hit to whole stroke boundaries using an exact 0.0f ratio.
    0xA75A98: bytes.fromhex("c0f8c410"),
    0xA75AA8: bytes.fromhex("d0f8c400"),
    0x5C3998: bytes.fromhex("0146"),
    0x5C399C: bytes.fromhex("b2f07af0"),
    0x5C3B1E: bytes.fromhex("0121"),
    0x5C3B22: bytes.fromhex("b1f0b7f7"),
    0x562848: bytes.fromhex("d4f8c430"),
    0x562850: bytes.fromhex("23b1"),
    0x5628E2: bytes.fromhex("9fed650a"),
    0x5628E6: bytes.fromhex("4bf277f1"),
    0x562A78: bytes.fromhex("00000000"),
    0x75B7B4: bytes.fromhex("d3f8c430"),
    0x75B7BA: bytes.fromhex("40f0ae81"),
    0x75BA00: bytes.fromhex("9fedaf0a"),
    0x75BA04: bytes.fromhex("52f0e8f0"),
    0x75BCC0: bytes.fromhex("00000000"),
    # Public Selection wrapper forwarding to PageSelection's boundary adjust.
    0x9E2D82: bytes.fromhex("14f167f8"),
    0xA78902: bytes.fromhex("f66f"),
    0xA77C40: bytes.fromhex("def87cb0"),
    0xA77C4C: bytes.fromhex("d847"),
    0xA77C84: bytes.fromhex("d3f88430"),
    0xA76F0A: bytes.fromhex("d3f88030"),
    0xA76F0E: bytes.fromhex("9847"),
    0x75C284: bytes.fromhex("aaf740f9"),
    # Eraser-size ABI. PageController+0xe4 is its LayoutGrid; lineGap() reads
    # +0x18. The plugin calls the public conversion and setRadius APIs, while
    # these pins prove the stock half-width conversion and setRadius style
    # update rather than a raw Eraser+0xcc write.
    0x506424: bytes.fromhex("d6f8e400"),
    0x605200: bytes.fromhex("90ed060a"),
    0xA75ADC: bytes.fromhex("014680b596b000af"),
    0xA75AE4: bytes.fromhex("0b68381d9b6b984797ed080a"),
    0xA75B2C: bytes.fromhex("2de9f04306462ded028b87b002af"),
    0xA75B44: bytes.fromhex("5a6ad3f830809047"),
    0xA75BB4: bytes.fromhex("c047"),
    0x5C38E2: bytes.fromhex("d8f0c9fcf6ee007a386920ee270ab2f01cf1"),
    0x69C310: bytes.fromhex(
        "cdcc4c3e9a99993e3d0ad73e9a99193f9a99593f"
    ),
    # Layer-preview export ABI. setRendererListener proves that the live
    # PageController owns its listener shared_ptr at data(+0x1b8)+0x34. The
    # export path creates the isolated 1024x1024 renderer and invokes that
    # listener synchronously through its third virtual slot.
    0x50549C: bytes.fromhex("d0f8b8510a68a86b104b6a634c68"),
    0x5132D8: bytes.fromhex("4ff0000c83608146a2584ff4806a0221"),
    0x513392: bytes.fromhex("7b6828461c682368d3f808903e677d67fff623f8"),
    0x5133B2: bytes.fromhex(
        "2046bc6897ed180a3246d7ed190a414697ed1a1ad7ed1b1ad7f8d8300094c847"
    ),
    # Replacement preview-writer ownership. EditorImpl returns strong owners
    # for EngineImpl from +0x2c/+0x30 and ConfigurationImpl-compatible storage
    # from +0x44/+0x48. Stock EditorImpl::export_ forwards that same
    # configuration owner to the explicitly typed helper pinned below.
    0x425BB4: bytes.fromhex("cb6a104a03600b6b7a444360"),
    0x425CF4: bytes.fromhex("4b6c104a03608b6c7a444360"),
    # The BackendImageDrawer helper receives &engine in r3 and
    # &painter/&configuration in the first two stack words. It allocates a
    # 0x40-byte inplace control block, then constructs the 0x34-byte drawer at
    # control-block +0x0c by calling the stock constructor.
    0x443EBC: bytes.fromhex(
        "40206a609a46d7f84090d7f844807e440df636ef04460028"
    ),
    0x443F5C: bytes.fromhex(
        "07f1080307f1100207f1180104f10c00a3f72cf9"
    ),
    # Kobo's anonymous RendererListener callback is deliberately reused
    # instead of reproducing non-trivial Page/Layout/Selection copies. Pin its
    # entry, drawer load at this+0x04, renderer->layout()->page(), copied
    # renderer owner, Selection copy with backend load at this+0x24, path copy,
    # and complete hard-float drawImage call setup. The final BL in the last
    # sequence is the stock BackendImageDrawer::drawImage call.
    0x47B090: bytes.fromhex(
        "2de9f04f9bb002af07f140088146484e4046"
    ),
    0x47B0BE: bytes.fromhex(
        "d9f804a0dcf1c5f1414620465ef131f2"
    ),
    0x47B0CE: bytes.fromhex("95e80c00fb61ba61"),
    0x47B0F8: bytes.fromhex(
        "07f1200559462846d9f824b066f158f5"
    ),
    0x47B108: bytes.fromhex("07f11409d7f888104846d6f5ceef"),
    0x47B116: bytes.fromhex(
        "97ed000a5b46d7ed010a504697ed021a07f11802d7ed031a"
        "2146cdf8049000956cf729fd"
    ),
    # BackendImageDrawer treats atk::core::Extent as left/top/right/bottom.
    # Its default image-size path subtracts the first coordinate pair from the
    # second, converts the resulting millimetres through the configured image
    # resolution, and rounds those two differences to pixel dimensions. This
    # prevents the sleep compositor from ever interpreting right/bottom as
    # width/height again.
    0x3E80F2: bytes.fromhex(
        "dfedc59a30ee6a0a95ed069a38ee4b8a80ee290a20ee090a6af68aea"
        "fdeec08a88ee290a29ee000a6af682ead9f81810bdeec09a"
    ),
}

EXPECTED_IINKNOTE = {
    "_ZN17IInkNotePadWidget12setToolThemeER13IInkToolTheme": 0x62200,
    "_ZN17IInkNotePadWidget12renderVolumeERK6Volume": 0x66720,
    "_ZN26IInkDropdownMenuController18createIInkMenuItemEP5QMenuRK7QStringRK7QPixmapb": 0x46BC4,
    "_ZN22IInkToolMenuController10tapGestureEP15GestureReceiverP10TapGesture": 0x75A4C,
    "_ZN22IInkToolMenuControllerC1EP7QWidgetRK7QVectorI8IInkToolERKS2_I13IInkToolBrushE13IInkToolTheme": 0x74760,
    "_ZN22IInkToolMenuController18createBrushSizeRowEP15NickelTouchMenuRK7QString": 0x74FCC,
    "_ZN22IInkToolMenuController17setBrushSizeIndexEi": 0x74BA4,
    "_ZN17IInkNotePadWidget13setActiveToolE8IInkTool": 0x62194,
    "_ZN17IInkNotePadWidget16stylusTouchBeginEP11QTouchEvent": 0x622A4,
    "_ZN22IInkToolMenuController8loadViewEv": 0x753C8,
    "_ZNK13IInkToolTheme7getToolEv": 0x76998,
    "_ZN13IInkToolTheme20getBrushSizesForToolE8IInkTool": 0x76954,
    "_ZN13IInkToolTheme17setBrushSizeIndexEi": 0x76C78,
    "_ZN11IInkToolBar18onToolButtonTappedEb": 0x70D28,
    "_ZN18IInkToolMenuWidgetC1EP7QWidget": 0x761AC,
    "_ZN18IInkToolMenuWidget11setSelectedEb": 0x76580,
}

EXPECTED_IINKUIREF = {
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainterC1Ev": 0x28B48,
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainterD0Ev": 0x29948,
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainter14setImageLoaderESt10shared_ptrINS1_11ImageLoaderEE": 0x28A60,
    "_ZN8myscript4iink25uireferenceimplementation12ImagePainter9saveImageERKNS0_6StringE": 0x28354,
    "_ZTVN8myscript4iink25uireferenceimplementation12EditorWidgetE": 0x50000,
}

# ImagePainter derives its encoder name from QFileInfo::suffix(), converts
# that suffix to UTF-8, and passes it as QImage::save's format argument. A
# temporary export path must therefore still end in `.png`.
PINNED_IINKUIREF_INSTRUCTIONS = {
    0x28444: bytes.fromhex("07f13c03414618461c46fb60eff7e0ef"),
    0x28454: bytes.fromhex(
        "21465046f0f7faed3a6c4ff0ff3338695946d4682244f0f70cef"
    ),
    # ImagePainter's primary IImagePainter base is at object+0: the concrete
    # vtable+0x08 is stored at +0, while the secondary -12 thunk vptr is stored
    # at +0x0c. The final 16-byte zero store begins at +0x14 and reaches +0x23,
    # proving a minimum 0x24-byte footprint inside the plugin's conservative
    # 0x40-byte allocation. The deleting destructor restores both vptrs,
    # releases owned state, then reaches its operator-delete PLT call.
    0x28B74: bytes.fromhex("03f1080228332260e360"),
    0x28B82: bytes.fromhex("c0ef500004f11403204643f98f0af8bd"),
    0x29948: bytes.fromhex(
        "f8b50446564d00af564b7d44066aeb5803f1080228330260c3608eb1"
    ),
    0x299E8: bytes.fromhex("2046eef7a8ef2046f8bd"),
    # A stock ImagePainter is not usable until it owns the live ImageLoader.
    # setImageLoader copies the incoming raw pointer to painter+0x1c and its
    # strong control block to painter+0x20. createCanvas then reads +0x1c and
    # unconditionally calls ImageLoader::getEditor().
    0x28A64: bytes.fromhex("91e84800056ac361"),
    0x28AB8: bytes.fromhex("2662f8bd"),
    0x28BC0: bytes.fromhex("c96907f11800f0f722ed"),
    # EditorWidget::setEngine builds the real 0x38-byte ImageLoader in an
    # inplace control block and stores the resulting shared_ptr at
    # EditorWidget+0x20/+0x24. Its destructor later loads +0x24 and performs
    # the strong-reference decrement, proving this is an owning shared_ptr.
    0x1E86C: bytes.fromhex("636a2062c4f82490"),
    0x1D1A8: bytes.fromhex(
        "666a8eb1784beb58002b50d0331dbff35f8f53e8002f511e43e800100028f8d1"
        "012abff35f8f76d0"
    ),
}

# The notebook-open restore hook runs after IInkNotePadWidget::renderVolume.
# Pin the only PLT call site (so nh_dlhook can intercept it), both setupVolume
# paths, the setupVolume active-tool call, and the alternate path's later
# virtual setPart call. These prove the wrapper runs after the live editor,
# selected part, backend, and concrete tool are coherent.
PINNED_IINKNOTE_INSTRUCTIONS = {
    0x4DA82: bytes.fromhex("e7f75eed"),
    0x6289C: bytes.fromhex("d4f77aea"),
    0x66A92: bytes.fromhex("d1f74aec"),
    0x66ED8: bytes.fromhex("d1f726ea"),
    0x66F18: bytes.fromhex("b047"),
    # The eraser branch constructs tools {1,2} and skips brush sections; the
    # pen branch appends size/color enums {0,1}. The ctor, row, and both setter
    # calls go through PLT/GOT and are therefore valid nh_dlhook seams.
    0x70D3A: bytes.fromhex("e1b1"),
    0x70D4A: bytes.fromhex("0122"),
    0x70D62: bytes.fromhex("0223"),
    0x70D74: bytes.fromhex("5de0"),
    0x70E14: bytes.fromhex("0023"),
    0x70E20: bytes.fromhex("0123"),
    0x70EAC: bytes.fromhex("00964a46514607f108038046c4f7e2ee"),
    # Immediately after constructing the stock tool-menu controller, the
    # toolbar calls virtual slot +0x34 (AbstractNickelMenuController::show).
    # The layer popup mirrors that show's anchor/position sequence below.
    0x70EB8: bytes.fromhex("c4f7e2eed8f8003040465b6b9847"),
    0x74BAC: bytes.fromhex("283000afc1f772ed"),
    0x74D76: bytes.fromhex("2046c3f7b2ea"),
    # createBrushSizeRow forwards the title to QObject::setObjectName on the
    # button-row container. The plugin must preserve that stock identity and
    # rename only the separate section QLabel.
    0x75028: bytes.fromhex("21465046c3f758eb"),
    0x7515A: bytes.fromhex("b8f1050f"),
    0x756CA: bytes.fromhex("5a4651462046c1f748ec"),
    0x756FA: bytes.fromhex("a16c2046c2f7f0ed"),
    # Eraser tools 1/2 bypass pen/highlighter theme-size application, and both
    # use the default five-value UI table. Engine radius remains plugin-owned.
    0x62210: bytes.fromhex("431e0646012b16d9"),
    0x7695C: bytes.fromhex("38b103280bd0"),
    # Physical stylus inversion loads the saved eraser enum from widget+0xb8
    # and reaches exported setActiveTool through this unique PLT call. It does
    # not visit IInkToolMenuController, so the plugin passively synchronizes
    # the native popup index after this exact return only.
    0x622EC: bytes.fromhex("b9f1010f00f0a880"),
    0x62444: bytes.fromhex("d4f8b8102046d4f7a4ec59e7"),
    # setActiveTool publishes the live IInkTool at widget+0xac. The deferred
    # eraser-state replay reads that exact field after the firmware event stack
    # unwinds, and only acts when it is Object/Brush Eraser (1/2).
    0x6219C: bytes.fromhex("c0f8ac10"),
    # setEraserTool maps tool 1 -> policy 0 and tool 2 -> policy 1 through
    # clz(tool-2)>>5, then retains the selected eraser enum at widget+0xb8.
    0x6084E: bytes.fromhex("a2f10208"),
    0x60858: bytes.fromhex("b8fa88f8"),
    0x60860: bytes.fromhex("4fea5818"),
    0x608E0: bytes.fromhex("c6f8b830"),
    # Native layer-selector rows reuse the exact IInkToolMenuWidget surface.
    # Stock loadView proves the 0x58 allocation/parented constructor, then the
    # row selection setter and the +0x44 GestureReceiver registration. The
    # plugin never feeds a synthetic layer value through setTool().
    0x7551E: bytes.fromhex("5820d3f80080c0f776ea51460546c0f78aee"),
    0x75580: bytes.fromhex("2846b1fa81f14909c2f7deed"),
    0x75594: bytes.fromhex("05f144012046c1f7ccee"),
    0x76580: bytes.fromhex("436d80b400af5868bd465df8047bc1f7bbbf"),
    # IInkNotePadWidget owns its child EditorWidget through a guarded Qt
    # pointer: construction stores the guard/control at +0x44 and the raw
    # EditorWidget at +0x48. The stock constructor checks the guard's live
    # strong reference before loading +0x48 and calling EditorWidget::setEngine
    # through vslot +0xd0.
    # The same constructor stores the owned BackgroundWidget at +0x90, and
    # setBackgroundType loads that exact field before forwarding to it. These
    # two sites pin the borrowed accessor used by cover editing and its menu.
    0x60ABA: bytes.fromhex("c4f89060"),
    0x5BA08: bytes.fromhex("d0f89000"),
    0x60FD6: bytes.fromhex("c4f84490c4f84880"),
    0x61004: bytes.fromhex(
        "636c002b00f051855b68002b00f04d85a06c9be80a000268d2f8d020"
    ),
    # Once setEngine returns, the notebook constructor reads the guarded live
    # Editor identity from EditorWidget+0x18/+0x1c and mirrors it to the already
    # pinned IInkNotePadWidget+0x3c/+0x40 fields. This is the runtime identity
    # check used before borrowing EditorWidget's strong ImageLoader owner.
    0x6109E: bytes.fromhex("a36cda69d3f818b0ba62002a"),
    0x610D4: bytes.fromhex("c4f83cb0"),
    0x61112: bytes.fromhex("be6a20469fedcc0a2664"),
}

EXPECTED_NICKEL = {
    "_ZN22AbstractMenuController15addWidgetActionEP5QMenuP7QWidgetP7QObjectPKcbbb": 0xB3CE80,
    "_ZN22AbstractMenuController14grabTapGestureEP15GestureReceiver": 0xB3CFAC,
    "_ZN22AbstractMenuController10tapGestureEP15GestureReceiverP10TapGesture": 0xB3D00C,
    "_ZN15NickelTouchMenuC1EP7QWidget18DecorationPosition": 0xED6788,
    "_ZN15NickelTouchMenu12setAlignmentE6QFlagsIN2Qt13AlignmentFlagEE": 0xED6A68,
    "_ZN9TouchMenu28setCustomPopupPositionOffsetERK6QPoint": 0x11150A8,
    "_ZNK28AbstractNickelMenuController15popupFromWidgetEv": 0xEB4F94,
    "_ZN15NickelTouchMenu13popupPositionEP7QWidget": 0xED7248,
    "_ZNK7Content5getIdEv": 0x953D84,
    "_ZN15FeatureSettings18excludeSyncFoldersEv": 0xA04650,
    "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb": 0xA6EC74,
    "_ZN22N3PowerWorkflowManager13showSleepViewEv": 0xF3EB98,
    "_ZN22N3PowerWorkflowManager16showPowerOffViewEv": 0xF3EA54,
    "_ZN19PowerViewController13onCoverLoadedERK6QImage": 0xF425B4,
    "_ZN19PowerViewController11updateCoverEv": 0xF430B0,
    "_ZN19PowerViewController19updateReadingStatusEv": 0xF42798,
    "_ZN25FullScreenDragonPowerView19setInfoPanelVisibleEb": 0xF3A114,
    "_ZN19N3SettingsPowerViewC1EP7QWidget": 0x10455DC,
    "_ZN23SettingItemWithCheckBoxC1EP7QWidget": 0x1065824,
    "_ZN23SettingItemWithDropDownC1EP7QWidget": 0x106599C,
    "_ZN23SettingItemWithCheckBox7setTextERK7QString": 0x10653A0,
    "_ZN23SettingItemWithCheckBox10setCheckedEb": 0x10653C0,
    "_ZN15SettingItemBase8setLabelERK7QString": 0x10652C0,
    "_ZNK23SettingItemWithDropDown8dropDownEv": 0x10653F8,
    "_ZN24MultiSelectTouchDropDown7addItemERK7QStringRK8QVariantRK7QLocaleb": 0x10DE390,
    "_ZN13TouchDropDown15setCurrentIndexEi": 0x1113230,
}

EXPECTED_NICKEL_SIZES = {
    "_ZNK7Content5getIdEv": 0x5A,
    "_ZN15FeatureSettings18excludeSyncFoldersEv": 0xB8,
    "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb": 0x8A0,
}

EXPECTED_NICKEL_SHA256 = (
    "577235f308b4dea1c150caf87a376ecf34c8ecab04aacee0806feddd9e268744"
)

EXPECTED_NICKEL_JUMP_SLOTS = {
    "_ZN13VolumeManager20removeCommonBookDataERK6DeviceR6Volumeb": 0x16B0470,
    "_ZN15FeatureSettings18excludeSyncFoldersEv": 0x16BB1F0,
    "_ZN22N3PowerWorkflowManager16showPowerOffViewEv": 0x16AC9D8,
    "_ZN19N3SettingsPowerViewC1EP7QWidget": 0x16AEF90,
    "_ZN19PowerViewController13onCoverLoadedERK6QImage": 0x16B1C38,
    "_ZN19PowerViewController11updateCoverEv": 0x16BCB5C,
    "_ZN22N3PowerWorkflowManager13showSleepViewEv": 0x16B5698,
    "_ZN19PowerViewController19updateReadingStatusEv": 0x16BB524,
    "_ZN25FullScreenDragonPowerView19setInfoPanelVisibleEb": 0x16BD7B8,
}

PINNED_NICKEL_INSTRUCTIONS = {
    # loadView calls updateCover through its PLT entry immediately before the
    # stock reading-status update. This is the reliable notebook-image seam:
    # the hook can apply the captured QImage and keep the stale book status
    # suppressed for the following call.
    0xF43A9C: bytes.fromhex("204665f70cce"),
    # updateCover stores onCoverLoaded as a direct C++ member-function pointer
    # in a QSlotObject, and that dispatcher ultimately branches through r2.
    # It therefore bypasses onCoverLoaded's otherwise valid PLT jump slot.
    0xF43430: bytes.fromhex(
        "dff828250021dff8283510205af802203964fa635af80330b9647b64"
    ),
    0xF43F46: bytes.fromhex(
        "c8688968c4074bbf401002eb600014580a4644bf801862585968bd4690bc1047"
    ),
    # PowerViewController's generated metacall dispatch loads the QImage
    # reference from Qt's argv array, restores the frame, and tail-branches
    # through onCoverLoaded's PLT entry. Keep that fallback hook pinned, but
    # updateCover's actual imageReady connection uses the direct QSlotObject
    # path proved above, so notebook injection happens at updateCover instead.
    # Binary Ninja address is ELF VMA +0x10000.
    0xF4304A: bytes.fromhex("5968bd46b0bc43f7309c"),
    # Native power checkboxes are two-column SettingItem rows. retranslateUi
    # first sends the full setting name to SettingItemBase::setLabel, then
    # sends only "On" to SettingItemWithCheckBox::setText. The plugin mirrors
    # this exact split so its checkbox aligns with the three stock rows.
    # Binary Ninja address is ELF VMA +0x10000.
    0x1046758: bytes.fromhex(
        "5ff624ce2146304635f65ecc78680368"
    ),
    0x104678E: bytes.fromhex(
        "5ff60ace214630464ef6b2cb78680368"
    ),
    # Library asset visibility. FeatureSettings reads the user's existing
    # ExcludeSyncFolders value; findFilesToParse reaches it through this exact
    # PLT/GOT call, splits the result on a one-byte separator, and later wraps
    # every expression in the stock 22-byte ^/mnt/(sd|onboard)/%1$ anchor.
    # Excluding an already indexed directory makes its rows stale. Pin the
    # narrower removeCommonBookData seam: r3 is the source-delete flag, the
    # zero test branches into the backing-file block only when it is true,
    # and that block strips file:// then calls QFile::remove. The plugin forces
    # false only for exact .kobo/custom content-ID prefixes.
    0xA04650: bytes.fromhex(
        "2de9f04306462a488db08846122100af784407f110045ef4a4ee"
    ),
    0x89B518: bytes.fromhex(
        "09f654e8dff8380a21467844c7f544effd6a012207f13c04c7f8b0000092"
        "002320462a46414603f6eaee"
    ),
    0x89B966: bytes.fromhex(
        "1621786ac7f522edfd6a07f14c08c7f8b0000023adf800904046ba6a2946"
        "d4f5ece907f18c06012230464146dbf586ed"
    ),
    0xA6EC74: bytes.fromhex(
        "2de9f04f99b002af15463b61f8607961"
    ),
    0xA6ED50: bytes.fromhex(
        "3b69002b40f0c880"
    ),
    0xA6EEF0: bytes.fromhex(
        "9847dff8d0150123002220467944f8f736e180462046"
    ),
    0xA6EF44: bytes.fromhex(
        "4946404632f4a4ef4946204628f45cea20460ef410e810b1484601f46ee9"
    ),
    0xA6F538: bytes.fromhex(
        "4b4640462246314612f4eaeb01222146"
    ),
    # popupFromWidget() dereferences the controller's QPointer at +0x18/+0x1c
    # and returns the exact toolbar widget that opened the stock menu.
    0xEB4F94: bytes.fromhex(
        "836980b400af33b15b6823b1c069bd465df8047b70471846bd465df8047b7047"
    ),
    # Stock show() polishes the TouchMenu, resolves popupFromWidget(), calls
    # the menu's popupPosition(QWidget*) vslot (+0xd0), then QMenu::popup().
    # The plugin keeps a modal exec only for controller lifetime, but uses the
    # same anchor and QPoint returned by this sequence.
    0xEB5A1C: bytes.fromhex(
        "4846ddf792cb2a68804bd9f80010526c7b44d1f8d0409a4254d1aa6912b15268"
        "02b1ea6949464046a047414648460022d7f7fecc"
    ),
    # QPoint is returned through the ARM aggregate-result pointer in r0;
    # NickelTouchMenu is r1 and the toolbar anchor QWidget is r2.
    0xED7248: bytes.fromhex(
        "2de9f04f9fb002af07f140030c461d46fb610b68804628469346"
    ),
    # Preserve the exact controller/secondary-base call shape used to grab
    # Kobo's tap gesture and delegate it back to AbstractMenuController.
    0xB3CFAC: bytes.fromhex(
        "2de9f0410646124c00af0d467c44e1b1104a6ff0010310492846a25861586bf7"
    ),
    # The modal ControllerMenuViewGuard mirrors the QPointer<QWidget> at
    # controller+0x10/+0x14 which base tapGesture reads. On GestureFinished it
    # maps the global position into that exact QMenu, calls actionAt(), and
    # activates the resolved QAction.
    0xB3D054: bytes.fromhex("d8f8103013b15b68002b45d1"),
    0xB3D084: bytes.fromhex("d8f8103019461bb1596809b1d8f81410"),
    0xB3D0BE: bytes.fromhex(
        "2946504640f748e221464846524670f774e34946204649f7fee40028b6d000213df732e6"
    ),
}


def symbols(path: pathlib.Path) -> dict[str, int]:
    result = subprocess.run(
        ["nm", "-D", "--defined-only", str(path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    found: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        try:
            address = int(fields[0], 16)
        except ValueError:
            continue
        found[fields[-1]] = address
    return found


def symbol_sizes(path: pathlib.Path) -> dict[str, int]:
    result = subprocess.run(
        ["nm", "-D", "-S", "--defined-only", str(path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    found: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        try:
            size = int(fields[1], 16)
        except ValueError:
            continue
        found[fields[-1]] = size
    return found


def jump_slots(path: pathlib.Path) -> dict[str, int]:
    """Return dynamic R_ARM_JUMP_SLOT relocation offsets by symbol name."""
    result = subprocess.run(
        ["objdump", "-R", str(path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    found: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 3 or fields[1] != "R_ARM_JUMP_SLOT":
            continue
        try:
            offset = int(fields[0], 16)
        except ValueError:
            continue
        found[fields[2].split("@", 1)[0]] = offset
    return found


def load_segments(path: pathlib.Path) -> list[tuple[int, int, int]]:
    """Return (vaddr, file_offset, filesz) for each PT_LOAD in a 32-bit LE ELF."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError(f"not a 32-bit little-endian ELF: {path}")
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    segments: list[tuple[int, int, int]] = []
    for i in range(e_phnum):
        base = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz = struct.unpack_from(
            "<IIIII", data, base
        )
        if p_type == 1:  # PT_LOAD
            segments.append((p_vaddr, p_offset, p_filesz))
    return segments


def read_word(data: bytes, segments: list[tuple[int, int, int]], vma: int) -> int:
    for vaddr, offset, filesz in segments:
        if vaddr <= vma < vaddr + filesz:
            file_off = vma - vaddr + offset
            return struct.unpack_from("<I", data, file_off)[0]
    raise ValueError(f"VMA 0x{vma:x} is not in any PT_LOAD segment")


def read_bytes(
    data: bytes,
    segments: list[tuple[int, int, int]],
    vma: int,
    length: int,
) -> bytes:
    for vaddr, offset, filesz in segments:
        if vaddr <= vma and vma + length <= vaddr + filesz:
            file_off = vma - vaddr + offset
            return data[file_off : file_off + length]
    raise ValueError(
        f"VMA range 0x{vma:x}..0x{vma + length:x} is not in one PT_LOAD segment"
    )


def verify_vtable_slots(path: pathlib.Path) -> list[str]:
    """Confirm each pinned vtable slot resolves to its expected function VMA."""
    data = path.read_bytes()
    segments = load_segments(path)
    problems: list[str] = []
    for vtable_vma, slots in VTABLE_SLOTS.items():
        # The pointer stored in objects (the vptr) is the vtable symbol + 8, so a
        # "slot +N" is the word at (vtable_vma + 8 + N).
        for slot, expected in slots.items():
            addr = vtable_vma + 8 + slot
            try:
                actual = read_word(data, segments, addr) & ~1  # drop Thumb bit
            except ValueError as exc:
                problems.append(str(exc))
                continue
            if actual != expected:
                problems.append(
                    f"vtable 0x{vtable_vma:x} slot +0x{slot:x}: "
                    f"0x{actual:x}, expected 0x{expected:x}"
                )
    return problems


def verify_instruction_bytes(path: pathlib.Path) -> list[str]:
    """Verify pinned firmware instruction and literal-data bytes."""
    data = path.read_bytes()
    segments = load_segments(path)
    problems: list[str] = []
    for vma, expected in PINNED_INSTRUCTIONS.items():
        try:
            actual = read_bytes(data, segments, vma, len(expected))
        except ValueError as exc:
            problems.append(str(exc))
            continue
        if actual != expected:
            problems.append(
                f"instruction at 0x{vma:x}: {actual.hex()}, "
                f"expected {expected.hex()}"
            )
    return problems


def verify_iinknote_instruction_bytes(path: pathlib.Path) -> list[str]:
    """Verify the hookability and ordering of notebook-open layer restore."""
    data = path.read_bytes()
    segments = load_segments(path)
    problems: list[str] = []
    for vma, expected in PINNED_IINKNOTE_INSTRUCTIONS.items():
        try:
            actual = read_bytes(data, segments, vma, len(expected))
        except ValueError as exc:
            problems.append(str(exc))
            continue
        if actual != expected:
            problems.append(
                f"libiinknote instruction at 0x{vma:x}: {actual.hex()}, "
                f"expected {expected.hex()}"
            )
    return problems


def verify_iinkuiref_instruction_bytes(path: pathlib.Path) -> list[str]:
    """Verify that PNG export format selection remains suffix-driven."""
    data = path.read_bytes()
    segments = load_segments(path)
    problems: list[str] = []
    for vma, expected in PINNED_IINKUIREF_INSTRUCTIONS.items():
        try:
            actual = read_bytes(data, segments, vma, len(expected))
        except ValueError as exc:
            problems.append(str(exc))
            continue
        if actual != expected:
            problems.append(
                f"libiinkuiref instruction at 0x{vma:x}: {actual.hex()}, "
                f"expected {expected.hex()}"
            )
    return problems


def verify_nickel_instruction_bytes(path: pathlib.Path) -> list[str]:
    """Verify native menu gesture registration in the pinned Nickel image."""
    data = path.read_bytes()
    segments = load_segments(path)
    problems: list[str] = []
    for vma, expected in PINNED_NICKEL_INSTRUCTIONS.items():
        try:
            actual = read_bytes(data, segments, vma, len(expected))
        except ValueError as exc:
            problems.append(str(exc))
            continue
        if actual != expected:
            problems.append(
                f"libnickel instruction at 0x{vma:x}: {actual.hex()}, "
                f"expected {expected.hex()}"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "library",
        nargs="?",
        type=pathlib.Path,
        default=pathlib.Path("extracted/rootfs/usr/local/Kobo/libiink.so"),
    )
    parser.add_argument(
        "--iinknote",
        type=pathlib.Path,
        default=pathlib.Path("extracted/rootfs/usr/local/Kobo/libiinknote.so"),
    )
    parser.add_argument(
        "--nickel",
        type=pathlib.Path,
        default=pathlib.Path("extracted/rootfs/usr/local/Kobo/libnickel.so.1.0.0"),
    )
    parser.add_argument(
        "--iinkuiref",
        type=pathlib.Path,
        default=pathlib.Path(
            "extracted/rootfs/usr/local/Kobo/libiinkuiref.so.1.0.0"
        ),
    )
    args = parser.parse_args()
    if not args.library.is_file():
        parser.error(f"library not found: {args.library}")
    if not args.iinknote.is_file():
        parser.error(f"library not found: {args.iinknote}")
    if not args.nickel.is_file():
        parser.error(f"library not found: {args.nickel}")
    if not args.iinkuiref.is_file():
        parser.error(f"library not found: {args.iinkuiref}")

    found = symbols(args.library)
    found_sizes = symbol_sizes(args.library)
    found_iinknote = symbols(args.iinknote)
    found_nickel = symbols(args.nickel)
    found_nickel_sizes = symbol_sizes(args.nickel)
    found_nickel_jump_slots = jump_slots(args.nickel)
    found_iinkuiref = symbols(args.iinkuiref)
    failures: list[str] = []
    actual_nickel_sha256 = hashlib.sha256(args.nickel.read_bytes()).hexdigest()
    if actual_nickel_sha256 != EXPECTED_NICKEL_SHA256:
        failures.append(
            "libnickel SHA-256 mismatch: "
            f"{actual_nickel_sha256}, expected {EXPECTED_NICKEL_SHA256}"
        )
    for name, expected in EXPECTED.items():
        actual = found.get(name)
        if actual is None:
            failures.append(f"missing {name}")
        elif actual != expected:
            failures.append(
                f"VMA mismatch {name}: 0x{actual:x}, expected 0x{expected:x}"
            )
    for name, expected in VTABLE_SIZES.items():
        actual = found_sizes.get(name)
        if actual is None:
            failures.append(f"missing size for {name}")
        elif actual != expected:
            failures.append(
                f"size mismatch {name}: {actual}, expected {expected}"
            )
    for name, expected in EXPECTED_IINKNOTE.items():
        actual = found_iinknote.get(name)
        if actual is None:
            failures.append(f"missing {name}")
        elif actual != expected:
            failures.append(
                f"VMA mismatch {name}: 0x{actual:x}, expected 0x{expected:x}"
            )
    for name, expected in EXPECTED_NICKEL.items():
        actual = found_nickel.get(name)
        if actual is None:
            failures.append(f"missing {name}")
        elif actual != expected:
            failures.append(
                f"VMA mismatch {name}: 0x{actual:x}, expected 0x{expected:x}"
            )
    for name, expected in EXPECTED_NICKEL_SIZES.items():
        actual = found_nickel_sizes.get(name)
        if actual is None:
            failures.append(f"missing libnickel size for {name}")
        elif actual != expected:
            failures.append(
                f"libnickel size mismatch {name}: {actual}, expected {expected}"
            )
    for name, expected in EXPECTED_NICKEL_JUMP_SLOTS.items():
        actual = found_nickel_jump_slots.get(name)
        if actual is None:
            failures.append(f"missing libnickel R_ARM_JUMP_SLOT for {name}")
        elif actual != expected:
            failures.append(
                f"libnickel jump-slot mismatch {name}: "
                f"0x{actual:x}, expected 0x{expected:x}"
            )
    for name, expected in EXPECTED_IINKUIREF.items():
        actual = found_iinkuiref.get(name)
        if actual is None:
            failures.append(f"missing {name}")
        elif actual != expected:
            failures.append(
                f"VMA mismatch {name}: 0x{actual:x}, expected 0x{expected:x}"
            )

    failures.extend(verify_vtable_slots(args.library))
    failures.extend(verify_instruction_bytes(args.library))
    failures.extend(verify_iinknote_instruction_bytes(args.iinknote))
    failures.extend(verify_nickel_instruction_bytes(args.nickel))
    failures.extend(verify_iinkuiref_instruction_bytes(args.iinkuiref))

    if failures:
        print("Layer ABI verification FAILED", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    slot_count = sum(len(slots) for slots in VTABLE_SLOTS.values())
    print(
        "Layer ABI verified: "
        f"{len(EXPECTED) + len(EXPECTED_IINKNOTE) + len(EXPECTED_NICKEL) + len(EXPECTED_IINKUIREF)} "
        f"symbols, {len(VTABLE_SIZES)} vtable size, and {slot_count} "
        f"vtable slots plus "
        f"{len(PINNED_INSTRUCTIONS) + len(PINNED_IINKNOTE_INSTRUCTIONS) + len(PINNED_NICKEL_INSTRUCTIONS) + len(PINNED_IINKUIREF_INSTRUCTIONS)} "
        f"instruction pins and {len(EXPECTED_NICKEL_JUMP_SLOTS)} jump slots "
        "match pinned libraries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
