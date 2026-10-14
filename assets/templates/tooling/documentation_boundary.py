#!/usr/bin/env python3
"""Validate a standalone Markdown/RST specification unit, without Sphinx.

Only local inputs and explicit, resolvable document references are accepted.
Code examples are not dependencies. Source-derived directives are not allowed in
the authored unit; implementation-derived API pages are generated separately.
"""

import argparse
import html
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


DOCUMENT_SUFFIXES = {".md", ".markdown", ".rst", ".rest"}
DIRECTIVE = re.compile(r"^(\s*)\.\.\s+([\w-]+)::\s*(.*)$")
ROLE = re.compile(r"(?::|\{)(doc|ref|numref|download|any)(?::|\})`([^`]+)`")


def slug(text):
    text = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]*>", "", text).replace("`", "")
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", text.lower()))


def target_text(value):
    value = value.strip()
    match = re.fullmatch(r".*?<([^<>]+)>", value)
    return match.group(1) if match else value


def active_text(text):
    """Keep directive fences, but mask literal code and RST code blocks."""
    lines = text.splitlines()
    out = []
    fence = None
    literal_indent = None
    for line in lines:
        opening = re.match(r"^\s*(`{3,}|~{3,}|:{3,})(.*)$", line)
        if fence:
            if opening and opening[1][0] == fence[0] and len(opening[1]) >= fence[1]:
                fence = None
                out.append("")
            else:
                out.append(("   " if fence[2] != "eval-rst" else "") + line if fence[2] else "")
            continue
        if opening:
            kind = re.fullmatch(r"\{([\w-]+)\}\s*(.*)", opening[2].strip())
            fence = (opening[1][0], len(opening[1]),
                     kind[1] if kind and kind[1] not in {"code-block", "code", "sourcecode"} else None)
            if fence[2] and kind[1] != "eval-rst":
                out.append(f".. {kind[1]}:: {kind[2]}")
            else:
                out.append("")
            continue
        indent = len(line) - len(line.lstrip())
        if literal_indent is not None:
            if not line.strip() or indent > literal_indent:
                out.append("")
                continue
            literal_indent = None
        code = re.match(r"^(\s*)\.\.\s+(code-block|code|sourcecode)::", line)
        if code or (line.endswith("::") and not DIRECTIVE.match(line)):
            literal_indent = len(code[1]) if code else indent
        out.append("" if code else line)
    return re.sub(r"<!--.*?-->", "", "\n".join(out), flags=re.S)


class Boundary:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.errors = []
        self.references = []
        self.labels = {}
        self.anchors = {}
        self.rst_links = []
        self.rst_targets = {}
        self.visited = set()

    def error(self, source, message):
        self.errors.append(f"{source.relative_to(self.root)}: {message}")

    def local(self, source, value, document=False, sphinx=False):
        value = html.unescape(target_text(value))
        try:
            parts = urlsplit(value)
        except ValueError:
            self.error(source, f"invalid dependency URL: {value}")
            return None
        if parts.scheme or parts.netloc:
            self.error(source, f"external dependency: {value}")
            return None
        path = unquote(parts.path)
        if "\\" in path:
            self.error(source, f"nonportable dependency path: {value}")
            return None
        if path.startswith("/") and not sphinx:
            self.error(source, f"absolute dependency path: {value}")
            return None
        candidate = ((self.root / path.lstrip("/")) if path.startswith("/")
                     else (source.parent / path) if path else source).resolve()
        if not candidate.is_relative_to(self.root):
            self.error(source, f"dependency leaves documentation unit: {value}")
            return None
        if document and not candidate.suffix:
            matches = [candidate.with_suffix(s) for s in (".rst", ".md")
                       if candidate.with_suffix(s).is_file()]
            if len(matches) != 1:
                self.error(source, f"missing or ambiguous document: {value}")
                return None
            candidate = matches[0]
        if not candidate.is_file():
            self.error(source, f"missing dependency: {value}")
            return None
        if document and candidate.suffix.lower() not in DOCUMENT_SUFFIXES:
            self.error(source, f"not a document: {value}")
            return None
        self.references.append((source, candidate, unquote(parts.fragment)))
        return candidate

    def scan(self, source):
        if source in self.visited:
            return
        self.visited.add(source)
        try:
            text = active_text(source.read_text(encoding="utf-8"))
        except (UnicodeError, OSError) as exc:
            self.error(source, f"unreadable document: {exc}")
            return
        lines = text.splitlines()
        anchors = set()
        counts = {}
        for index, line in enumerate(lines):
            heading = re.match(r"^\s{0,3}#{1,6}\s+(.+?)(?:\s+#+)?$", line)
            underline = index + 1 < len(lines) and re.fullmatch(
                r"\s*([=~^\"'`:+*#_-])\1{2,}\s*", lines[index + 1])
            if heading or (line.strip() and underline):
                name = slug(heading[1] if heading else line.strip())
                count = counts.get(name, 0)
                counts[name] = count + 1
                anchors.add(name if not count else f"{name}-{count}")
            label = re.match(r"^\s*(?:\.\. _([^:]+):|\(([^)]+)\)=)\s*$", line)
            if label:
                name = label[1] or label[2]
                if name in self.labels:
                    self.error(source, f"duplicate Sphinx label: {name}")
                self.labels[name] = source
                anchors.add(name)
        def mask_code(match):
            before = text[match.start() - 1:match.start()] if match.start() else ""
            after = text[match.end():match.end() + 1]
            return match[0] if before in {":", "}"} or after == "_" else ""

        text = re.sub(r"(`+)([^`\n]*?)\1", mask_code, text)
        class Links(HTMLParser):
            def handle_starttag(inner, tag, attrs):
                for key, value in attrs:
                    if value is None:
                        continue
                    if key in {"id", "name"}:
                        anchors.add(value)
                    elif key in {"href", "src", "data", "poster"}:
                        self.local(source, value)

        Links().feed(text)
        anchors.update(re.findall(r"\{#([^}]+)\}", text))
        self.anchors[source] = anchors
        self.rst_targets[source] = {slug(m[1]) for m in re.finditer(
            r"^\s*\.\. _([^:]+):", text, re.M)}

        # Markdown inline links, reference definitions, and reference uses.
        definitions = {}
        for match in re.finditer(r"^\s{0,3}\[([^]]+)\]:\s*(<[^>]+>|\S+)", text, re.M):
            key = " ".join(match[1].lower().split())
            definitions[key] = match[2].strip("<>")
            self.local(source, definitions[key])
        links = re.compile(r"!?\[([^]\n]*)\]\(\s*(<[^>]+>|(?:[^\s()]|\([^()]*\))+)"
                           r"(?:\s+[\"'][^\n]*?[\"'])?\s*\)")
        for match in links.finditer(text):
            self.local(source, match[2].strip("<>"))
        without_inline = links.sub("", text)
        for match in re.finditer(r"!?\[([^]\n]+)\](?:\[([^]\n]*)\])?", without_inline):
            if match.start() and without_inline[match.start() - 1] == "\\":
                continue
            key = " ".join((match[2] or match[1]).lower().split())
            if match[2] is not None and key not in definitions:
                self.error(source, f"undefined Markdown reference: {key}")
        rst_inline = re.compile(r"`([^`]+)`_+")
        for match in rst_inline.finditer(text):
            explicit = re.fullmatch(r".*?<([^<>]+)>", match[1])
            if explicit:
                self.local(source, explicit[1])
            elif source.suffix.lower() in {".rst", ".rest"}:
                self.rst_links.append((source, slug(match[1])))
        for match in re.finditer(r"^\s*\.\. _[^:]+:\s+(\S+)", text, re.M):
            self.local(source, match[1])
        for match in re.finditer(r"<(?:[A-Za-z][\w+.-]*:|//)[^<>\s]+>", text):
            self.local(source, match[0][1:-1])
        for match in ROLE.finditer(text):
            target = target_text(match[2])
            if match[1] in {"ref", "numref"}:
                self.references.append((source, None, target))
            elif match[1] == "any":
                self.error(source, "ambiguous Sphinx any-reference needs boundary review")
            else:
                self.local(source, target, document=match[1] == "doc", sphinx=True)

        for index, line in enumerate(lines):
            line = re.sub(r"^(\s*\.\.)\s+\|[^|]+\|\s+", r"\1 ", line)
            directive = DIRECTIVE.match(line)
            if not directive:
                continue
            name, argument = directive[2].lower(), directive[3].strip()
            indent = len(directive[1])
            body = []
            for following in lines[index + 1:]:
                if following.strip() and len(following) - len(following.lstrip()) <= indent:
                    break
                body.append(following.strip())
            if name.startswith(("doxygen", "auto")) or name in {
                "bibliography", "program-output", "exec", "jupyter-execute"}:
                self.error(source, f"source-derived dependency directive: {name}")
            elif name == "toctree":
                for entry in body:
                    if not entry or entry.startswith(":"):
                        continue
                    entry = target_text(entry)
                    if ":glob:" in body and any(c in entry for c in "*?["):
                        # Validate the glob's path boundary before expanding it.
                        prefix = re.split(r"[\*\?\[]", entry, maxsplit=1)[0]
                        parent = (self.root / prefix.lstrip("/")) if entry.startswith("/") else source.parent / prefix
                        if not parent.resolve().is_relative_to(self.root):
                            self.error(source, f"toctree glob leaves documentation unit: {entry}")
                            continue
                        base = self.root if entry.startswith("/") else source.parent
                        matches = sorted(base.glob(entry.lstrip("/") + ".*"))
                        matches = [p for p in matches if p.suffix.lower() in DOCUMENT_SUFFIXES]
                        if not matches:
                            self.error(source, f"empty toctree glob: {entry}")
                        for path in matches:
                            self.local(source, str(path.relative_to(base)), document=True)
                    else:
                        self.local(source, entry, document=True, sphinx=True)
            elif name in {"include", "literalinclude", "image", "figure"}:
                path = self.local(source, argument, sphinx=True)
                if path and name == "include":
                    self.scan(path)
            elif name in {"raw", "csv-table"}:
                for option in body:
                    match = re.match(r":(file|url):\s*(.+)$", option)
                    if match:
                        path = self.local(source, match[2], sphinx=True)
                        if path and name == "raw":
                            self.scan(path)
            elif name in {"eval-rst"}:
                pass  # Its active contents are scanned as RST above.
            elif name not in {
                "note", "warning", "tip", "important", "caution", "attention",
                "admonition", "hint", "danger", "error", "contents", "math",
                "list-table", "table", "rubric", "container", "compound",
                "topic", "sidebar", "class", "role", "only", "versionadded",
                "versionchanged", "deprecated", "highlight", "code-block",
                "code", "sourcecode"}:
                self.error(source, f"unsupported directive needs boundary review: {name}")

    def check(self):
        if not self.root.is_dir():
            return [f"missing documentation unit: {self.root}"]
        entries = [p for p in (self.root / "index.md", self.root / "index.rst") if p.is_file()]
        if len(entries) != 1:
            self.error(self.root / "index", "exactly one Markdown/RST entry point is required")
        for source in sorted(self.root.rglob("*")):
            if source.name == "conf.py":
                self.error(source, "executable Sphinx config needs boundary review")
            if source.is_symlink() and not source.resolve().is_relative_to(self.root):
                self.error(source, "symlink leaves documentation unit")
            elif source.is_file() and source.suffix.lower() in DOCUMENT_SUFFIXES:
                self.scan(source.resolve())
        for source, label in self.rst_links:
            if label not in self.anchors.get(source, set()) | self.rst_targets.get(source, set()):
                self.error(source, f"missing RST hyperlink target: {label}")
        for source, target, fragment in self.references:
            if target is None:
                if fragment not in self.labels:
                    self.error(source, f"missing Sphinx reference: {fragment}")
            elif fragment and fragment not in self.anchors.get(target, set()):
                self.error(source, f"missing anchor {fragment!r} in {target.relative_to(self.root)}")
        return sorted(set(self.errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", type=Path, nargs="+")
    args = parser.parse_args()
    failed = False
    for root in args.roots:
        check = Boundary(root)
        errors = check.check()
        if errors:
            print(f"{root}:\n" + "\n".join(errors))
            failed = True
        else:
            print(f"{root}: self-contained: {len(check.visited)} documents, "
                  f"{len(check.references)} local references")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
