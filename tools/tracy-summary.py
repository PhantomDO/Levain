#!/usr/bin/env python3
"""Le résumé d'une capture Tracy (#295) : la médiane de chaque zone CPU et de chaque courbe GPU.

tracy-csvexport donne des moyennes, qu'une seule image de chargement suffit à fausser (scanAssets,
la première image) : la médiane dit ce que coûte une image ordinaire. Les courbes « GPU étape/nom »
sont les temps GPU des fonctions d'étape (engine/render/src/stages.cpp), une valeur par image ; la
capture doit en contenir, sinon elle ne dit rien du GPU et le script échoue (règle n°7).

    tools/tracy-summary.py captures/hike.tracy
"""

import csv
import io
import os
import statistics
import subprocess
import sys


def export(capture: str) -> list[dict[str, str]]:
    """Chaque zone et chaque point de courbe (-u -p), une ligne par événement."""
    tool = os.path.join(os.environ.get("TRACY_DIR", os.path.expanduser("~/.local/opt/tracy-0.14.1")),
                        "tracy-csvexport")
    output = subprocess.run([tool, "-u", "-p", capture], check=True, capture_output=True,
                            text=True).stdout
    return list(csv.DictReader(io.StringIO(output)))


def medians(rows: list[dict[str, str]], field: str) -> dict[str, tuple[float, int]]:
    values: dict[str, list[float]] = {}
    for row in rows:
        values.setdefault(row["name"], []).append(float(row[field]))
    return {name: (statistics.median(found), len(found)) for name, found in values.items()}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    rows = export(sys.argv[1])
    # Une courbe n'a pas d'emplacement dans le source ; une zone en a un. La colonne `value` ne les
    # distingue pas : une zone y met son ZoneText.
    zones = medians([row for row in rows if row["src_line"]], "exec_time_ns")
    plots = medians([row for row in rows if not row["src_line"] and row["name"].startswith("GPU ")],
                    "value")
    if not zones or not plots:
        print("ÉCHEC : la capture n'a pas de zones, ou pas de courbes « GPU … » "
              "(sandbox profilé ? timeFunctions ?)", file=sys.stderr)
        return 1
    print("zones CPU, médiane par appel :")
    for name, (median, count) in sorted(zones.items(), key=lambda item: -item[1][0]):
        print(f"  {name} : {median / 1000:.1f} µs ({count} appels)")
    print("courbes GPU, médiane par image :")
    for name, (median, count) in sorted(plots.items()):
        print(f"  {name} : {median:.3f} ms ({count} images)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
