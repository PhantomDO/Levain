#!/usr/bin/env python3
"""i18n.untranslated (ADR-0036, décision 14) : un texte affiché par l'éditeur ou les panneaux du moteur passe par le
catalogue (ui::tr, ui::trf, ui::textf, ui::labelOf). Usage : check_untranslated.py <racine du dépôt>.

Refusé par défaut, pour que la fonction d'ImGui qu'on n'avait pas prévue (SetItemTooltip, ColorEdit3, TextColored…) soit
vue aussi : dans un appel `ImGui::<f>(…)`, tout littéral avec une lettre (ou un caractère non ASCII) une fois ôtés les
champs de printf, sauf s'il est en « ## » (un identifiant), dans un `tr`/`trf`/`textf`/`labelOf`, ou s'il est le premier
argument d'une fonction d'ID_ONLY, qui n'affiche rien. `Begin` veut un `labelOf(` : un titre traduit sans « ### » ferait
perdre sa fenêtre à la disposition. Non vus : un texte construit à l'exécution, un appel sans « ImGui:: », un glyphe
d'icône (écrit en échappement \\uXXXX, hors du contrôle). Les caractères non ASCII d'un littéral : sous editor/, ils
doivent être dans tests/data/ui-glyphs.txt, que tests/ui_fonts_test.cpp vérifie dans les vraies polices ; sous
engine/app et engine/ui, qui gardent la police par défaut d'ImGui (le sandbox, Rando), en Latin-1 (« œ » s'y dessinerait
« ? »). Un échappement non ASCII (\\uXXXX, \\UXXXXXXXX, \\xHH, \\ooo) passerait entre les deux contrôles : il est refusé
partout, sauf dans icons.hpp et pour la zone d'usage privé (les icônes de Material Symbols). Python plutôt que CMake,
pour une regex qui donne sa position."""
import re
import sys
from pathlib import Path

# Là où le dépôt dessine ses fenêtres, avec la règle des caractères non ASCII : la liste des glyphes des polices de
# l'éditeur (Inter, Noto Sans), ou None : Latin-1, la police par défaut d'ImGui, que le sandbox et Rando gardent.
DIRECTORIES = {"editor": "list", "engine/app": None, "engine/ui": None}
ICONS_FILE = "icons.hpp"
ESCAPE = re.compile(r"\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})|\\x([0-9a-fA-F]{2})|\\([0-7]{3})")
ID_ONLY = frozenset("""BeginTable BeginChild BeginTabBar PushID GetID OpenPopup BeginPopup BeginPopupContextItem
BeginPopupContextWindow IsPopupOpen InvisibleButton ArrowButton TreePush""".split())
STRING = r'"(?:[^"\\]|\\.)*"'
SOURCE_TOKEN = re.compile(rf"{STRING}|'(?:[^'\\]|\\.)'|//[^\n]*|/\*.*?\*/", re.S)
GLYPH_LIST = "tests/data/ui-glyphs.txt"
CALL = re.compile(r"\bImGui::(\w+)\s*\(")
# Le contexte facultatif (`labelOf("Annuler", "refuser")`) est une clé, pas un texte affiché.
CATALOGUED = re.compile(rf"\b(?:ui::)?(?:tr|trf|textf|labelOf)\(\s*{STRING}(?:\s*,\s*{STRING})?")
PRINTF_FIELD = re.compile(r"\\.|%[-+ #0]*\d*(?:\.\d+)?(?:ll|l|z|h)?[a-zA-Z]")
# (extrait, refus attendus) : le contrôle se prouve avant de lire le dépôt (règle n°7), chaque cas est un contre-test.
SELF_TEST = (
    ('ImGui::Text("Bonjour \\"toi\\"", x);', 1), ('ImGui::SetItemTooltip("Lancer");', 1),
    ('ImGui::TextColored(color, "Erreur");', 1), ('ImGui::TextDisabled("%s", "non mesuré");', 1),
    ('ImGui::TreeNodeEx("##id", 0, "Noeud %d", i);', 1), ('ImGui::Button(\n"Valider");', 1),
    ('ImGui::Text(str("Bonjour"));', 1), ('ImGui::Begin(ui::tr("Console"));', 1), ("ImGui::Begin(name);", 1),
    ('ImGui::Text("a"); // ImGui::Text("Bonjour")', 1), ('// ImGui::Text("Bonjour")\nint x;', 0),
    ('ImGui::TextDisabled("%s", ui::tr("ok (fin)"));', 0), ('ImGui::Begin(ui::labelOf("Console").c_str());', 0),
    ('ImGui::Button(ui::labelOf("Annuler", "refuser").c_str());', 0), ('ImGui::Text(ui::tr("b", "c"), "Zut");', 1),
    ('ImGui::BeginTable("passes", 2);', 0), ('ImGui::TreeNodeEx("##entité", 0, "%s", s);', 0),
    # Hors de la liste (SELF_GLYPHS) : aucune police n'est garantie d'avoir le glyphe.
    ('ImGui::Text(ui::tr("Créer ✓"));', 1), ('soonItem("Raccourcis ✓", "M7.7");', 1),
    ('ImGui::Text(ui::tr("640×360 é…"));', 0), ('soonItem("Raccourcis…", "M7.7");', 0),
    ('// "Créer ✓"\nint x;', 0),
)
SELF_GLYPHS = frozenset("é×…")
# (extrait, refus attendus, fichier des icônes, règle des caractères) : la règle Latin-1 d'engine/ (None), les
# échappements qui passeraient entre le contrôle et le test des polices.
SELF_TEST_RULES = (
    ('ImGui::Text(ui::tr("Chargement…"));', 1, False, None), ('ImGui::Text(ui::tr("Créé à 8 °C"));', 0, False, None),
    ('ImGui::Text(ui::tr("œuvre"));', 1, False, None), ('ImGui::Text(ui::tr("Créer ✓"));', 1, False, None),
    ('ImGui::Text(ui::tr("Chargement…"));', 0, False, SELF_GLYPHS),
    ('x = "\\uE037";', 0, True, SELF_GLYPHS), ('x = "\\uE037";', 1, False, SELF_GLYPHS),
    ('x = "\\u2026";', 1, True, SELF_GLYPHS), ('ImGui::Text(ui::tr("Cr\\xc3\\xa9er"));', 1, False, SELF_GLYPHS),
    ('x = "\\U0001F600";', 1, True, SELF_GLYPHS), ('x = "\\303\\251";', 1, False, None), ('x = "a\\n\\x41";', 0, False, None),
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


PRIVATE_USE = (0xE000, 0xF8FF)  # la zone des icônes de Material Symbols


def permitted(char: str, glyphs: frozenset[str] | None) -> bool:
    """Un caractère non ASCII est permis s'il est dans la liste (editor/), ou en Latin-1 (le reste)."""
    return ord(char) <= 0xFF if glyphs is None else char in glyphs


def shows_text(literal: str) -> bool:
    """Vrai si ImGui afficherait ce littéral tel quel, avec une lettre (ou un caractère non ASCII)."""
    return not literal.startswith("##") and re.search(r"[A-Za-z]|[^\x00-\x7f]", PRINTF_FIELD.sub("", literal)) is not None


def escape_value(match: re.Match) -> int:
    """Le code que désigne un échappement (\\u, \\U, \\x ou octal)."""
    for group, base in zip(match.groups(), (16, 16, 16, 8)):
        if group is not None:
            return int(group, base)
    raise ValueError(match.group())


def refusals(text: str, glyphs: frozenset[str] | None,
             icons_file: bool = False) -> tuple[int, list[tuple[int, str]]]:
    """Le nombre d'appels d'ImGui lus, et les (ligne, raison) des refus. `glyphs` : la liste permise, ou None pour
    Latin-1 ; `icons_file` : le fichier des icônes, seul à pouvoir écrire un échappement de la zone d'usage privé."""
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
    for literal in re.finditer(STRING, text):
        line = text.count("\n", 0, literal.start()) + 1
        outside = sorted({c for c in literal.group() if ord(c) > 127 and not permitted(c, glyphs)})
        if outside:
            rule = (f"n'est pas dans {GLYPH_LIST} (l'y ajouter si les polices l'ont : tests/ui_fonts_test.cpp "
                    "le vérifie)" if glyphs is not None else "sort de Latin-1, la police par défaut d'ImGui n'a pas ce glyphe")
            found.append((line, f"« {literal.group()[1:-1]} » : {' '.join(outside)} {rule}"))
        for escape in ESCAPE.finditer(literal.group()):
            code = escape_value(escape)
            if code > 127 and not (icons_file and PRIVATE_USE[0] <= code <= PRIVATE_USE[1]):
                found.append((line, f"l'échappement {escape.group()} écrit un caractère non ASCII qu'aucun contrôle ne "
                                    "voit : l'écrire en clair (et dans la liste), ou une icône dans icons.hpp"))
                break  # un refus par littéral
    return calls, found


def glyphs_of(root: Path) -> frozenset[str]:
    """Les caractères de la liste partagée avec le test des polices. LA règle de lecture, la même que tests/ui_fonts_test.cpp :
    une ligne qui commence par « # » est un commentaire ; l'espace, la tabulation et le retour chariot séparent ; tout autre
    caractère est un glyphe (l'espace insécable aussi) ; un BOM est refusé. Les lignes se coupent sur « \\n » seul."""
    path = root / GLYPH_LIST
    if not path.is_file():
        sys.exit(f"{path} introuvable : le contrôle ne saurait pas quels caractères sont permis")
    text = path.read_text(encoding="utf-8")
    if text.startswith("\ufeff"):
        sys.exit(f"{path} commence par un BOM : il deviendrait un glyphe permis")
    glyphs = frozenset(c for line in text.split("\n") if not line.startswith("#") for c in line if c not in " \t\r")
    if len(glyphs) < 20:
        sys.exit(f"{path} : {len(glyphs)} caractères seulement, la liste est vide ou mal lue")
    return glyphs


def main(root: Path) -> int:
    for snippet, expected in SELF_TEST:
        if len(refusals(snippet, SELF_GLYPHS)[1]) != expected:
            sys.exit(f"le contrôle se trompe sur {snippet!r} : {expected} refus attendus, "
                     f"{refusals(snippet, SELF_GLYPHS)[1]}")
    for snippet, expected, icons_file, rule in SELF_TEST_RULES:
        if len(refusals(snippet, rule, icons_file)[1]) != expected:
            sys.exit(f"le contrôle se trompe sur {snippet!r} (icônes : {icons_file}, règle : {rule}) : {expected} refus "
                     f"attendus, {refusals(snippet, rule, icons_file)[1]}")
    listed = glyphs_of(root)
    failures = []
    for directory, rule in DIRECTORIES.items():
        if not (root / directory).is_dir():
            sys.exit(f"{root / directory} introuvable : le contrôle ne verrait rien")
        calls = 0
        for path in sorted(p for p in (root / directory).rglob("*") if p.suffix in (".cpp", ".hpp")):
            read, found = refusals(path.read_text(encoding="utf-8"), listed if rule == "list" else None,
                                   path.name == ICONS_FILE)
            calls += read
            failures += [f"{path.relative_to(root)}:{line} : {reason}" for line, reason in found]
        if calls == 0:
            sys.exit(f"aucun appel d'ImGui lu sous {directory} : le contrôle ne verrait rien")
    print("\n".join(failures + ["(ui::tr, ui::trf, ui::textf, ui::labelOf ; un identifiant seul prend « ## »)"])
          if failures else f"{len(DIRECTORIES)} dossiers lus, aucun littéral hors du catalogue")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1])))
