#!/usr/bin/env python3
"""Replayable proof of Kobo's full-export/current-page argument flow."""

from __future__ import annotations

import argparse
import json
import pathlib

from bnremote_api import connect


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_FILE = (
    REPOSITORY_ROOT / "binary-ninja/libiinknote-4.38.23697.bndb"
).resolve()
EXPECTED_SHA256 = (
    "f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea"
)
WIDGET_EXPORT_FUNCTION = 0x74A30
EXPORT_CONTROLLER_CONSTRUCTOR = 0x46508

# IInkNotePadWidget::exportNotebook(bool) saves the requested full-notebook
# flag, obtains the current part index independently, then passes both values
# to IInkExportNoteController(Volume const&, bool, int).
ARGUMENT_FLOW = {
    0x74A7A: (bytes.fromhex("0c46"), "mov     r4, r1", "save_full_notebook"),
    0x74AE8: (bytes.fromhex("9047"), "blx     r2", "get_current_part_index"),
    0x74AEA: (bytes.fromhex("8346"), "mov     r11, r0", "save_current_part_index"),
    0x74B10: (bytes.fromhex("5b46"), "mov     r3, r11", "pass_current_part_index"),
    0x74B12: (bytes.fromhex("2246"), "mov     r2, r4", "pass_full_notebook"),
    0x74B1A: (bytes.fromhex("d1f7f6ec"), "blx     #0x46508", "construct_export_controller"),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--target",
        default=str(EXPECTED_FILE),
        help="Exact BN Remote instance ID, filename, or full path",
    )
    args = parser.parse_args()

    with connect(target=args.target) as bn:
        session = bn.session()
        view = bn.current_view()
        status = view.wait_for_analysis(timeout=60, poll_interval=0.1)

        assert pathlib.Path(view.filename or "").resolve() == EXPECTED_FILE
        assert session.filename == str(EXPECTED_FILE)
        assert session.view_type == "ELF"
        assert view.architecture == "armv7"
        assert view.platform == "linux-armv7"
        assert session.raw_fingerprint["file_sha256"] == EXPECTED_SHA256
        assert session.instance_id == bn.instance.instance_id
        assert session.process_id == bn.instance.process_id
        assert not view.capabilities
        assert status.complete and not status.incomplete

        widget_export = view.get_function_at(WIDGET_EXPORT_FUNCTION).fetch(
            fields=("signature", "calls", "metrics"),
            projection="summary",
            limits={"callers": 30, "callees": 100, "tail_calls": 20},
        )
        constructor = view.get_function_at(EXPORT_CONTROLLER_CONSTRUCTOR).fetch(
            fields=("signature",),
            projection="summary",
        )
        assert widget_export.start == WIDGET_EXPORT_FUNCTION
        assert "IInkNotePadWidget" in widget_export.name
        assert "exportNotebook" in widget_export.name
        assert "IInkExportNoteController" in constructor.name
        assert not widget_export.incomplete
        assert not constructor.incomplete

        flow: dict[str, object] = {}
        for address, (expected, text_fragment, role) in ARGUMENT_FLOW.items():
            instruction = view.disassembly(address, count=1).instructions[0]
            assert instruction.address == address
            assert instruction.data == expected
            assert instruction.architecture == "thumb2"
            assert instruction.architecture_source in {"basic_block", "function"}
            assert text_fragment in instruction.text
            flow[hex(address)] = {
                "role": role,
                "bytes": instruction.data.hex(),
                "text": instruction.text,
            }

        report = {
            "session": {
                "file": str(EXPECTED_FILE),
                "sha256": EXPECTED_SHA256,
                "instance_id": session.instance_id,
                "process_id": session.process_id,
                "analysis_generation": status.generation,
                "capabilities": dict(view.capabilities),
            },
            "widget_export": {
                "start": hex(widget_export.start),
                "name": widget_export.name,
                "controller_constructor": constructor.name,
                "argument_flow": flow,
            },
            "conclusion": (
                "Kobo passes the full-notebook boolean and the current part index "
                "as independent IInkExportNoteController constructor arguments. "
                "The current index is not normalized to zero for full exports, so "
                "an armed range must not require partIndex == 0."
            ),
        }

    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
