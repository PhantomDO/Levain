#!/usr/bin/env python3
"""L'échelle de liaisons structurées de la réflexion des composants (ADR-0034).

C++23 n'a pas de paquet dans les liaisons structurées (P1061 arrive en C++26) : lire les champs d'un
agrégat demande une branche `auto& [f0, …, fN] = object` par nombre de champs. Écrites à la main,
ces 32 lignes cachent une faute de frappe à la relecture ; ce script les écrit, hors de
clang-format, qui les étalerait sur 190 lignes. Le test `scene.field-ladder` le relance avec
`--check` et échoue si le fichier versionné n'est plus sa sortie (règle n°7).

    tools/generate_field_ladder.py engine/scene/include/levain/scene/detail/field_ladder.inc
    tools/generate_field_ladder.py --check engine/scene/include/levain/scene/detail/field_ladder.inc
"""

import sys

MAX_FIELDS = 32  # MaxFields dans reflection.hpp : les deux bougent ensemble


def ladder() -> str:
    lines = ["// Généré par tools/generate_field_ladder.py : ne pas modifier, relancer le script.\n"]
    for count in range(1, MAX_FIELDS + 1):
        names = ", ".join(f"f{i}" for i in range(count))
        branch = "if" if count == 1 else "else if"
        lines.append(f"{branch} constexpr (N == {count}) {{ auto& [{names}] = object; "
                     f"return std::tie({names}); }}\n")
    return "".join(lines)


def main(arguments: list[str]) -> int:
    if arguments[:1] == ["--check"] and len(arguments) == 2:
        with open(arguments[1], encoding="utf-8") as committed:
            if committed.read() == ladder():
                print(f"{arguments[1]} : à jour")
                return 0
        print(f"{arguments[1]} n'est plus la sortie de ce script : le relancer sans --check",
              file=sys.stderr)
        return 1
    if len(arguments) != 1 or arguments[0].startswith("-"):
        print(__doc__, file=sys.stderr)
        return 2
    with open(arguments[0], "w", encoding="utf-8") as output:
        output.write(ladder())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
