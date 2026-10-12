"""Parse authored Markdown with MyST and resolve category-relative media links."""
from urllib.parse import urlsplit

from .inputs import require


def markdown(source, prefix, carousel):
    from docutils import nodes
    from docutils.core import publish_parts
    from myst_parser.parsers.docutils_ import Parser

    class asset_parser(Parser):
        def parse(self, text, document):
            super().parse(text, document)
            for node in document.findall(nodes.Element):
                for key in ('uri', 'refuri'):
                    url = node.get(key)
                    if url and not url.startswith(('/', '#')) and not urlsplit(url).scheme:
                        node[key] = prefix + url
            position = 0
            for node in list(document.findall(nodes.raw)):
                if node.astext().strip() == '<carousel/>':
                    position += 1
                    node.replace_self(nodes.raw('', carousel(position), format='html'))
            require(position, 'paired readme requires a <carousel/> block outside code')

    # Includes cannot bypass the index's selected file set.
    return publish_parts(source, parser=asset_parser(), writer_name='html5', settings_overrides={
        'doctitle_xform': False, 'halt_level': 2, 'report_level': 2,
        'file_insertion_enabled': False,
        'myst_all_links_external': True,
    })['fragment']
