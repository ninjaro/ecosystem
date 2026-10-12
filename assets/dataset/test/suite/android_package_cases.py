"""Independent package-generation regressions; no application or Android SDK required."""

from pathlib import Path
import runpy
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
GENERATOR = ROOT / "assets/templates/android/generate_package.py.tpl"
MODULE = runpy.run_path(str(GENERATOR))
GENERATE = MODULE["generate_package"]
ANDROID = MODULE["android"]

# Minimal fixture, deliberately includes future Qt data that must pass through.
TEMPLATE = '''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
 package="example" android:versionCode="-- %%INSERT_VERSION_CODE%% --"
 android:versionName="-- %%INSERT_VERSION_NAME%% --">
 <!-- %%INSERT_PERMISSIONS --> <!-- %%INSERT_FEATURES -->
 <application android:label="-- %%INSERT_APP_NAME%% --" android:allowBackup="true">
  <activity android:name="org.qtproject.qt.android.bindings.QtActivity">
   <meta-data android:name="android.app.lib_name" android:value="-- %%INSERT_APP_LIB_NAME%% --"/>
   <meta-data android:name="android.app.arguments" android:value="-- %%INSERT_APP_ARGUMENTS%% --"/>
  </activity>
  <provider android:name="future.QtProvider" android:authorities="${applicationId}.qtprovider"/>
 </application>
</manifest>'''


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.template = self.root / "QtManifest.xml"
        self.template.write_text(TEMPLATE, encoding="utf-8")
        self.source = self.root / "source"
        self.icon = self.source / "assets/brand/logo.svg"
        self.icon.parent.mkdir(parents=True)
        self.icon.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">'
                             '<g id="logo"><rect width="32" height="32" fill="red"/></g></svg>', encoding="utf-8")
        self.output = self.root / "build/package"
        self.config = {"application_id": "org.example.fixture", "allow_backup": False,
                       "permissions": ["android.permission.CAMERA"],
                       "excluded_permissions": ["android.permission.RECORD_AUDIO"],
                       "features": [{"name": "android.hardware.camera.any", "required": False}]}

    def test_settings_and_qt_data_survive(self):
        GENERATE(self.template, self.config, self.source, self.output)
        text = (self.output / "AndroidManifest.xml").read_text()
        for marker in ("INSERT_PERMISSIONS", "INSERT_FEATURES", "INSERT_APP_LIB_NAME",
                       "INSERT_APP_ARGUMENTS", "INSERT_APP_NAME", "INSERT_VERSION_CODE",
                       "INSERT_VERSION_NAME"):
            self.assertIn("%%" + marker, text)
        root = ET.fromstring(text)
        self.assertEqual(root.get("package"), "org.example.fixture")
        self.assertEqual(root.find("application").get(ANDROID("allowBackup")), "false")
        self.assertEqual(root.find("uses-feature").get(ANDROID("required")), "false")
        self.assertIn("future.QtProvider", text)
        self.assertIn("${applicationId}.qtprovider", text)
        self.assertIn('tools:node="remove"', text)
        self.assertEqual(list(self.output.iterdir()), [self.output / "AndroidManifest.xml"])
        self.assertFalse((self.source / "app/android").exists())

    def test_missing_converter_preserves_existing_output(self):
        self.output.mkdir(parents=True)
        (self.output / "sentinel").write_text("working")
        self.config["icon"] = "assets/brand/logo.svg"
        with patch("shutil.which", return_value=None), self.assertRaisesRegex(ValueError, "rsvg-convert"):
            GENERATE(self.template, self.config, self.source, self.output)
        self.assertEqual((self.output / "sentinel").read_text(), "working")

    def test_incompatible_qt_template_is_diagnosed(self):
        self.template.write_text(TEMPLATE.replace("%%INSERT_FEATURES", "removed"))
        with self.assertRaisesRegex(ValueError, "INSERT_FEATURES"):
            GENERATE(self.template, self.config, self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_icon_symlink_cannot_escape_brand(self):
        self.icon.unlink()
        outside = self.root / "outside.svg"
        outside.write_text("<svg/>")
        self.icon.symlink_to(outside)
        self.config["icon"] = "assets/brand/logo.svg"
        with self.assertRaises(ValueError):
            GENERATE(self.template, self.config, self.source, self.output)

    @unittest.skipUnless(shutil.which("rsvg-convert"), "rsvg-convert not installed")
    def test_png_densities_and_stale_output(self):
        self.config.update(icon="assets/brand/logo.svg", icon_export_id="logo", round_icon=True)
        GENERATE(self.template, self.config, self.source, self.output)
        for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                              ("xxhdpi", 144), ("xxxhdpi", 192)):
            png = self.output / "res" / ("mipmap-" + density) / "ic_launcher.png"
            self.assertEqual(struct.unpack(">II", png.read_bytes()[16:24]), (size, size))
        root = ET.parse(self.output / "AndroidManifest.xml").getroot()
        self.assertEqual(root.find("application").get(ANDROID("roundIcon")), "@mipmap/ic_launcher")
        del self.config["icon"]
        del self.config["icon_export_id"]
        del self.config["round_icon"]
        GENERATE(self.template, self.config, self.source, self.output)
        self.assertFalse((self.output / "res").exists())


if __name__ == "__main__":
    unittest.main()
