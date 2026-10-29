#!/usr/bin/env python3
"""Prove behavioral tests reject representative faults in production code."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/StackSize.cpp").read_text()
MUTATIONS = [
    ("startup hook omitted", '        WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,', ''),
    ("configuration hook omitted", '        WORLDHOOK_ON_AFTER_CONFIG_LOAD,', ''),
    ("cache hook omitted", '        WORLDHOOK_ON_BEFORE_FINALIZE_PLAYER_WORLD_SESSION', ''),
    ("trade-goods default", 'ITEM_CLASS_TRADE_GOODS, 999', 'ITEM_CLASS_TRADE_GOODS, 998'),
    ("non-stackable class item", 'proto.Stackable <= 1', 'false'),
    ("class rule lowers a stack", ' || uint32(proto.Stackable) >= itr->second', ''),
    ("explicit item ignored", 'return itr->second;', 'return 0;'),
    ("excluded item changed", 'if (config.excluded.count(proto.ItemId))', 'if (false)'),
    ("carry-limit raise disabled", 'bool const raiseCap =', 'bool const raiseCap = false &&'),
    ("carry-only update skipped", 'if (uint32(proto->Stackable) == size && !raiseCap)', 'if (uint32(proto->Stackable) == size)'),
    ("higher carry-limit lowered", ' && uint32(proto->MaxCount) < size', ''),
    ("stack target omitted from cache", 'Mix(hash, size);', 'Mix(hash, 1);'),
    ("carry limit omitted from cache", 'Mix(hash, uint32(proto->MaxCount));', '(void)hash;'),
    ("master switch ignored", 'if (config.enabled)', 'if (true)'),
    ("cache switch ignored", 'if (changed && config.resetClientCache)', 'if (changed)'),
    ("unchanged items invalidate cache", 'if (changed && config.resetClientCache)', 'if ((static_cast<void>(changed), config.resetClientCache))'),
    ("zero size accepted", ' || !*size', ''),
    ("explicit size not clamped", 'sizes[*entry] = std::min(*size, MaxStack);', 'sizes[*entry] = *size;'),
    ("reload rereads configuration", 'if (reload)', 'if (reload && false)'),
]


def check_mutation(mutation):
    name, old, new = mutation
    if old not in SOURCE:
        return name, False, "Mutation anchor missing: update the sentinel for the new source"
    with tempfile.TemporaryDirectory(prefix="stack-size-mutation-") as temporary:
        source = Path(temporary) / "StackSize.cpp"
        source.write_text(SOURCE.replace(old, new, 1))
        environment = dict(os.environ, STACK_SIZE_SOURCE=str(source))
        environment.pop("BUILD_DIR", None)
        environment.pop("CXXFLAGS", None)
        result = subprocess.run([sys.executable, str(ROOT / "tests/run.py")], env=environment,
                                capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        # Compile errors do not count as a caught behavioral regression.
        killed = result.returncode != 0 and "FAIL:" in output and "error:" not in output
        return name, killed, output[-1500:]


if __name__ == "__main__":
    subprocess.run([sys.executable, str(ROOT / "tests/run.py")], check=True)
    workers = min(4, os.cpu_count() or 1)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(check_mutation, MUTATIONS))
    for name, killed, output in results:
        print(f'{"CAUGHT" if killed else "MISSED"}: {name}')
        if not killed:
            print(output)
    caught = sum(killed for _, killed, _ in results)
    print(f"Caught {caught}/{len(results)} representative behavioral regressions")
    sys.exit(0 if caught == len(results) else 1)
