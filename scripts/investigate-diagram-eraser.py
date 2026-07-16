#!/usr/bin/env python3
"""Replayable proof of Kobo's Diagram erase and pen layer-routing paths."""

from __future__ import annotations

import json
import pathlib
import struct

from bnremote_api import connect


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_FILE = (
    REPOSITORY_ROOT / "extracted/rootfs/usr/local/Kobo/libiink.so"
).resolve()
EXPECTED_SHA256 = "0dc02118b9b763450c228da1e632dadbcce5cfd2acb41a3ca76a2472aed71ac6"
# Binary Ninja addresses. Firmware ELF VMAs are these values minus 0x10000.
FUNCTIONS = {
    0x738B7C: "atk::diagram::DiagramActiveBackend::getTool",
    0xA88668: "atk::core::Eraser::penDown",
    0xA86DA8: "atk::core::Eraser::penUp",
    0xA87B20: "atk::core::Eraser::penMove(vector)",
    0x76B73C: "atk::diagram::DiagramEraser::updateSelection",
    0x76C224: "atk::diagram::DiagramEraser::eraseSelection",
    0x716508: "atk::diagram::Diagram::removeEraseSelection",
    0x7C0E6C: "atk::diagram::DiagramPen::restrictToLayer",
    0x7BF1E4: "atk::diagram::DiagramPen::penDown",
    0xA93C00: "atk::core::SmartPen::penDown",
    0xA893EC: "atk::core::Pen::penDown",
    0xAAB710: "atk::core::InkSampler::setStrokeLayer",
    0xA85CE4: "atk::core::Eraser::updateSelection",
    0xA85A94: "atk::core::Eraser::setEraserPolicy",
    0xA85AA4: "atk::core::Eraser::eraserPolicy",
    0xA87470: "atk::core::Eraser::selectionFromPoints",
    0xA85FB0: "atk::core::Eraser::strokerPolygon",
    0xA8CE70: "atk::core::Selector::computeSelection",
    0x9F27B8: "atk::core::Selection::selectPolygon",
    0x9F2D70: "atk::core::Selection::adjustToStrokeBoundaries",
    0xB06E54: "myscript::document::PageSelection::adjustToStrokeBoundaries_",
    0xBBDBD8: "myscript::ink::InkSelection::adjustToStrokeBoundaries_",
    0x572838: "snt::DrawingEraser::selectionFromPoints",
    0x710C5C: "atk::diagram::Diagram::computeEraseSelectionRemovals",
}

INSTRUCTIONS = {
    # penDown clears the final Selection at Eraser+0xd0.
    0xA888EA: (bytes.fromhex("05f1d000"), "#0xd0"),
    # penDown loads/calls the concrete updateSelection vslot +0x7c.
    0xA88902: (bytes.fromhex("f66f"), "#0x7c"),
    0xA8890C: (bytes.fromhex("b047"), "blx"),
    # DiagramEraser delegates its transformed selection to core updateSelection.
    0x76BB20: (bytes.fromhex("1af3e0f8"), "#0xa85ce4"),
    # DiagramEraser::eraseSelection reads the same Eraser+0xd0 member.
    0x76C228: (bytes.fromhex("00f1d004"), "#0xd0"),
    # Core penMove loads +0x7c, calls it, and forwards its boolean to the
    # renderer update at +0x84. Returning the direct core-update result is
    # therefore required for a visible eraser/cut preview.
    0xA87C40: (bytes.fromhex("def87cb0"), "#0x7c"),
    0xA87C4C: (bytes.fromhex("d847"), "blx"),
    0xA87C84: (bytes.fromhex("d3f88430"), "#0x84"),
    # Core penUp calls virtual +0x80; DiagramEraser then forwards the exact
    # committed +0xd0 Selection to Diagram::removeEraseSelection.
    0xA86F0A: (bytes.fromhex("d3f88030"), "#0x80"),
    0xA86F0E: (bytes.fromhex("9847"), "blx"),
    0x76C284: (bytes.fromhex("aaf740f9"), "#0x716508"),
    # DiagramActiveBackend returns cached eraser kind 4 from backend+0x64,
    # while pen kinds 0-2 use the default cached DiagramPen at backend+0x5c.
    0x738C4E: (bytes.fromhex("736e"), "#0x64"),
    0x738C76: (bytes.fromhex("f36d"), "#0x5c"),
    # DiagramPen::restrictToLayer calls the base setter, locks shared Diagram
    # data, then assigns the same layer ID at data+0x60.
    0x7C0E76: (bytes.fromhex("d3f26ffa"), "#0xa94358"),
    0x7C0E7E: (bytes.fromhex("fbf74fff"), "#0x7bcd20"),
    0x7C0E88: (bytes.fromhex("6030"), "#0x60"),
    # Core Eraser's stock selection is polygon/Replace after deriving its
    # scaled width from Eraser+0xcc and +0x110.
    0xA8748C: (bytes.fromhex("d1ed337a"), "#0xcc"),
    0xA87492: (bytes.fromhex("d1ed448a"), "#0x110"),
    0xA874A8: (bytes.fromhex("67eea88a"), "vmul.f32"),
    0xA8764C: (bytes.fromhex("0023"), "#0"),
    # EraserPolicy is stored at Eraser+0xc4. ToolDispatcher maps the native
    # names "stroke" to 0 and "precise" to 1. Both stock DrawingEraser and
    # DiagramEraser expand policy-0 hits with the same 0.0f literal.
    0xA85A98: (bytes.fromhex("c0f8c410"), "#0xc4"),
    0xA85AA8: (bytes.fromhex("d0f8c400"), "#0xc4"),
    0x5D3998: (bytes.fromhex("0146"), "r1, r0"),
    0x5D399C: (bytes.fromhex("b2f07af0"), "#0xa85a94"),
    0x5D3B1E: (bytes.fromhex("0121"), "#1"),
    0x5D3B22: (bytes.fromhex("b1f0b7f7"), "#0xa85a94"),
    0x572848: (bytes.fromhex("d4f8c430"), "#0xc4"),
    0x572850: (bytes.fromhex("23b1"), "#0x57285c"),
    0x5728E2: (bytes.fromhex("9fed650a"), "s0"),
    0x5728E6: (bytes.fromhex("4bf277f1"), "#0xbbdbd8"),
    0x76B7B4: (bytes.fromhex("d3f8c430"), "#0xc4"),
    0x76B7BA: (bytes.fromhex("40f0ae81"), "#0x76bb1a"),
    0x76BA00: (bytes.fromhex("9fedaf0a"), "s0"),
    0x76BA04: (bytes.fromhex("52f0e8f0"), "#0xbbdbd8"),
    0x9F2D82: (bytes.fromhex("14f167f8"), "#0xb06e54"),
    # Selector proves the required named-layer selection order and modes:
    # layer/Replace, then polygon/Intersection.
    0xA8CED0: (bytes.fromhex("0022"), "#0"),
    0xA8D07E: (bytes.fromhex("0223"), "#2"),
    # DiagramPen reaches SmartPen, which reaches core Pen. Core Pen loads
    # virtual restrictedLayer from slot +0x58 and passes it to setStrokeLayer.
    0x7BF41E: (bytes.fromhex("d4f2effb"), "#0xa93c00"),
    0xA93CF8: (bytes.fromhex("f5f778fb"), "#0xa893ec"),
    0xA894B0: (bytes.fromhex("936d"), "#0x58"),
    0xA894BE: (bytes.fromhex("22f027f9"), "#0xaab710"),
}

DATA_BYTES = {
    # Exact float literals consumed by the policy-0 whole-stroke paths.
    0x572A78: bytes.fromhex("00000000"),
    0x76BCC0: bytes.fromhex("00000000"),
    # Later Diagram removal expansion uses 0.7f, but only after iterating
    # semantic Diagram items; custom raw ink bypasses that item loop.
    0x7110AC: bytes.fromhex("3333333f"),
}

VTABLES = {
    "diagram_eraser": (
        0xD9EB30,
        {
            0x78: 0xA87470,
            0x7C: 0x76B73C,
            0x80: 0x76C224,
        },
    ),
    "diagram_pen": (
        0xD9FFB0,
        {
            0x3C: 0x7BF1E4,
            0x54: 0x7C0E6C,
            0x58: 0xA942F0,
        },
    ),
    "draft_active_backend": (
        0xDA0288,
        {
            0x2C: 0x738B7C,
        },
    ),
}


def main() -> None:
    report: dict[str, object] = {}
    with connect() as bn:
        session = bn.session()
        view = bn.current_view()
        status = view.wait_for_analysis(timeout=60, poll_interval=0.1)

        assert pathlib.Path(view.filename or "") == EXPECTED_FILE
        assert session.view_type == "ELF"
        assert view.architecture == "armv7"
        assert view.platform == "linux-armv7"
        assert session.raw_fingerprint["file_sha256"] == EXPECTED_SHA256
        # The bridge fingerprint intentionally includes the live view session
        # identity and changes when this ELF is reopened. Record and validate
        # its shape here; the stable target identity is the exact file SHA.
        assert len(session.bridge_fingerprint) == 64
        assert all(c in "0123456789abcdef" for c in session.bridge_fingerprint)
        assert not view.capabilities
        assert status.complete and not status.incomplete

        snapshots = view.fetch_functions(
            addresses=tuple(FUNCTIONS),
            fields=("signature", "calls"),
            projection="summary",
            limits={"callers": 0, "callees": 100, "tail_calls": 0},
        )
        functions = {}
        for snapshot in snapshots:
            assert snapshot.start in FUNCTIONS
            assert not snapshot.incomplete
            functions[hex(snapshot.start)] = {
                "expected": FUNCTIONS[snapshot.start],
                "symbol": snapshot.name,
                "architecture": snapshot.architecture,
            }

        instructions = {}
        for address, (expected_bytes, text_fragment) in INSTRUCTIONS.items():
            item = view.disassembly(address, count=1).instructions[0]
            assert item.address == address
            assert item.data == expected_bytes
            assert item.architecture == "thumb2"
            assert item.architecture_source in {"basic_block", "function"}
            assert text_fragment in item.text
            instructions[hex(address)] = {
                "bytes": item.data.hex(),
                "text": item.text,
                "architecture_source": item.architecture_source,
            }

        data_bytes = {}
        for address, expected_bytes in DATA_BYTES.items():
            memory = view.read(address, len(expected_bytes))
            assert memory.complete and memory.data == expected_bytes
            data_bytes[hex(address)] = memory.data.hex()

        vtables = {}
        for name, (vtable, expected_entries) in VTABLES.items():
            entries = {}
            for slot, expected_address in expected_entries.items():
                address = vtable + 8 + slot
                memory = view.read(address, 4)
                assert memory.complete and len(memory.data) == 4
                actual_address = struct.unpack("<I", memory.data)[0] & ~1
                assert actual_address == expected_address
                entries[hex(slot)] = hex(actual_address)
            vtables[name] = entries

        report = {
            "file": str(EXPECTED_FILE),
            "sha256": EXPECTED_SHA256,
            "bridge_fingerprint": session.bridge_fingerprint,
            "analysis_generation": status.generation,
            "capabilities": dict(view.capabilities),
            "functions": functions,
            "instructions": instructions,
            "data_bytes": data_bytes,
            "vtables": vtables,
            "conclusion": (
                "DiagramEraser's stock update narrows through its semantic "
                "layout-group/item model, which can discard plugin-layer raw "
                "ink. For cnt.layer.* raw ink, core Eraser::updateSelection "
                "can directly union the verified layer/Replace then polygon/"
                "Intersection selection into Eraser+0xd0 and its boolean must "
                "be returned for renderer feedback. Core penUp virtually "
                "calls DiagramEraser +0x80, which removes that same selection. "
                "DiagramPen reaches core Pen, which consumes virtual "
                "restrictedLayer at physical pen-down and supplies it to "
                "InkSampler::setStrokeLayer; the guarded build reasserts the "
                "popup-selected ID immediately before that stock chain."
                " The native eraser policy is 0 for whole-stroke and 1 for "
                "precise erasing. Because the cnt.layer.* update path bypasses "
                "DiagramEraser's policy-0 semantic transform, it must expand "
                "the already layer-scoped polygon hit with "
                "Selection::adjustToStrokeBoundaries(0.0f) before committing "
                "through core updateSelection. That adjustment mutates the "
                "existing selection and performs no new page/layer hit-test."
            ),
        }

    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
