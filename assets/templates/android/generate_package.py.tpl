"""Generate a minimal Qt Android overlay; never vendor Qt's runtime package."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ANDROID = "http://schemas.android.com/apk/res/android"
TOOLS = "http://schemas.android.com/tools"
ET.register_namespace("android", ANDROID)
ET.register_namespace("tools", TOOLS)


def android(name):
    return "{" + ANDROID + "}" + name


def declaration(root, tag, name):
    for child in root.findall(tag):
        if child.get(android("name")) == name:
            return child
    return ET.SubElement(root, tag, {android("name"): name})


def generate_package(template, config, source_root, output, converter=None):
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    tree = ET.parse(template, parser=parser)
    root = tree.getroot()
    app = root.find("application")
    activity = app.find("activity") if app is not None else None
    if root.tag != "manifest" or app is None or activity is None:
        raise ValueError("Qt Android template lacks manifest/application/activity")
    original = Path(template).read_text(encoding="utf-8")
    for marker in ("INSERT_VERSION_CODE", "INSERT_VERSION_NAME", "INSERT_APP_NAME",
                   "INSERT_APP_LIB_NAME", "INSERT_APP_ARGUMENTS", "INSERT_PERMISSIONS",
                   "INSERT_FEATURES"):
        token = "%%" + marker + ("" if marker in ("INSERT_PERMISSIONS", "INSERT_FEATURES") else "%%")
        if token not in original:
            raise ValueError("Qt Android template lacks deployment placeholder " + marker)

    root.set("package", config["application_id"])
    for field, attribute in (("allow_backup", "allowBackup"),
                             ("full_backup_content", "fullBackupContent"),
                             ("full_backup_only", "fullBackupOnly"),
                             ("supports_rtl", "supportsRtl")):
        if field in config:
            app.set(android(attribute), str(config[field]).lower())
    if "activity_config_changes" in config:
        activity.set(android("configChanges"), "|".join(config["activity_config_changes"]))

    # Existing declarations also suppress androiddeployqt's automatic additions.
    # tools:node removal additionally prevents a library manifest reintroducing them.
    for permission in config.get("permissions", []):
        declaration(root, "uses-permission", permission)
    for permission in config.get("excluded_permissions", []):
        declaration(root, "uses-permission", permission).set("{" + TOOLS + "}node", "remove")
    for feature in config.get("features", []):
        declaration(root, "uses-feature", feature["name"]).set(
            android("required"), str(feature["required"]).lower())

    icon = config.get("icon")
    if icon:
        source_root = Path(source_root).resolve()
        source = (source_root / icon).resolve()
        source.relative_to(source_root / "assets" / "brand")
        if not source.is_file() or source.suffix != ".svg":
            raise ValueError("Android icon must be an existing assets/brand SVG: " + icon)
        converter = converter or shutil.which("rsvg-convert")
        if not converter:
            raise ValueError("Android SVG launcher generation requires rsvg-convert "
                             "(librsvg); install it or set ECOSYSTEM_ANDROID_SVG_CONVERTER")
        app.set(android("icon"), "@mipmap/ic_launcher")
        if config.get("round_icon", False):
            app.set(android("roundIcon"), "@mipmap/ic_launcher")
    elif config.get("round_icon", False):
        raise ValueError("Android round_icon requires icon metadata")

    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Keep an existing working overlay intact if template validation or rendering fails.
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output.parent) as temporary:
        staged = Path(temporary)
        if icon:
            for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                                  ("xxhdpi", 144), ("xxxhdpi", 192)):
                target = staged / "res" / ("mipmap-" + density) / "ic_launcher.png"
                target.parent.mkdir(parents=True)
                selection = (["--export-id", config["icon_export_id"]]
                             if "icon_export_id" in config else [])
                subprocess.run([str(converter), "--keep-aspect-ratio",
                                "--width", str(size), "--height", str(size),
                                "--page-width", str(size), "--page-height", str(size),
                                "--output", str(target)] + selection + [str(source)], check=True)
                if target.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError("SVG converter did not produce PNG: " + str(target))
        tree.write(staged / "AndroidManifest.xml", encoding="utf-8", xml_declaration=True)
        if output.exists():
            shutil.rmtree(output)
        shutil.copytree(staged, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-template", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--svg-converter")
    args = parser.parse_args()
    try:
        generate_package(args.qt_template, json.loads(args.config.read_text(encoding="utf-8")),
                         args.source_root, args.output, args.svg_converter)
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        parser.exit(1, "ecosystem Android package generation: " + str(error) + "\n")


if __name__ == "__main__":
    main()
