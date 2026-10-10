#!/usr/bin/env python3
"""i18n.untranslated (ADR-0036, décision 14) : un texte affiché par l'éditeur ou les panneaux du moteur passe par le
catalogue (ui::tr, ui::trf, ui::textf, ui::labelOf). Usage : check_untranslated.py <racine du dépôt>.

Refusé : `ImGui::<f>(` dont le PREMIER argument est un littéral avec une lettre, pour les <f> de DISPLAYING (ceux qui
montrent cet argument). Admis : un littéral en « ## » (un identifiant, qu'ImGui n'affiche pas), un littéral sans lettre
une fois ôtés les champs de printf (« %s » : `TextDisabled("%s", tr("…"))`), tout ce qui n'est pas un littéral. Non vus :
un texte construit à l'exécution, un littéral qui n'est pas le premier argument (`TextColored`), un appel sans
« ImGui:: ». Python plutôt que CMake, pour une regex qui donne sa position."""
import re
import sys
from pathlib import Path

DIRECTORIES = ("editor", "engine/app", "engine/ui")  # là où le dépôt dessine ses fenêtres
DISPLAYING = """Text TextDisabled TextWrapped TextUnformatted BulletText LabelText SeparatorText SetTooltip Button
SmallButton Checkbox RadioButton MenuItem BeginMenu Begin BeginPopupModal BeginCombo Combo TreeNode TreeNodeEx
CollapsingHeader Selectable PlotLines PlotHistogram TableSetupColumn BeginTabItem DragFloat DragFloat3 DragInt
SliderFloat SliderInt InputText InputFloat InputInt""".split()
CALL = re.compile(r"ImGui::(?:%s)\s*\(" % "|".join(DISPLAYING))
LITERAL = re.compile(CALL.pattern + r'\s*"((?:[^"\\]|\\.)*)"')
PRINTF_FIELD = re.compile(r"\\.|%[-+ #0]*\d*(?:\.\d+)?(?:ll|l|z|h)?[a-zA-Z]")


def shows_text(literal: str) -> bool:
    """Vrai si ImGui afficherait ce littéral tel quel, avec une lettre (ou un caractère non ASCII)."""
    visible = PRINTF_FIELD.sub("", literal)
    return not literal.startswith("##") and re.search(r"[A-Za-z]|[^\x00-\x7f]", visible) is not None


def main(root: Path) -> int:
    example = LITERAL.search('ImGui::Text("Bonjour \\"toi\\"", x)')  # règle n°7 : l'expression se prouve d'abord
    if example is None or not shows_text(example.group(1)):
        sys.exit("l'expression des littéraux ne lit plus ImGui::Text(\"Bonjour\") : le contrôle ne verrait rien")
    calls, failures = 0, []
    for directory in DIRECTORIES:
        if not (root / directory).is_dir():
            sys.exit(f"{root / directory} introuvable : le contrôle ne verrait rien")
        for path in sorted(p for p in (root / directory).rglob("*") if p.suffix in (".cpp", ".hpp")):
            text = path.read_text(encoding="utf-8")
            calls += len(CALL.findall(text))
            for hit in filter(lambda h: shows_text(h.group(1)), LITERAL.finditer(text)):
                line = text.count("\n", 0, hit.start()) + 1
                failures.append(f"{path.relative_to(root)}:{line} : « {hit.group(1)} » est affiché hors du catalogue")
    if calls == 0:
        sys.exit("aucun appel d'affichage d'ImGui lu sous " + ", ".join(DIRECTORIES) + " : le contrôle ne verrait rien")
    print("\n".join(failures + ["(ui::tr, ui::trf, ui::textf, ui::labelOf ; un identifiant seul prend « ## »)"])
          if failures else f"{calls} appels d'affichage d'ImGui lus, aucun littéral hors du catalogue")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1])))
