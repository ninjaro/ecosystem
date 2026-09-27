import os

project = {{project_name}}
extensions = [
    'breathe',
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
html_theme = os.environ.get(
    'MANIFESTO_SPHINX_THEME',
    os.environ.get('ECOSYSTEM_SPHINX_THEME', 'sphinx_rtd_theme'),
)
html_static_path = []
