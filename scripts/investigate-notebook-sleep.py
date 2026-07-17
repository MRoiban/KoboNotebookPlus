#!/usr/bin/env python3
"""Replayable libnickel investigation for notebook sleep-screen routing."""

from __future__ import annotations

import argparse
import json
import pathlib
from collections.abc import Iterable

from bnremote_api import connect


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED_FILE = (
    REPOSITORY_ROOT / "extracted/rootfs/usr/local/Kobo/libnickel.so.1.0.0"
).resolve()
EXPECTED_SHA256 = "577235f308b4dea1c150caf87a376ecf34c8ecab04aacee0806feddd9e268744"

# Binary Ninja rebases this ELF by 0x10000. The runtime ELF VMAs pinned by the
# plugin are each address below minus 0x10000.
FUNCTIONS = {
    0xF4EB98: "N3PowerWorkflowManager::showSleepView",
    0xF4EA54: "N3PowerWorkflowManager::showPowerOffView",
    0xF525B4: "PowerViewController::onCoverLoaded",
    0xF52798: "PowerViewController::updateReadingStatus",
    0xF4A114: "FullScreenDragonPowerView::setInfoPanelVisible",
    0xF4A0D0: "FullScreenDragonPowerView::setImage",
    0xF51A74: "PowerViewController::PowerViewController",
    0xF52610: "PowerViewController::onCoverLoaded continuation 1",
    0xF526B8: "PowerViewController::onCoverLoaded continuation 2",
    0xF5306C: "PowerViewController::qt_metacall",
    0xF530B0: "PowerViewController::updateCover",
    0xF5398C: "PowerViewController::loadView",
    0xF53F10: "PowerViewController QImage slot-object dispatcher",
}

NAME_PATTERNS = (
    "N3PowerWorkflowManager",
    "PowerViewController",
    "FullScreenDragonPowerView",
)

RELEVANT_NAME_TERMS = (
    "loadView",
    "onCoverLoaded",
    "setImage",
    "setInfoPanelVisible",
    "showPowerOffView",
    "showSleepView",
    "updateCover",
    "updateReadingStatus",
)

PROOF_BYTES = {
    # loadView reaches updateCover through its PLT entry.
    0xF53A9C: bytes.fromhex("204665f70cce"),
    # updateCover loads and stores onCoverLoaded's direct member pointer in
    # the QSlotObject used for the imageReady connection.
    0xF53430: bytes.fromhex(
        "dff828250021dff8283510205af802203964fa635af80330b9647b64"
    ),
    # The QSlotObject dispatcher invokes that stored member pointer via r2.
    0xF53F46: bytes.fromhex(
        "c8688968c4074bbf401002eb600014580a4644bf801862585968bd4690bc1047"
    ),
}

IL_TERMS = (
    "call",
    "cover",
    "image",
    "power",
    "reading",
    "sleep",
    "view",
)


def identity(function: object | None) -> dict[str, object] | None:
    if function is None:
        return None
    return {
        "start": hex(function.start),
        "name": function.name,
        "architecture": function.architecture,
    }


def call_sites(snapshot: object) -> list[dict[str, object]]:
    return [
        {
            "call_address": hex(site.call_address),
            "target": hex(site.target),
            "target_function": identity(site.function),
            "tail_call": site.tail_call,
            "llil_operation": site.llil_operation,
        }
        for site in snapshot.calls.callees.items
    ]


def relevant_il(view: object, address: int) -> list[dict[str, object]]:
    function = view.get_function_at(address)
    instructions = function.il("hlil").instructions(
        page_size=500,
        projection="summary",
        fields=("operation", "expression_index", "address", "text"),
    ).take(2_000)
    selected = []
    for instruction in instructions:
        text = instruction.text
        lowered = text.lower()
        if instruction.operation.is_call or any(term in lowered for term in IL_TERMS):
            selected.append(
                {
                    "address": (
                        None if instruction.address is None else hex(instruction.address)
                    ),
                    "expression_index": instruction.expr_index,
                    "operation": instruction.operation.name,
                    "text": text,
                }
            )
    return selected


def xrefs(view: object, address: int) -> list[dict[str, object]]:
    records = view.xrefs_to(address, kind="code", page_size=500).take(2_000)
    return [
        {
            "address": hex(record.address),
            "kind": record.kind,
            "architecture": record.architecture,
            "function": identity(record.function),
            "target": None if record.target is None else hex(record.target),
        }
        for record in records
    ]


def named_functions(view: object, patterns: Iterable[str]) -> list[dict[str, object]]:
    found: dict[int, dict[str, object]] = {}
    for pattern in patterns:
        query = (
            view.function_query()
            .where(name_contains=pattern)
            .select("start", "name", "architecture")
            .page_size(100)
        )
        for snapshot in query.fetch_all(max_items=500):
            if snapshot.start < 0xF00000:
                continue
            if not any(term in snapshot.name for term in RELEVANT_NAME_TERMS):
                continue
            found[snapshot.start] = {
                "start": hex(snapshot.start),
                "name": snapshot.name,
                "architecture": snapshot.architecture,
            }
    return [found[address] for address in sorted(found)]


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

        snapshots = view.fetch_functions(
            addresses=tuple(FUNCTIONS),
            fields=("signature", "calls", "il"),
            projection="summary",
            limits={"callers": 100, "callees": 200, "tail_calls": 50},
        )
        functions = {}
        for snapshot in snapshots:
            assert snapshot.start in FUNCTIONS
            assert not snapshot.incomplete
            functions[hex(snapshot.start)] = {
                "expected": FUNCTIONS[snapshot.start],
                "symbol": snapshot.name,
                "architecture": snapshot.architecture,
                "callees": call_sites(snapshot),
                "relevant_hlil": relevant_il(view, snapshot.start),
            }

        report = {
            "session": {
                "file": str(EXPECTED_FILE),
                "sha256": EXPECTED_SHA256,
                "instance_id": session.instance_id,
                "process_id": session.process_id,
                "view_type": session.view_type,
                "architecture": view.architecture,
                "platform": view.platform,
                "analysis_generation": status.generation,
                "capabilities": dict(view.capabilities),
            },
            "named_power_functions": named_functions(view, NAME_PATTERNS),
            "functions": functions,
            "on_cover_loaded_xrefs": xrefs(view, 0xF525B4),
            "proof_bytes": {},
            "conclusion": (
                "PowerViewController::updateCover stores onCoverLoaded as a direct "
                "C++ member-function pointer in a QSlotObject. The slot dispatcher "
                "branches through that stored pointer, bypassing onCoverLoaded's "
                "PLT jump slot. PowerViewController::loadView does call updateCover "
                "through a PLT jump slot immediately before updateReadingStatus. "
                "Hook updateCover and pass the prepared notebook QImage directly to "
                "stock onCoverLoaded; skip the asynchronous book-cover load only "
                "while a notebook image is pending."
            ),
        }
        for address, expected in PROOF_BYTES.items():
            memory = view.read(address, len(expected))
            assert memory.complete and memory.data == expected
            report["proof_bytes"][hex(address)] = memory.data.hex()

    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
