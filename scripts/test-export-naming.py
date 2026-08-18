#!/usr/bin/env python3

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    source_dir = root / "mods" / "custom-notebook-templates" / "src"
    compiler_name = os.environ.get("HOST_CXX", "c++")
    compiler = shutil.which(compiler_name)
    if compiler is None:
        raise SystemExit(f"host C++ compiler not found: {compiler_name}")

    with tempfile.TemporaryDirectory(
        prefix="kobo-export-naming-test-"
    ) as temporary:
        executable = Path(temporary) / "export-naming-test"
        subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{source_dir}",
                str(source_dir / "export_naming.cc"),
                str(root / "scripts" / "test-export-naming.cc"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
