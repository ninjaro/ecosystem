import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from ecosystem_documentation_boundary import Boundary

authored_source = Path(__file__).resolve().parents[2] / 'assets' / 'docs'
for directory in (authored_source.parent, authored_source):
    if directory.is_symlink():
        raise ValueError('documentation-source error: symlink source: ' + str(directory))
boundary_errors = Boundary(authored_source).check()
if boundary_errors:
    raise ValueError('documentation-source error:\n' + '\n'.join(boundary_errors))


def setup(app):
    if Path(os.path.abspath(app.srcdir)) != authored_source:
        raise ValueError('documentation-source error: Sphinx source must be assets/docs/')

project = {{project_name}}
extensions = [
    'breathe',
    'ecosystem_presentation',
{{extensions}}]
breathe_projects = {
    project: os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'doxygen', 'xml')),
}
breathe_default_project = project
templates_path = []
exclude_patterns = {{exclude_patterns}}
myst_heading_anchors = 6
source_suffix = {
    '.rst': 'restructuredtext',
{{source_suffix_markdown}}}
master_doc = 'index'
html_theme = os.environ.get('ECOSYSTEM_SPHINX_THEME', 'sphinx_rtd_theme')
html_static_path = []
