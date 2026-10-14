"""Exercise generated presentation code without requiring Sphinx."""
import importlib.util
from importlib.machinery import SourceFileLoader
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
module_path = Path(sys.argv.pop(1))
sys.path.insert(0, str(module_path.parent))
loader = SourceFileLoader("ecosystem_presentation", str(module_path))
spec = importlib.util.spec_from_loader(loader.name, loader)
presentation = importlib.util.module_from_spec(spec)
sys.modules[loader.name] = presentation
loader.exec_module(presentation)

from presentation.assets import AssetsTests
from presentation.composition import CompositionTests
from presentation.verification import VerificationTests
from presentation.previews import PreviewTests
from presentation.diagnostics import DiagnosticTests
from presentation.benchmarks import BenchmarkTests


if __name__ == "__main__":
    unittest.main()
