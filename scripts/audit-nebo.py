#!/usr/bin/env python3
"""Read-only structural audit for Kobo/MyScript .nebo archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
from pathlib import Path


REQUIRED_ROOT = {"meta.json", "rel.json", "index.bdom"}
REQUIRED_PAGE = {"meta.json", "page.bdom", "ink.bink"}


def digest_members(archive: zipfile.ZipFile, members: list[str]) -> str:
    digest = hashlib.sha256()
    for name in sorted(members):
        digest.update(name.rsplit("/", 1)[-1].encode("utf-8"))
        digest.update(b"\0")
        digest.update(archive.read(name))
    return digest.hexdigest()


def audit(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "ok": False, "errors": []}
    errors: list[str] = result["errors"]  # type: ignore[assignment]
    if not path.is_file() or path.suffix.lower() != ".nebo":
        errors.append("not a regular .nebo file")
        return result

    try:
        with zipfile.ZipFile(path) as archive:
            bad_member = archive.testzip()
            if bad_member:
                errors.append(f"CRC failure: {bad_member}")
            names = set(archive.namelist())
            for name in sorted(REQUIRED_ROOT - names):
                errors.append(f"missing root member: {name}")

            rel = json.loads(archive.read("rel.json"))
            rel_pages = rel.get("pages")
            if not isinstance(rel_pages, dict) or not rel_pages:
                errors.append("rel.json has no pages map")
                page_ids: list[str] = []
            else:
                # JSON object order is useful as insertion evidence, but the
                # authoritative display order remains binary index.bdom.
                page_ids = list(rel_pages)

            directory_ids = {
                name.split("/", 2)[1]
                for name in names
                if name.startswith("pages/") and name.count("/") >= 2
            }
            missing_directories = set(page_ids) - directory_ids
            orphan_directories = directory_ids - set(page_ids)
            for page_id in sorted(missing_directories):
                errors.append(f"missing page directory: {page_id}")
            for page_id in sorted(orphan_directories):
                errors.append(f"orphan page directory: {page_id}")

            pages: list[dict[str, object]] = []
            for page_id in page_ids:
                prefix = f"pages/{page_id}/"
                members = [name for name in names if name.startswith(prefix)]
                basenames = {name[len(prefix) :] for name in members}
                for name in sorted(REQUIRED_PAGE - basenames):
                    errors.append(f"page {page_id} missing {name}")
                background = None
                try:
                    page_meta = json.loads(archive.read(prefix + "meta.json"))
                    background = (
                        page_meta.get("iink-user-metadata", {})
                        .get("kobo", {})
                        .get("backgroundType")
                    )
                except (KeyError, json.JSONDecodeError, AttributeError) as exc:
                    errors.append(f"page {page_id} metadata invalid: {exc}")
                pages.append(
                    {
                        "id": page_id,
                        "background": background,
                        "content_sha256": digest_members(archive, members),
                        "members": len(members),
                    }
                )

            result.update(
                {
                    "bytes": path.stat().st_size,
                    "page_count": len(page_ids),
                    "page_ids_in_rel_order": page_ids,
                    "pages": pages,
                    "ok": not errors,
                }
            )
    except (OSError, zipfile.BadZipFile, KeyError, json.JSONDecodeError) as exc:
        errors.append(str(exc))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("notebooks", nargs="+", type=Path)
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    reports = [audit(path) for path in args.notebooks]
    output = reports
    if args.summary:
        output = [
            {
                "path": report["path"],
                "ok": report["ok"],
                "page_count": report.get("page_count"),
                "errors": report["errors"],
            }
            for report in reports
        ]
    json.dump(
        output,
        sys.stdout,
        indent=None if args.compact else 2,
        sort_keys=True,
    )
    sys.stdout.write("\n")
    return 0 if all(report["ok"] for report in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
