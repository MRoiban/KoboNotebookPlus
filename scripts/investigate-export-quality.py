#!/usr/bin/env python3
"""Replayable proof for selectable Kobo MyScript export quality patches."""

from __future__ import annotations

import argparse
import json
import pathlib

from bnremote_api import connect


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_FILES = {
    "stock": (REPOSITORY_ROOT / "binary-ninja/iink-4.38.23697.bndb").resolve(),
    "best": (
        REPOSITORY_ROOT
        / "mods/custom-notebook-templates/package/iink-export-best"
    ).resolve(),
    "fast": (
        REPOSITORY_ROOT
        / "mods/custom-notebook-templates/package/iink-export-fast"
    ).resolve(),
}
EXPECTED_SHA256 = {
    "stock": "98bb2032f47f75bd1c3796f3cda2f016edc43a57435007035d6b5b8657dd7bb1",
    "best": "f8ddf410f153b2c670201a43d2961746a229939b6e129a57c0ffe77545f3fac2",
    "fast": "2672624eeae69f71fd614556df5a4e5600829303c611b3f5471fe9e9c828b57e",
}
EXPORT_FUNCTION = 0x1F464

# Every dimension instruction in MyScriptExporterService::exportFile. PDF uses
# 2480x3508; ZIP and single-image output use 2480x3307. The fast exporter
# halves raster/render dimensions, but deliberately preserves the PDF draw
# destination at 2480x3508 so QPainter fills the normal A4 page.
DIMENSION_INSTRUCTIONS = {
    0x1FCD0: ("pdf_render_height", bytes.fromhex("40f6b452"), "#0xdb4"),
    0x1FCD4: ("pdf_render_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
    0x1FE00: ("pdf_destination_width", bytes.fromhex("4ff41b60"), "#0x9b0"),
    0x1FE04: ("pdf_destination_height", bytes.fromhex("40f6b45e"), "#0xdb4"),
    0x1FFC8: ("pdf_raster_height", bytes.fromhex("40f6b452"), "#0xdb4"),
    0x1FFD0: ("pdf_raster_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
    0x20266: ("image_height", bytes.fromhex("40f6eb42"), "#0xceb"),
    0x2026A: ("image_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
    0x204A0: ("image_height", bytes.fromhex("40f6eb42"), "#0xceb"),
    0x204A8: ("image_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
    0x2080E: ("image_height", bytes.fromhex("40f6eb42"), "#0xceb"),
    0x20812: ("image_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
    0x2092A: ("image_height", bytes.fromhex("40f6eb42"), "#0xceb"),
    0x20932: ("image_width", bytes.fromhex("4ff41b61"), "#0x9b0"),
}

SCALE_LITERALS = {
    0x20040: bytes.fromhex("00001b45"),
    0x202CC: bytes.fromhex("00001b45"),
    0x20BB4: bytes.fromhex("00001b45"),
}

# The request entry and successful completion paths both arm the exporter's
# idle timer for 120 seconds. Export-only variants shorten these two starts;
# the constructor retains the stock timeout so D-Bus activation cannot race.
IDLE_TIMER_INSTRUCTIONS = {
    0x1F48A: (bytes.fromhex("4df2c041"), "#0xd4c0"),
    0x1F48E: (bytes.fromhex("c0f20101"), "#1"),
    0x2061A: (bytes.fromhex("4df2c041"), "#0xd4c0"),
    0x20620: (bytes.fromhex("c0f20101"), "#1"),
}

FAST_DIMENSION_INSTRUCTIONS = {
    0x1FCD0: ("pdf_render_height", bytes.fromhex("40f2da62"), "#0x6da"),
    0x1FCD4: ("pdf_render_width", bytes.fromhex("40f2d841"), "#0x4d8"),
    0x1FE00: ("pdf_destination_width", bytes.fromhex("4ff41b60"), "#0x9b0"),
    0x1FE04: ("pdf_destination_height", bytes.fromhex("40f6b45e"), "#0xdb4"),
    0x1FFC8: ("pdf_raster_height", bytes.fromhex("40f2da62"), "#0x6da"),
    0x1FFD0: ("pdf_raster_width", bytes.fromhex("40f2d841"), "#0x4d8"),
    0x20266: ("image_height", bytes.fromhex("40f27662"), "#0x676"),
    0x2026A: ("image_width", bytes.fromhex("40f2d841"), "#0x4d8"),
    0x204A0: ("image_height", bytes.fromhex("40f27662"), "#0x676"),
    0x204A8: ("image_width", bytes.fromhex("40f2d841"), "#0x4d8"),
    0x2080E: ("image_height", bytes.fromhex("40f27662"), "#0x676"),
    0x20812: ("image_width", bytes.fromhex("40f2d841"), "#0x4d8"),
    0x2092A: ("image_height", bytes.fromhex("40f27662"), "#0x676"),
    0x20932: ("image_width", bytes.fromhex("40f2d841"), "#0x4d8"),
}

PATCHED_IDLE_TIMER_INSTRUCTIONS = {
    0x1F48A: (bytes.fromhex("4df2c041"), "#0xd4c0"),
    0x1F48E: (bytes.fromhex("c0f20101"), "#1"),
    0x2061A: (bytes.fromhex("40f2e831"), "#0x3e8"),
    0x20620: (bytes.fromhex("c0f20001"), "#0"),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--variant",
        choices=tuple(EXPECTED_FILES),
        default="stock",
    )
    parser.add_argument(
        "--target",
        default=None,
        help="Exact BN Remote instance ID, filename, or full path",
    )
    args = parser.parse_args()
    expected_file = EXPECTED_FILES[args.variant]
    target = args.target or str(expected_file)
    dimension_instructions = (
        FAST_DIMENSION_INSTRUCTIONS
        if args.variant == "fast"
        else DIMENSION_INSTRUCTIONS
    )
    scale_literals = {
        address: bytes.fromhex("00009b44")
        for address in SCALE_LITERALS
    } if args.variant == "fast" else SCALE_LITERALS
    timer_instructions = (
        IDLE_TIMER_INSTRUCTIONS
        if args.variant == "stock"
        else PATCHED_IDLE_TIMER_INSTRUCTIONS
    )

    with connect(target=target) as bn:
        session = bn.session()
        view = bn.current_view()
        status = view.wait_for_analysis(timeout=60, poll_interval=0.1)

        assert pathlib.Path(view.filename or "").resolve() == expected_file
        assert session.filename == str(expected_file)
        assert session.view_type == "ELF"
        assert view.architecture == "thumb2"
        assert view.platform == "linux-thumb2"
        assert session.raw_fingerprint["file_sha256"] == EXPECTED_SHA256[args.variant]
        assert session.instance_id == bn.instance.instance_id
        assert session.process_id == bn.instance.process_id
        assert not view.capabilities
        assert status.complete and not status.incomplete

        exporter = view.get_function_at(EXPORT_FUNCTION).fetch(
            fields=("signature", "calls", "metrics"),
            projection="summary",
            limits={"callers": 20, "callees": 100, "tail_calls": 20},
        )
        assert exporter.start == EXPORT_FUNCTION
        assert "MyScriptExporterService" in exporter.name
        assert not exporter.incomplete

        dimensions: dict[str, object] = {}
        for address, (role, expected, text_fragment) in dimension_instructions.items():
            instruction = view.disassembly(address, count=1).instructions[0]
            assert instruction.address == address
            assert instruction.data == expected
            assert instruction.architecture == "thumb2"
            assert instruction.architecture_source in {"basic_block", "function"}
            assert text_fragment in instruction.text
            dimensions[hex(address)] = {
                "role": role,
                "bytes": instruction.data.hex(),
                "text": instruction.text,
                "architecture_source": instruction.architecture_source,
            }

        scales: dict[str, str] = {}
        for address, expected in scale_literals.items():
            memory = view.read(address, len(expected))
            assert memory.complete and memory.data == expected
            scales[hex(address)] = memory.data.hex()

        timers: dict[str, object] = {}
        for address, (expected, text_fragment) in timer_instructions.items():
            instruction = view.disassembly(address, count=1).instructions[0]
            assert instruction.address == address
            assert instruction.data == expected
            assert instruction.architecture == "thumb2"
            assert instruction.architecture_source in {"basic_block", "function"}
            assert text_fragment in instruction.text
            timers[hex(address)] = {
                "bytes": instruction.data.hex(),
                "text": instruction.text,
                "architecture_source": instruction.architecture_source,
            }

        timer_xrefs = view.xrefs_to(0x1B27C, kind="code", page_size=100).take(100)
        exporter_timer_calls = sorted(
            record.address
            for record in timer_xrefs
            if record.function and record.function.start == EXPORT_FUNCTION
        )
        assert exporter_timer_calls == [0x1F49C, 0x20624]

        report = {
            "session": {
                "file": str(expected_file),
                "variant": args.variant,
                "sha256": EXPECTED_SHA256[args.variant],
                "instance_id": session.instance_id,
                "process_id": session.process_id,
                "view_type": session.view_type,
                "architecture": view.architecture,
                "platform": view.platform,
                "analysis_generation": status.generation,
                "capabilities": dict(view.capabilities),
            },
            "exporter": {
                "start": hex(exporter.start),
                "name": exporter.name,
                "dimension_instructions": dimensions,
                "scale_literals": scales,
                "idle_timer_instructions": timers,
                "timer_calls": [hex(address) for address in exporter_timer_calls],
            },
            "conclusion": (
                "All PDF, ZIP, PNG, and JPEG raster dimensions, the PDF draw "
                "destination, and their 2480.0 scale literals are explicit, "
                "bounded sites inside MyScriptExporterService::exportFile. A "
                "stock-resolution Best variant and half-resolution Fast variant "
                "that retains the stock PDF destination can share the exact "
                "D-Bus protocol. Keeping the request-entry timeout at 120 seconds "
                "protects in-flight QPdfWriter finalization, while shortening only "
                "the post-finalization idle timer makes the exporter-only process "
                "release its bus name promptly for the next quality selection."
            ),
        }

    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
