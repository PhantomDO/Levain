#!/usr/bin/env python3
"""Le résumé d'une capture Tracy (#295) : la médiane de chaque zone CPU et de chaque courbe.

tracy-csvexport donne des moyennes, qu'une seule image de chargement suffit à fausser (scanAssets,
la première image) : la médiane dit ce que coûte une image ordinaire. Les courbes « GPU étape/nom »
sont les temps GPU des fonctions d'étape (engine/render/src/stages.cpp), une valeur par image.

    tools/tracy-summary.py captures/hike.tracy
"""

import csv
import io
import os
import statistics
import subprocess
import sys


def export(capture: str, *options: str) -> list[dict[str, str]]:
    tool = os.path.join(os.environ.get("TRACY_DIR", os.path.expanduser("~/.local/opt/tracy-0.14.1")),
                        "tracy-csvexport")
    output = subprocess.run([tool, *options, capture], check=True, capture_output=True, text=True).stdout
    return list(csv.DictReader(io.StringIO(output)))


def medians(rows: list[dict[str, str]], field: str) -> dict[str, tuple[float, int]]:
    values: dict[str, list[float]] = {}
    for row in rows:
        if row[field]:
            values.setdefault(row["name"], []).append(float(row[field]))
    return {name: (statistics.median(found), len(found)) for name, found in values.items()}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    capture = sys.argv[1]
    zones = medians(export(capture, "-u"), "exec_time_ns")
    # Les courbes viennent avec les zones (-p n'agit qu'avec -u) : on ne garde que les lignes valuées.
    plots = medians([row for row in export(capture, "-u", "-p") if row.get("value")], "value")
    if not zones:
        print("ÉCHEC : aucune zone dans la capture", file=sys.stderr)
        return 1
    print("zones CPU, médiane par appel :")
    for name, (median, count) in sorted(zones.items(), key=lambda item: -item[1][0]):
        print(f"  {name} : {median / 1000:.1f} µs ({count} appels)")
    print("courbes, médiane par image :")
    for name, (median, count) in sorted(plots.items()):
        print(f"  {name} : {median:.3f} ({count} images)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
