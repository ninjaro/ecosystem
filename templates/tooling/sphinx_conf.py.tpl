import os

project = "{{project_name}}"
extensions = [
{{extensions}}]
templates_path = []
exclude_patterns = ['_build']
source_suffix = {
    '.rst': 'restructuredtext',
{{source_suffix_markdown}}}
master_doc = 'index'
html_theme = os.environ.get(
    'MANIFESTO_SPHINX_THEME',
    os.environ.get('ECOSYSTEM_SPHINX_THEME', 'sphinx_rtd_theme'),
)
html_static_path = []
