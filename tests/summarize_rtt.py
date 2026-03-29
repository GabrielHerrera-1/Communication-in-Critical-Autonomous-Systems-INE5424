#!/usr/bin/env python3

import pathlib
import re
import statistics
import sys


SAMPLE_RE = re.compile(r"RTT_SAMPLE seq=(\d+) us=(\d+)")


def extract_samples(log_path: pathlib.Path) -> list[int]:
    samples = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SAMPLE_RE.search(line)
        if match:
            samples.append(int(match.group(2)))
    return samples


def describe_samples(samples: list[int]) -> dict[str, float]:
    avg_us = statistics.fmean(samples)
    min_us = min(samples)
    max_us = max(samples)
    return {
        "count": float(len(samples)),
        "avg_us": avg_us,
        "min_us": float(min_us),
        "max_us": float(max_us),
    }


def main() -> int:
    if len(sys.argv) != 2:
        print("uso: summarize_rtt.py <vm1.log>", file=sys.stderr)
        return 2

    log_path = pathlib.Path(sys.argv[1])
    if not log_path.is_file():
        print(f"log ausente: {log_path}", file=sys.stderr)
        return 1

    samples = extract_samples(log_path)

    if not samples:
        print("nenhuma amostra RTT encontrada no log", file=sys.stderr)
        return 1

    stats = describe_samples(samples)
    avg_us = stats["avg_us"]
    min_us = int(stats["min_us"])
    max_us = int(stats["max_us"])
    avg_one_way_us = avg_us / 2.0
    min_one_way_us = min_us / 2.0
    max_one_way_us = max_us / 2.0

    print(f"RTT amostras: {len(samples)}")
    print(f"RTT medio: {avg_us:.2f} us ({avg_us / 1000.0:.4f} ms)")
    print(f"RTT minimo: {min_us} us ({min_us / 1000.0:.4f} ms)")
    print(f"RTT maximo: {max_us} us ({max_us / 1000.0:.4f} ms)")
    print(
        "Latencia estimada de ida (RTT/2): "
        f"{avg_one_way_us:.2f} us ({avg_one_way_us / 1000.0:.4f} ms)"
    )
    print(
        "Latencia estimada minima de ida (RTT/2): "
        f"{min_one_way_us:.2f} us ({min_one_way_us / 1000.0:.4f} ms)"
    )
    print(
        "Latencia estimada maxima de ida (RTT/2): "
        f"{max_one_way_us:.2f} us ({max_one_way_us / 1000.0:.4f} ms)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
