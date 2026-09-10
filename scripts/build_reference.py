# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Build the authored Godot XML with pinned upstream make_rst and Sphinx."""
import argparse
import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import unquote, urlsplit
from urllib.request import urlopen
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED = {
    "BaristaScript": "Script",
    "BaristaScriptLanguage": "ScriptLanguageExtension",
    "BaristaScriptResourceLoader": "ResourceFormatLoader",
}


def validate_sources(source):
    """Reject missing/private class sources before running the upstream generator."""
    files = sorted(source.rglob("*.xml"))
    found = set()
    for path in files:
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as error:
            raise ValueError(f"{path}:{error.position[0]}: invalid XML: {error}") from error
        name = root.get("name")
        if name not in SUPPORTED:
            raise ValueError(f"{path}:1: unsupported class {name!r}; keep test/private APIs out of doc_classes")
        # Godot's streaming DocTools parser expects explicit closing container tags,
        # although ElementTree and make_rst accept the equivalent self-closing XML.
        text = path.read_text(encoding="utf-8")
        container = re.search(r"<(?:class|tutorials|methods|signals|members|constants|description|brief_description)\b[^>]*?/>", text)
        if container:
            line = text.count("\n", 0, container.start()) + 1
            raise ValueError(f"{path}:{line}: self-closing container is incompatible with Godot editor help")
        if path != source / f"{name}.xml" or name in found:
            raise ValueError(f"{path}:1: class must occur once at doc_classes/{name}.xml")
        found.add(name)
        if root.tag != "class" or root.get("inherits") != SUPPORTED[name]:
            raise ValueError(f"{path}:1: invalid class or inheritance")
        for tag in ("brief_description", "description"):
            if not (root.findtext(tag) or "").strip():
                raise ValueError(f"{path}:1: missing {tag}")
        # Bindings, not C++ public visibility, define the supported class members.
        methods = [method.get("name") for method in root.findall("methods/method")]
        expected = {"get_build_info": "Dictionary", "is_valid": "bool"} if name == "BaristaScript" else {}
        if methods != list(expected) or any(root.find(tag) is not None for tag in ("members", "signals", "constants")):
            raise ValueError(f"{path}:1: documented members differ from supported bindings {list(expected)}")
        for method in root.findall("methods/method"):
            method_name = method.get("name")
            if (method.attrib != {"name": method_name, "qualifiers": "const"}
                    or [item.attrib for item in method.findall("return")] != [{"type": expected[method_name]}]
                    or method.findall("param")):
                raise ValueError(f"{path}:1: documented signature differs from const no-argument instance binding {method_name}")
    missing = SUPPORTED.keys() - found
    if missing:
        raise ValueError(f"{source}: missing supported class XML: {', '.join(sorted(missing))}")
    return files


def extract_toolchain(destination, pin):
    """Verify the immutable upstream archive on every invocation; extract only doc tooling."""
    cache = ROOT / "build/reference-tools"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "godot.tar.gz"
    if not archive.exists():
        url = f"https://codeload.github.com/godotengine/godot/tar.gz/{pin['godot_revision']}"
        print(f"Downloading pinned Godot documentation tooling: {url}", flush=True)
        with urlopen(url, timeout=120) as response, tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
            temporary = Path(output.name)
            try:
                shutil.copyfileobj(response, output)
            except BaseException:
                temporary.unlink(missing_ok=True)
                raise
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin["godot_archive_sha256"]:
        raise ValueError(f"{archive}: checksum mismatch; remove the cached archive and retry")
    prefix = f"godot-{pin['godot_revision']}/"
    with tarfile.open(archive) as bundle:
        for member in bundle:
            if not member.name.startswith(prefix) or not member.isfile():
                continue
            relative = PurePosixPath(member.name[len(prefix):])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"{archive}: unsafe archive path {relative}")
            if not (str(relative).startswith(("doc/classes/", "doc/tools/", "misc/utility/")) or str(relative) == "version.py"):
                continue
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with bundle.extractfile(member) as input_file, target.open("wb") as output:
                shutil.copyfileobj(input_file, output)


def engine_links(engine, version):
    """Map only real engine XML labels to their external Godot reference targets."""
    links = {}
    for path in sorted((engine / "doc/classes").glob("*.xml")):
        root = ET.parse(path).getroot()
        name = root.attrib["name"]
        label = "class_" + name
        url = f"https://docs.godotengine.org/en/{version}/classes/class_{name.lower()}.html"
        links[label.lower()] = url
        for section, item, kind in (("methods", "method", "method"), ("members", "member", "property"),
                                    ("signals", "signal", "signal"), ("constants", "constant", "constant")):
            for member in root.findall(f"{section}/{item}"):
                anchor = f"{label}_{kind}_{member.attrib['name']}"
                links[anchor.lower()] = url + "#" + anchor.lower().replace("_", "-")
                if member.get("enum"):
                    anchor = f"enum_{name}_{member.get('enum')}"
                    links[anchor.lower()] = url + "#" + anchor.lower().replace("_", "-")
    return links


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if attributes.get("id"):
            self.ids.add(attributes["id"])
        for attribute in ("href", "src"):
            if attributes.get(attribute):
                self.links.append(attributes[attribute])


def check_html(output):
    """Fail on absent class pages and local targets/fragments, including raw XML URLs."""
    expected = {f"class_{name.lower()}.html" for name in SUPPORTED}
    actual = {path.name for path in output.glob("class_*.html")}
    if actual != expected:
        raise ValueError(f"{output}: missing generated class page or unexpected page: {sorted(expected ^ actual)}")
    pages = {}
    for path in sorted(output.rglob("*.html")):
        page = Page()
        page.feed(path.read_text(encoding="utf-8"))
        pages[path.resolve()] = page
    for path, page in pages.items():
        for link in page.links:
            parts = urlsplit(link)
            if parts.scheme or parts.netloc:
                continue
            target = (path.parent / unquote(parts.path)).resolve() if parts.path else path
            if not target.is_relative_to(output.resolve()) or not target.is_file():
                raise ValueError(f"{path.name}: broken internal link {link!r}")
            if parts.fragment and target in pages and unquote(parts.fragment) not in pages[target].ids:
                raise ValueError(f"{path.name}: broken internal fragment {link!r}")


def build(source, output, temporary, pin):
    files = validate_sources(source)
    engine = temporary / "godot"
    extract_toolchain(engine, pin)
    rst = temporary / "rst"
    rst.mkdir()
    # Copy XML into a stable relative layout for make_rst's generated source comments.
    authored = engine / "doc_classes"
    authored.mkdir()
    for path in files:
        shutil.copyfile(path, authored / path.name)
    environment = dict(os.environ, SOURCE_DATE_EPOCH="0", PYTHONHASHSEED="0")
    subprocess.run([sys.executable, str(engine / "doc/tools/make_rst.py"),
                    str(engine / "doc/classes"), str(authored), "--filter", r"BaristaScript(?:Language|ResourceLoader)?\.xml$",
                    "--output", str(rst)], check=True, env=environment)
    links = engine_links(engine, pin["godot_docs_version"])
    for name in SUPPORTED:
        path = rst / f"class_{name.lower()}.rst"
        if not path.exists():
            raise ValueError(f"{source / (name + '.xml')}: missing generated class page")
        text = path.read_text(encoding="utf-8")
        text = re.sub(r"^\.\. XML source:.*$", f".. XML source: doc_classes/{name}.xml", text, flags=re.MULTILINE)
        path.write_text(text, encoding="utf-8")
    # Keep the landing page small. All API descriptions remain in authored XML.
    (rst / "index.rst").write_text("BaristaScript API reference\n===========================\n\n"
        "Generated from the same class XML embedded in editor/debug builds.\n\n"
        ".. toctree::\n   :maxdepth: 1\n\n" + "".join(f"   class_{name.lower()}\n" for name in SUPPORTED), encoding="utf-8")
    # The upstream generator emits its own index; use the project landing page above.
    (rst / "class_index.rst").unlink(missing_ok=True)
    (rst / "conf.py").write_text("project = 'BaristaScript'\nmaster_doc = 'index'\n"
        "html_theme = 'alabaster'\nhtml_show_copyright = False\nhtml_show_sphinx = False\n"
        "html_last_updated_fmt = None\nhtml_copy_source = False\nhtml_show_sourcelink = False\n"
        "nitpicky = True\nexclude_patterns = []\n"
        "from docutils import nodes\n"
        f"engine_links = {links!r}\n"
        "def resolve_engine(app, env, node, content):\n"
        "    if node.get('refdomain') == 'std' and node.get('reftype') == 'ref':\n"
        "        url = engine_links.get(node['reftarget'].lower())\n"
        "        if url:\n"
        "            return nodes.reference('', '', content, refuri=url)\n"
        "def setup(app):\n"
        "    app.connect('missing-reference', resolve_engine)\n", encoding="utf-8")
    subprocess.run([sys.executable, "-m", "sphinx", "-n", "-W", "--keep-going", "-b", "html", "-d",
                    str(temporary / "doctrees"), str(rst), str(output)], check=True, env=environment)
    check_html(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="build and validate in temporary storage without changing authored XML")
    mode.add_argument("--output", type=Path, help="write the static reference to this directory (normally build/docs)")
    parser.add_argument("--source", type=Path, default=ROOT / "doc_classes", help="alternate authored XML directory for validation fixtures")
    args = parser.parse_args()
    try:
        pin = json.loads((ROOT / "docs/reference-toolchain.json").read_text())
        # Publish only after a complete successful build; never delete user files.
        with tempfile.TemporaryDirectory(prefix="barista-reference-") as directory:
            temporary = Path(directory)
            generated = temporary / "html"
            build(args.source.resolve(), generated, temporary, pin)
            if args.output:
                shutil.copytree(generated, args.output, dirs_exist_ok=True)
                check_html(args.output)
        print("BaristaScript reference: XML, class pages, and internal links validated")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Documentation build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
