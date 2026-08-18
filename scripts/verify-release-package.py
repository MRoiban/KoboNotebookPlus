#!/usr/bin/env python3
"""Validate the firmware-pinned KoboRoot installer and export-quality payload."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import tarfile


PLUGIN_MEMBER = "usr/local/Kobo/imageformats/libcustomnotebooktemplates.so"
STOCK_MEMBER = "usr/local/Kobo/libiinknote.so"
BEST_EXPORTER_MEMBER = "usr/local/Kobo/iink-export-best"
FAST_EXPORTER_MEMBER = "usr/local/Kobo/iink-export-fast"
LAUNCHER_MEMBER = "usr/local/Kobo/iink-launcher.sh"
EXPECTED_MEMBERS = frozenset(
    (
        PLUGIN_MEMBER,
        STOCK_MEMBER,
        BEST_EXPORTER_MEMBER,
        FAST_EXPORTER_MEMBER,
        LAUNCHER_MEMBER,
    )
)
EXPECTED_STOCK_SHA256 = (
    "f80a7de7a1c482173a89b18f2bb8164fcfb53b8fab9b2a75bd23998813a528ea"
)
EXPECTED_BEST_EXPORTER_SHA256 = (
    "f8ddf410f153b2c670201a43d2961746a229939b6e129a57c0ffe77545f3fac2"
)
EXPECTED_FAST_EXPORTER_SHA256 = (
    "2672624eeae69f71fd614556df5a4e5600829303c611b3f5471fe9e9c828b57e"
)
EXPECTED_LAUNCHER_SHA256 = (
    "93b145f0824e0ef4e7d3f0563260d58c01ea50bed8d16363e3da25c517f54541"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_member(name: str) -> str:
    while name.startswith("./"):
        name = name[2:]
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not name:
        raise ValueError(f"unsafe archive member: {name!r}")
    return str(path)


def validate_arm_shared_object(data: bytes) -> None:
    if len(data) < 20 or data[:4] != b"\x7fELF":
        raise ValueError("packaged plugin is not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise ValueError("packaged plugin is not 32-bit little-endian ELF")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if elf_type != 3 or machine != 40:
        raise ValueError(
            f"packaged plugin is not an ARM shared object "
            f"(type={elf_type}, machine={machine})"
        )


def validate_arm_executable(data: bytes) -> None:
    if len(data) < 20 or data[:4] != b"\x7fELF":
        raise ValueError("packaged exporter is not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise ValueError("packaged exporter is not 32-bit little-endian ELF")
    elf_type, machine = struct.unpack_from("<HH", data, 16)
    if elf_type != 2 or machine != 40:
        raise ValueError(
            f"packaged exporter is not an ARM executable "
            f"(type={elf_type}, machine={machine})"
        )


def validate_package(archive: pathlib.Path, built_plugin: pathlib.Path) -> dict[str, str]:
    built = built_plugin.read_bytes()
    validate_arm_shared_object(built)

    payloads: dict[str, bytes] = {}
    modes: dict[str, int] = {}
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle.getmembers():
            name = normalized_member(member.name)
            if not member.isfile():
                raise ValueError(f"archive member is not a regular file: {name}")
            if name in payloads:
                raise ValueError(f"duplicate archive member: {name}")
            stream = bundle.extractfile(member)
            if stream is None:
                raise ValueError(f"archive member cannot be read: {name}")
            payloads[name] = stream.read()
            modes[name] = member.mode

    names = frozenset(payloads)
    if names != EXPECTED_MEMBERS:
        missing = sorted(EXPECTED_MEMBERS - names)
        extra = sorted(names - EXPECTED_MEMBERS)
        raise ValueError(f"archive contents differ; missing={missing}, extra={extra}")

    packaged_plugin = payloads[PLUGIN_MEMBER]
    if packaged_plugin != built:
        raise ValueError("packaged plugin is not byte-identical to build output")
    validate_arm_shared_object(packaged_plugin)

    stock_digest = sha256(payloads[STOCK_MEMBER])
    if stock_digest != EXPECTED_STOCK_SHA256:
        raise ValueError(
            f"stock libiinknote SHA-256 is {stock_digest}, "
            f"expected {EXPECTED_STOCK_SHA256}"
        )

    best = payloads[BEST_EXPORTER_MEMBER]
    fast = payloads[FAST_EXPORTER_MEMBER]
    launcher = payloads[LAUNCHER_MEMBER]
    validate_arm_executable(best)
    validate_arm_executable(fast)
    expected_digests = {
        BEST_EXPORTER_MEMBER: EXPECTED_BEST_EXPORTER_SHA256,
        FAST_EXPORTER_MEMBER: EXPECTED_FAST_EXPORTER_SHA256,
        LAUNCHER_MEMBER: EXPECTED_LAUNCHER_SHA256,
    }
    for name, expected in expected_digests.items():
        actual = sha256(payloads[name])
        if actual != expected:
            raise ValueError(
                f"{name} SHA-256 is {actual}, expected {expected}"
            )
        if modes[name] & 0o111 == 0:
            raise ValueError(f"packaged executable is not executable: {name}")

    return {
        "archive": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "plugin": sha256(packaged_plugin),
        "stock": stock_digest,
        "best": sha256(best),
        "fast": sha256(fast),
        "launcher": sha256(launcher),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("built_plugin", type=pathlib.Path)
    args = parser.parse_args()
    try:
        digests = validate_package(args.archive, args.built_plugin)
    except (OSError, tarfile.TarError, ValueError) as exc:
        print(f"Release package verification FAILED: {exc}")
        return 1

    print("Release package verified: five pinned regular files")
    print(f"KoboRoot.tgz SHA-256: {digests['archive']}")
    print(f"Plugin SHA-256: {digests['plugin']}")
    print(f"Stock libiinknote.so SHA-256: {digests['stock']}")
    print(f"Best exporter SHA-256: {digests['best']}")
    print(f"Fast exporter SHA-256: {digests['fast']}")
    print(f"Launcher SHA-256: {digests['launcher']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
