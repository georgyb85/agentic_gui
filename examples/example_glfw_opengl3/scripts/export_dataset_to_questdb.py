#!/usr/bin/env python3
"""
Minimal helper to push CSV indicator exports to QuestDB over ILP.
Usage:
  scripts/export_dataset_to_questdb.py --csv data.csv --measurement btc25_1
"""

import argparse
import csv
import socket
import sys
from typing import Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export CSV dataset to QuestDB (ILP).")
    parser.add_argument("--csv", required=True, help="Path to CSV file containing indicator rows.")
    parser.add_argument("--measurement", required=True, help="QuestDB measurement/table name.")
    parser.add_argument("--host", default="45.85.147.236", help="QuestDB host (default: 45.85.147.236).")
    parser.add_argument("--port", type=int, default=9009, help="QuestDB ILP port (default: 9009).")
    parser.add_argument("--tag-dataset", action="store_true", help="Attach dataset tag equal to measurement.")
    return parser.parse_args()


def to_number(value: str) -> Optional[str]:
    if value is None or value == "":
        return None
    try:
        if "." in value:
            return f"{float(value)}"
        return f"{int(value)}i"
    except ValueError:
        return None


def build_ilp(measurement: str, row: dict, tag_dataset: bool) -> Optional[str]:
    ts_raw = row.get("timestamp_unix") or row.get("timestamp")
    if not ts_raw:
        return None
    try:
        ts_seconds = int(float(ts_raw))
    except ValueError:
        return None

    fields = []
    for key, value in row.items():
        if key in {"timestamp_unix", "timestamp", "Date", "Time"}:
            continue
        number = to_number(value)
        if number is not None:
            # field keys require escaping of spaces/commas; keep it simple
            safe_key = key.replace(" ", "_").replace(",", "_")
            fields.append(f"{safe_key}={number}")
    if not fields:
        return None

    tags = []
    if tag_dataset:
        tags.append(f"dataset={measurement}")
    tags_part = ",".join(tags)
    line = measurement
    if tags_part:
        line += f",{tags_part}"
    line += " " + ",".join(fields) + f" {ts_seconds * 1_000_000_000}"
    return line


def main() -> int:
    args = parse_args()
    try:
        with open(args.csv, newline="") as handle:
            reader = csv.DictReader(handle)
            lines = []
            for row in reader:
                ilp = build_ilp(args.measurement, row, args.tag_dataset)
                if ilp:
                    lines.append(ilp)
    except FileNotFoundError:
        print(f"[export] CSV file not found: {args.csv}", file=sys.stderr)
        return 1

    if not lines:
        print("[export] No ILP lines generated (check timestamp_unix column).", file=sys.stderr)
        return 1

    payload = ("\n".join(lines) + "\n").encode("utf-8")

    try:
        with socket.create_connection((args.host, args.port)) as sock:
            sock.sendall(payload)
    except OSError as exc:
        print(f"[export] Failed to send ILP payload: {exc}", file=sys.stderr)
        return 1

    print(f"[export] Pushed {len(lines)} rows to {args.measurement} at {args.host}:{args.port}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
