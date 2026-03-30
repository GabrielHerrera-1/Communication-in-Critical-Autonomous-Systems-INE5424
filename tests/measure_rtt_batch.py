#!/usr/bin/env python3

import pathlib
import os
import statistics
import subprocess
import sys
import tempfile


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from summarize_rtt import describe_samples, extract_samples


def main() -> int:
    if len(sys.argv) not in (4, 5):
        print(
            "uso: measure_rtt_batch.py <run_qemu_test.sh> <qemu_cpu> <repeticoes> [binario]",
            file=sys.stderr,
        )
        return 2

    runner = pathlib.Path(sys.argv[1]).resolve()
    qemu_cpu = sys.argv[2]
    repetitions = int(sys.argv[3])
    binary_rel = sys.argv[4] if len(sys.argv) == 5 else "tests/rtt"

    if repetitions <= 0:
        print("repeticoes deve ser > 0", file=sys.stderr)
        return 2

    if not runner.is_file():
        print(f"runner ausente: {runner}", file=sys.stderr)
        return 1

    repo_root = SCRIPT_DIR.parent
    all_samples: list[int] = []
    preserved_runs: list[pathlib.Path] = []

    for run_index in range(1, repetitions + 1):
        with tempfile.NamedTemporaryFile(prefix="so2-rtt-artifacts.", delete=False) as tmp:
            artifacts_file = pathlib.Path(tmp.name)

        env = dict(os.environ)
        env["KEEP_ARTIFACTS"] = "1"
        env["ARTIFACTS_FILE"] = str(artifacts_file)
        env["QEMU_CPU"] = qemu_cpu

        try:
            subprocess.run(
                [
                    str(runner),
                    binary_rel,
                    "5",
                    "rtt-benchmark",
                    "RTT benchmark concluido.",
                ],
                cwd=repo_root,
                env=env,
                check=True,
            )

            artifacts_root = pathlib.Path(artifacts_file.read_text(encoding="utf-8").strip())
            preserved_runs.append(artifacts_root)
            vm1_log = artifacts_root / "logs" / "vm1.log"
            samples = extract_samples(vm1_log)
            if not samples:
                print(f"run {run_index}: nenhuma amostra encontrada em {vm1_log}", file=sys.stderr)
                return 1

            all_samples.extend(samples)
            stats = describe_samples(samples)
            print(
                "run {idx}: avg={avg:.2f} us ({avg_ms:.4f} ms), "
                "min={min_us:.0f} us, max={max_us:.0f} us, amostras={count:.0f}".format(
                    idx=run_index,
                    avg=stats["avg_us"],
                    avg_ms=stats["avg_us"] / 1000.0,
                    min_us=stats["min_us"],
                    max_us=stats["max_us"],
                    count=stats["count"],
                )
            )
        finally:
            artifacts_file.unlink(missing_ok=True)

    aggregate = describe_samples(all_samples)
    avg_us = aggregate["avg_us"]
    min_us = aggregate["min_us"]
    max_us = aggregate["max_us"]

    print("")
    print(f"RTT agregado ({repetitions} execucoes)")
    print(f"RTT amostras totais: {int(aggregate['count'])}")
    print(f"RTT medio agregado: {avg_us:.2f} us ({avg_us / 1000.0:.4f} ms)")
    print(f"RTT minimo agregado: {min_us:.0f} us ({min_us / 1000.0:.4f} ms)")
    print(f"RTT maximo agregado: {max_us:.0f} us ({max_us / 1000.0:.4f} ms)")
    print(
        "Latencia estimada de ida agregada (RTT/2): "
        f"{avg_us / 2.0:.2f} us ({avg_us / 2000.0:.4f} ms)"
    )
    print("Artefatos preservados:")
    for path in preserved_runs:
        print(path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
