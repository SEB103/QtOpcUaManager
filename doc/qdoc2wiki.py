#!/usr/bin/env python3
"""Convert the QDoc user manual (doc/manual/<lang>/*.qdoc) into GitHub Wiki pages.

The manual uses a small, closed subset of QDoc markup (pages, sections, lists,
tables, one image, and the inline commands \\l, \\c, \\b, \\e, \\appversion).
This script maps that subset to GitHub-flavored Markdown and writes one wiki
page per manual page plus the wiki's _Sidebar.md / _Footer.md.

Usage:
    python doc/qdoc2wiki.py --out <dir> [--version 1.2.3] [--api-url URL]

Page naming (GitHub Wiki page names are global, so non-English pages get a
language prefix):
    en:  Home.md, Getting-Started.md, ...
    de:  de-Home.md, de-Getting-Started.md, ...

Links to the generated API reference (\\l ClassName, "OpcUaManager API
Reference", ...) are rendered as plain text unless --api-url points to a
published copy of the QDoc site's opcuamanager/ directory.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANUAL_DIR = ROOT / "doc" / "manual"
IMAGES_DIR = ROOT / "doc" / "images"
PRODUCT_JSON = ROOT / "packaging" / "product.json"
MANIFEST = ".qdoc2wiki-manifest"       # lists the files this script wrote

DEFAULT_LANG = "en"
LANG_NAMES = {
    "en": "English",
    "de": "Deutsch",
    "fr": "Français",
    "it": "Italiano",
    "ru": "Русский",
    "uk": "Українська",
}

# Titles of the generated API reference topics (doc/api/*.qdoc) and the QDoc
# page they resolve to inside the site's opcuamanager/ directory.
API_TOPICS = {
    "OpcUaManager API Reference": "opcuamanager-module.html",
    "OpcUaManager C++ Classes": "opcuamanager-module.html",
    "OpcUaManager QML Types": "opcuamanager-qmlmodule.html",
    "Base QML Types": "base-qmlmodule.html",
}

# Trailing punctuation that QDoc excludes from a single-word command argument
# (e.g. "\c .uaproj)" documents ".uaproj", not ".uaproj)").
WORD_TRAILING_PUNCT = ".,;:!?)"


# --------------------------------------------------------------------------
# Document model
# --------------------------------------------------------------------------

@dataclass
class Page:
    lang: str
    stem: str                       # "getting-started"
    title: str
    next_title: str | None = None
    blocks: list = field(default_factory=list)
    wiki_base: str = ""             # "Getting-Started", shared by all languages

    @property
    def wiki_name(self) -> str:
        base = self.wiki_base or self.stem
        return base if self.lang == DEFAULT_LANG else f"{self.lang}-{base}"


@dataclass
class Paragraph:
    text: str


@dataclass
class Heading:
    level: int
    text: str


@dataclass
class ListBlock:
    ordered: bool
    items: list = field(default_factory=list)   # list[list[block]]


@dataclass
class Table:
    header: list[str] | None
    rows: list[list[str]]


@dataclass
class Image:
    file: str
    alt: str


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

def strip_comment(text: str) -> list[str]:
    """Return the body lines of the single QDoc comment block in a file."""
    m = re.search(r"/\*!(.*?)\*/", text, re.S)
    if not m:
        raise ValueError("no QDoc comment block found")
    lines = m.group(1).splitlines()
    return [line[4:] if line.startswith("    ") else line.lstrip() for line in lines]


class Parser:
    """Line-oriented parser for the manual's QDoc subset."""

    def __init__(self, lang: str, lines: list[str]):
        self.lang = lang
        self.lines = lines
        self.pos = 0
        self.page = Page(lang=lang, stem="", title="")

    def parse(self) -> Page:
        self.page.blocks = self.parse_blocks(stop=lambda s: False)
        if not self.page.stem or not self.page.title:
            raise ValueError("page is missing \\page or \\title")
        return self.page

    def peek(self) -> str | None:
        return self.lines[self.pos] if self.pos < len(self.lines) else None

    def parse_blocks(self, stop) -> list:
        blocks: list = []
        para: list[str] = []

        def flush():
            if para:
                blocks.append(Paragraph(" ".join(s.strip() for s in para)))
                para.clear()

        while (line := self.peek()) is not None:
            s = line.strip()
            if stop(s):
                flush()
                return blocks
            self.pos += 1
            if not s:
                flush()
                continue
            if s.startswith("\\page "):
                self.page.stem = s[6:].strip().removesuffix(".html")
            elif s.startswith("\\title "):
                self.page.title = s[7:].strip()
            elif s.startswith("\\nextpage "):
                self.page.next_title = s[10:].strip()
            elif s.startswith("\\previouspage "):
                pass
            elif m := re.match(r"\\section(\d)\s+(.*)", s):
                flush()
                blocks.append(Heading(int(m.group(1)), m.group(2).strip()))
            elif m := re.match(r"\\list(?:\s+(\S+))?\s*$", s):
                flush()
                blocks.append(self.parse_list(ordered=m.group(1) is not None))
            elif s == "\\table":
                flush()
                blocks.append(self.parse_table())
            elif s.startswith("\\image "):
                flush()
                parts = s[7:].strip().split(None, 1)
                blocks.append(Image(parts[0], parts[1] if len(parts) > 1 else ""))
            elif s.startswith("\\") and not re.match(r"\\([lcbe]|appversion)\b", s):
                # Any other block-level command is unsupported in this subset.
                raise ValueError(f"unsupported command at line {self.pos}: {s}")
            else:
                para.append(s)
        flush()
        return blocks

    def parse_list(self, ordered: bool) -> ListBlock:
        lst = ListBlock(ordered=ordered)
        while (line := self.peek()) is not None:
            s = line.strip()
            if s == "\\endlist":
                self.pos += 1
                return lst
            if s.startswith("\\li"):
                # Rewrite "\li text" so the item text is parsed as a normal line.
                self.lines[self.pos] = line.replace("\\li", "", 1)
                item = self.parse_blocks(
                    stop=lambda t: t.startswith("\\li") or t == "\\endlist")
                lst.items.append(item)
            elif not s:
                self.pos += 1
            else:
                raise ValueError(f"unexpected text in list at line {self.pos + 1}: {s}")
        raise ValueError("unterminated \\list")

    def parse_table(self) -> Table:
        raw: list[str] = []
        while (line := self.peek()) is not None:
            self.pos += 1
            if line.strip() == "\\endtable":
                break
            raw.append(line.strip())
        else:
            raise ValueError("unterminated \\table")
        text = " ".join(raw)
        header = None
        rows = []
        for m in re.finditer(r"\\(header|row)\b(.*?)(?=\\header\b|\\row\b|$)", text, re.S):
            cells = [c.strip() for c in re.split(r"\\li\b", m.group(2))][1:]
            if m.group(1) == "header":
                header = cells
            else:
                rows.append(cells)
        return Table(header, rows)


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

class Renderer:
    def __init__(self, pages: dict[str, dict[str, Page]], version: str, api_url: str | None):
        self.pages = pages
        self.version = version
        self.api_url = api_url.rstrip("/") if api_url else None
        self.warnings: list[str] = []

    # --- inline markup ---------------------------------------------------

    def inline(self, text: str, lang: str) -> str:
        text = text.replace("\\appversion", self.version)
        out = []
        i = 0
        while i < len(text):
            if text[i] == "\\":
                m = re.match(r"\\([lcbe])(?=[\s{])", text[i:])
                if m:
                    cmd = m.group(1)
                    j = i + m.end()
                    arg, j = self.read_argument(text, j)
                    arg2 = None
                    if cmd == "l" and j < len(text) and text[j] == "{":
                        arg2, j = self.read_argument(text, j)
                    out.append(self.render_inline(cmd, arg, arg2, lang))
                    i = j
                    continue
                self.warnings.append(f"unknown inline command near: {text[i:i + 30]!r}")
            out.append(text[i])
            i += 1
        return "".join(out)

    @staticmethod
    def read_argument(text: str, j: int) -> tuple[str, int]:
        """Read a QDoc command argument: {braced group} or a single word."""
        while j < len(text) and text[j] == " ":
            j += 1
        if j < len(text) and text[j] == "{":
            depth = 0
            k = j
            while k < len(text):
                if text[k] == "{":
                    depth += 1
                elif text[k] == "}":
                    depth -= 1
                    if depth == 0:
                        return text[j + 1:k].strip(), k + 1
                k += 1
            raise ValueError("unbalanced braces in inline command")
        k = j
        while k < len(text) and not text[k].isspace():
            k += 1
        word = text[j:k]
        stripped = word.rstrip(WORD_TRAILING_PUNCT)
        if stripped:
            k -= len(word) - len(stripped)
            word = stripped
        return word, k

    def render_inline(self, cmd: str, arg: str, arg2: str | None, lang: str) -> str:
        if cmd == "c":
            return f"`{arg}`"
        if cmd == "b":
            return f"**{self.inline(arg, lang)}**"
        if cmd == "e":
            return f"*{self.inline(arg, lang)}*"
        return self.render_link(arg, arg2, lang)

    def render_link(self, target: str, text: str | None, lang: str) -> str:
        label = self.inline(text, lang) if text else None
        if re.match(r"https?://", target):
            return f"[{label or target}]({target})"
        page = self.pages.get(lang, {}).get(target)
        if page is not None:
            return f"[{label or page.title}]({page.wiki_name})"
        if target in API_TOPICS:
            if self.api_url:
                return f"[{label or target}]({self.api_url}/{API_TOPICS[target]})"
            return label or target
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:]*", target):
            # A C++ class / namespace / QML type in the API reference.
            name = target.split("::")[0].lower()
            if text and "\\" not in text:
                # Unformatted label such as "ServerProject::ProjectData".
                label = f"`{text}`"
            shown = label or f"`{target}`"
            if self.api_url:
                return f"[{shown}]({self.api_url}/{name}.html)"
            return shown
        self.warnings.append(f"[{lang}] unresolved link target: {target!r}")
        return label or target

    # --- blocks ----------------------------------------------------------

    def blocks(self, blocks: list, lang: str, indent: str = "") -> list[str]:
        out: list[str] = []
        for b in blocks:
            if out and out[-1] != "":
                out.append("")
            if isinstance(b, Paragraph):
                out.append(indent + self.inline(b.text, lang))
            elif isinstance(b, Heading):
                out.append(indent + "#" * (b.level + 1) + " " + self.inline(b.text, lang))
            elif isinstance(b, Image):
                out.append(f"{indent}![{b.alt}](images/{b.file})")
            elif isinstance(b, Table):
                out.extend(self.table(b, lang, indent))
            elif isinstance(b, ListBlock):
                out.extend(self.list_block(b, lang, indent))
        return out

    def list_block(self, lst: ListBlock, lang: str, indent: str) -> list[str]:
        out: list[str] = []
        for n, item in enumerate(lst.items, 1):
            marker = f"{n}. " if lst.ordered else "- "
            sub = indent + " " * len(marker)
            lines = self.blocks(item, lang, sub)
            if not lines:
                out.append(indent + marker.rstrip())
                continue
            lines[0] = indent + marker + lines[0][len(sub):]
            out.extend(lines)
        return out

    def table(self, t: Table, lang: str, indent: str) -> list[str]:
        width = max(len(r) for r in ([t.header] if t.header else []) + t.rows)
        header = t.header or [""] * width

        def row(cells: list[str]) -> str:
            cells = cells + [""] * (width - len(cells))
            return indent + "| " + " | ".join(
                self.inline(c, lang).replace("|", "\\|") for c in cells) + " |"

        return [row(header), indent + "|" + " --- |" * width] + [row(r) for r in t.rows]

    # --- pages -----------------------------------------------------------

    def language_bar(self, page: Page) -> str:
        parts = []
        for lang in sorted(self.pages, key=lambda l: (l != DEFAULT_LANG, l)):
            sibling = next((p for p in self.pages[lang].values() if p.stem == page.stem), None)
            name = LANG_NAMES.get(lang, lang)
            if lang == page.lang:
                parts.append(f"**{name}**")
            elif sibling is not None:
                parts.append(f"[{name}]({sibling.wiki_name})")
        return " · ".join(parts)

    def page(self, page: Page) -> str:
        lines = [self.language_bar(page), "", f"# {self.inline(page.title, page.lang)}", ""]
        lines.extend(self.blocks(page.blocks, page.lang))
        return "\n".join(lines).rstrip() + "\n"

    def ordered_pages(self, lang: str) -> list[Page]:
        """Pages of one language in \\nextpage chain order, starting at index."""
        by_title = self.pages[lang]
        by_stem = {p.stem: p for p in by_title.values()}
        order = []
        seen = set()
        cur = by_stem.get("index")
        while cur is not None and cur.stem not in seen:
            order.append(cur)
            seen.add(cur.stem)
            cur = by_title.get(cur.next_title) if cur.next_title else None
        order.extend(p for p in by_title.values() if p.stem not in seen)
        return order

    def sidebar(self) -> str:
        out = []
        for lang in sorted(self.pages, key=lambda l: (l != DEFAULT_LANG, l)):
            name = LANG_NAMES.get(lang, lang)
            open_attr = " open" if lang == DEFAULT_LANG else ""
            out.append(f"<details{open_attr}>")
            out.append(f"<summary><b>{name}</b></summary>")
            out.append("")
            for p in self.ordered_pages(lang):
                out.append(f"- [{p.title}]({p.wiki_name})")
            out.append("")
            out.append("</details>")
            out.append("")
        return "\n".join(out)

    def footer(self, homepage: str) -> str:
        return (f"OpcUaManager {self.version} · generated from `doc/manual/` · "
                f"[Project home]({homepage})\n")


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def load_pages() -> dict[str, dict[str, Page]]:
    pages: dict[str, dict[str, Page]] = {}
    for lang_dir in sorted(MANUAL_DIR.iterdir()):
        if not (lang_dir / "index.qdoc").is_file():
            continue
        lang = lang_dir.name
        pages[lang] = {}
        for f in sorted(lang_dir.glob("*.qdoc")):
            try:
                page = Parser(lang, strip_comment(f.read_text(encoding="utf-8"))).parse()
            except ValueError as e:
                raise SystemExit(f"{f}: {e}")
            pages[lang][page.title] = page
    # Wiki page names are global; derive one ASCII name per manual page from
    # the English title and reuse it (with a language prefix) for every language.
    base_by_stem = {"index": "Home"}
    for page in pages.get(DEFAULT_LANG, {}).values():
        base_by_stem.setdefault(page.stem, re.sub(r"[^A-Za-z0-9]+", "-", page.title).strip("-"))
    for by_title in pages.values():
        for page in by_title.values():
            page.wiki_base = base_by_stem.get(page.stem, page.stem)
    return pages


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True, type=Path, help="output directory (wiki checkout)")
    ap.add_argument("--version", help="documentation version (default: packaging/product.json)")
    ap.add_argument("--api-url", help="base URL of a published API reference (opcuamanager/)")
    ap.add_argument("--clean", action="store_true",
                    help=f"remove the files listed in --out/{MANIFEST} (a previous run) before writing")
    args = ap.parse_args()

    product = json.loads(PRODUCT_JSON.read_text(encoding="utf-8"))
    version = args.version or product["version"]
    homepage = product.get("homepage", "")

    pages = load_pages()
    if DEFAULT_LANG not in pages:
        raise SystemExit(f"no {DEFAULT_LANG} manual under {MANUAL_DIR}")
    renderer = Renderer(pages, version, args.api_url)

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    manifest = out / MANIFEST
    if args.clean and manifest.is_file():
        # Only files from a previous run are removed; hand-written wiki pages
        # that live next to the generated ones are left alone.
        for rel in manifest.read_text(encoding="utf-8").splitlines():
            (out / rel).unlink(missing_ok=True)

    generated: list[str] = []

    def write(rel: str, text: str) -> None:
        (out / rel).write_text(text, encoding="utf-8", newline="\n")
        generated.append(rel)

    images: set[str] = set()
    for by_title in pages.values():
        for page in by_title.values():
            write(f"{page.wiki_name}.md", renderer.page(page))
            images.update(b.file for b in page.blocks if isinstance(b, Image))
    write("_Sidebar.md", renderer.sidebar())
    write("_Footer.md", renderer.footer(homepage))
    if images:
        (out / "images").mkdir(exist_ok=True)
        for name in sorted(images):
            src = IMAGES_DIR / name
            if not src.is_file():
                renderer.warnings.append(f"missing image: {src}")
                continue
            shutil.copy2(src, out / "images" / name)
            generated.append(f"images/{name}")
    manifest.write_text("\n".join(generated) + "\n", encoding="utf-8", newline="\n")

    for w in sorted(set(renderer.warnings)):
        print(f"warning: {w}", file=sys.stderr)
    pages_written = sum(len(v) for v in pages.values())
    print(f"wrote {pages_written} pages for {', '.join(pages)} to {out}")
    return 1 if renderer.warnings else 0


if __name__ == "__main__":
    sys.exit(main())
