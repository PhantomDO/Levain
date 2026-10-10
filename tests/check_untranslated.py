#!/usr/bin/env python3
"""i18n.untranslated (ADR-0036, décision 14) : un texte affiché par l'éditeur ou les panneaux du moteur passe par le
catalogue (ui::tr, ui::trf, ui::textf, ui::labelOf). Usage : check_untranslated.py <racine du dépôt>.

Refusé par défaut, pour que la fonction d'ImGui qu'on n'avait pas prévue (SetItemTooltip, ColorEdit3, TextColored…) soit
vue aussi : dans un appel `ImGui::<f>(…)`, tout littéral avec une lettre (ou un caractère non ASCII) une fois ôtés les
champs de printf, sauf s'il est en « ## » (un identifiant), dans un `tr`/`trf`/`textf`/`labelOf`, ou s'il est le premier
argument d'une fonction d'ID_ONLY, qui n'affiche rien. `Begin` veut un `labelOf(` : un titre traduit sans « ### » ferait
perdre sa fenêtre à la disposition. Non vus : un texte construit à l'exécution, un appel sans « ImGui:: », un glyphe
d'icône (à autoriser quand il viendra). Python plutôt que CMake, pour une regex qui donne sa position."""
import re
import sys
from pathlib import Path

DIRECTORIES = ("editor", "engine/app", "engine/ui")  # là où le dépôt dessine ses fenêtres
ID_ONLY = frozenset("""BeginTable BeginChild BeginTabBar PushID GetID OpenPopup BeginPopup BeginPopupContextItem
BeginPopupContextWindow IsPopupOpen InvisibleButton ArrowButton TreePush""".split())
STRING = r'"(?:[^"\\]|\\.)*"'
SOURCE_TOKEN = re.compile(rf"{STRING}|'(?:[^'\\]|\\.)'|//[^\n]*|/\*.*?\*/", re.S)
CALL = re.compile(r"\bImGui::(\w+)\s*\(")
CATALOGUED = re.compile(rf"\b(?:ui::)?(?:tr|trf|textf|labelOf)\(\s*{STRING}")
PRINTF_FIELD = re.compile(r"\\.|%[-+ #0]*\d*(?:\.\d+)?(?:ll|l|z|h)?[a-zA-Z]")
# (extrait, refus attendus) : le contrôle se prouve avant de lire le dépôt (règle n°7), chaque cas est un contre-test.
SELF_TEST = (
    ('ImGui::Text("Bonjour \\"toi\\"", x);', 1), ('ImGui::SetItemTooltip("Lancer");', 1),
    ('ImGui::TextColored(color, "Erreur");', 1), ('ImGui::TextDisabled("%s", "non mesuré");', 1),
    ('ImGui::TreeNodeEx("##id", 0, "Nœud %d", i);', 1), ('ImGui::Button(\n"Valider");', 1),
    ('ImGui::Text(str("Bonjour"));', 1), ('ImGui::Begin(ui::tr("Console"));', 1), ("ImGui::Begin(name);", 1),
    ('ImGui::Text("a"); // ImGui::Text("Bonjour")', 1), ('// ImGui::Text("Bonjour")\nint x;', 0),
    ('ImGui::TextDisabled("%s", ui::tr("ok (fin)"));', 0), ('ImGui::Begin(ui::labelOf("Console").c_str());', 0),
    ('ImGui::BeginTable("passes", 2);', 0), ('ImGui::TreeNodeEx("##entité", 0, "%s", s);', 0),
)


def without_comments(text: str) -> str:
    """Les commentaires en blancs, leurs retours à la ligne gardés : les numéros de ligne restent justes."""
    return SOURCE_TOKEN.sub(lambda m: m.group() if m.group()[0] in "\"'" else re.sub(r"[^\n]", " ", m.group()), text)


def arguments_of(text: str, start: int) -> str:
    """Ce qui suit la parenthèse ouvrante, jusqu'à sa fermante ; un littéral se saute entier (il peut contenir « ) »)."""
    depth, i = 1, start
    while i < len(text) and depth:
        token = SOURCE_TOKEN.match(text, i)
        if token:
            i = token.end()
            continue
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    return text[start:i - 1]


def shows_text(literal: str) -> bool:
    """Vrai si ImGui afficherait ce littéral tel quel, avec une lettre (ou un caractère non ASCII)."""
    return not literal.startswith("##") and re.search(r"[A-Za-z]|[^\x00-\x7f]", PRINTF_FIELD.sub("", literal)) is not None


def refusals(text: str) -> tuple[int, list[tuple[int, str]]]:
    """Le nombre d'appels d'ImGui lus, et les (ligne, raison) des refus."""
    text, found, calls = without_comments(text), [], 0
    for call in CALL.finditer(text):
        calls += 1
        name, args, line = call.group(1), arguments_of(text, call.end()), text.count("\n", 0, call.start()) + 1
        if name == "Begin" and not re.match(r"\s*(?:ui::)?labelOf\(", args):
            found.append((line, "Begin sans ui::labelOf : traduit, son titre perdrait sa fenêtre à la disposition"))
        args = CATALOGUED.sub("", args)
        for rank, literal in enumerate(re.finditer(STRING, args)):
            first_argument = rank == 0 and args[:literal.start()].strip() == ""
            if shows_text(literal.group()[1:-1]) and not (first_argument and name in ID_ONLY):
                found.append((line, f"« {literal.group()[1:-1]} » est affiché hors du catalogue (ImGui::{name})"))
    return calls, found


def main(root: Path) -> int:
    for snippet, expected in SELF_TEST:
        if len(refusals(snippet)[1]) != expected:
            sys.exit(f"le contrôle se trompe sur {snippet!r} : {expected} refus attendus, {refusals(snippet)[1]}")
    failures = []
    for directory in DIRECTORIES:
        if not (root / directory).is_dir():
            sys.exit(f"{root / directory} introuvable : le contrôle ne verrait rien")
        calls = 0
        for path in sorted(p for p in (root / directory).rglob("*") if p.suffix in (".cpp", ".hpp")):
            read, found = refusals(path.read_text(encoding="utf-8"))
            calls += read
            failures += [f"{path.relative_to(root)}:{line} : {reason}" for line, reason in found]
        if calls == 0:
            sys.exit(f"aucun appel d'ImGui lu sous {directory} : le contrôle ne verrait rien")
    print("\n".join(failures + ["(ui::tr, ui::trf, ui::textf, ui::labelOf ; un identifiant seul prend « ## »)"])
          if failures else f"{len(DIRECTORIES)} dossiers lus, aucun littéral hors du catalogue")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1])))
