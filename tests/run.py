#!/usr/bin/env python3
"""Compile and execute the actual module against isolated external core services."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(build):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", ""))
    if any(flag.startswith("-fsanitize=") for flag in flags):
        flags.append("-fno-sanitize-recover=all")
    source = os.environ.get("STACK_SIZE_SOURCE")
    defines = [f'-DSTACK_SIZE_SOURCE="{Path(source).resolve()}"'] if source else []
    output = build / "stack-size-tests"
    # Reusing BUILD_DIR must not merge profiles from different source revisions.
    for pattern in ("stack-size-tests*.gcda", "stack-size-tests*.gcno"):
        for profile in build.glob(pattern):
            profile.unlink()
    # Instrument/link every production translation unit. A newly added .cpp must
    # either be covered by this suite or fail the source coverage gate.
    included_source = ROOT / "src/StackSize.cpp"
    other_sources = [str(path) for path in sorted((ROOT / "src").rglob("*.cpp"))
                     if path != included_source]
    subprocess.run(compiler + ["-std=c++20", "-Wall", "-Wextra", "-Werror"] + flags
                   + defines + ["-I", str(ROOT / "tests/stubs"),
                                str(ROOT / "tests/test_stack_size.cpp"),
                                *other_sources, "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    if os.environ.get("BUILD_DIR"):
        directory = Path(os.environ["BUILD_DIR"]).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        run(directory)
    else:
        with tempfile.TemporaryDirectory(prefix="stack-size-tests-") as directory:
            run(Path(directory))
