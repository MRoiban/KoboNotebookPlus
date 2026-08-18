#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import struct
import unittest


SCRIPT = pathlib.Path(__file__).with_name("make-iink-exporters.py")
SPEC = importlib.util.spec_from_file_location("make_iink_exporters", SCRIPT)
assert SPEC and SPEC.loader
PATCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCHER)

REQUEST_ENTRY_TIMER_BYTES = {
    0x1F48A: "4df2c041",
    0x1F48E: "c0f20101",
}


def synthetic_stock() -> bytes:
    data = bytearray(PATCHER.EXPECTED_SIZE)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", data, 16, 2, 40)
    for group in (PATCHER.COMMON_PATCHES, PATCHER.FAST_PATCHES):
        for address, (expected, _) in group.items():
            payload = bytes.fromhex(expected)
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            data[offset : offset + len(payload)] = payload
    for address, expected in REQUEST_ENTRY_TIMER_BYTES.items():
        payload = bytes.fromhex(expected)
        offset = address - PATCHER.VIEW_TO_FILE_BIAS
        data[offset : offset + len(payload)] = payload
    return bytes(data)


class ExporterPatchTests(unittest.TestCase):
    PDF_DESTINATION_SITES = {
        0x1FE00: bytes.fromhex("4ff41b60"),
        0x1FE04: bytes.fromhex("40f6b45e"),
    }

    def test_best_changes_only_idle_timer_sites(self) -> None:
        stock = synthetic_stock()
        best = PATCHER.apply_patches(stock, (PATCHER.COMMON_PATCHES,))
        PATCHER.validate_elf(best)
        changed = {i for i, (before, after) in enumerate(zip(stock, best)) if before != after}
        expected = set()
        for address, (before, after) in PATCHER.COMMON_PATCHES.items():
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            expected.update(
                offset + i
                for i, pair in enumerate(zip(bytes.fromhex(before), bytes.fromhex(after)))
                if pair[0] != pair[1]
            )
        self.assertEqual(changed, expected)

    def test_request_entry_timeout_remains_stock(self) -> None:
        stock = synthetic_stock()
        best = PATCHER.apply_patches(stock, (PATCHER.COMMON_PATCHES,))
        for address, expected in REQUEST_ENTRY_TIMER_BYTES.items():
            payload = bytes.fromhex(expected)
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            self.assertEqual(stock[offset : offset + len(payload)], payload)
            self.assertEqual(best[offset : offset + len(payload)], payload)

    def test_completion_timeout_is_shortened(self) -> None:
        stock = synthetic_stock()
        best = PATCHER.apply_patches(stock, (PATCHER.COMMON_PATCHES,))
        for address in (0x2061A, 0x20620):
            _, replacement = PATCHER.COMMON_PATCHES[address]
            payload = bytes.fromhex(replacement)
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            self.assertEqual(best[offset : offset + len(payload)], payload)

    def test_fast_applies_every_resolution_and_timer_patch(self) -> None:
        stock = synthetic_stock()
        fast = PATCHER.apply_patches(
            stock, (PATCHER.COMMON_PATCHES, PATCHER.FAST_PATCHES)
        )
        PATCHER.validate_elf(fast)
        for group in (PATCHER.COMMON_PATCHES, PATCHER.FAST_PATCHES):
            for address, (_, replacement) in group.items():
                payload = bytes.fromhex(replacement)
                offset = address - PATCHER.VIEW_TO_FILE_BIAS
                self.assertEqual(fast[offset : offset + len(payload)], payload)

    def test_fast_preserves_full_size_pdf_destination(self) -> None:
        stock = bytearray(synthetic_stock())
        for address, payload in self.PDF_DESTINATION_SITES.items():
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            stock[offset : offset + len(payload)] = payload
        fast = PATCHER.apply_patches(
            bytes(stock), (PATCHER.COMMON_PATCHES, PATCHER.FAST_PATCHES)
        )
        for address, payload in self.PDF_DESTINATION_SITES.items():
            offset = address - PATCHER.VIEW_TO_FILE_BIAS
            self.assertEqual(fast[offset : offset + len(payload)], payload)

    def test_stale_patch_precondition_fails_closed(self) -> None:
        stock = bytearray(synthetic_stock())
        address = next(iter(PATCHER.FAST_PATCHES))
        stock[address - PATCHER.VIEW_TO_FILE_BIAS] ^= 0xFF
        with self.assertRaisesRegex(ValueError, "precondition failed"):
            PATCHER.apply_patches(bytes(stock), (PATCHER.FAST_PATCHES,))


if __name__ == "__main__":
    unittest.main()
