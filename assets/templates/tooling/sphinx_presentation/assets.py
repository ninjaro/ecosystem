"""Render category indexes without changing media or index ownership."""
import csv
import html
import io
from pathlib import Path
from urllib.parse import quote

from .inputs import link, require, safe_path
from .previews import preview

CATEGORIES = ('brand', 'showcase', 'docs', 'reports', 'dataset', 'dumps', 'sets', 'templates')
COLUMNS = ['id', 'path', 'type', 'description', 'datetime']
EXTENSIONS = {'image': {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.avif'},
              'video': {'.mp4', '.webm'}, 'pdf': {'.pdf'}}


def index_rows(inputs, category):
    name = f'assets/{category}/index.tsv'
    if not safe_path(inputs.root, name).exists():
        return None
    text = inputs.read(name).decode('utf-8')
    require('\0' not in text, 'NUL in asset index: ' + name)
    table = csv.reader(io.StringIO(text, newline=''), delimiter='\t', strict=True)
    require(next(table, None) == COLUMNS, 'invalid asset columns: ' + name)
    ids, paths, rows = set(), set(), []
    for values in table:
        require(len(values) == len(COLUMNS), 'invalid asset row: ' + name)
        row = dict(zip(COLUMNS, values))
        require(row['id'] and row['type']
                and not any(ord(ch) < 32 or ord(ch) == 127 for ch in row['id'] + row['type']),
                'missing or invalid asset identity: ' + name)
        require(row['id'] not in ids and row['path'] not in paths,
                'duplicate asset identity or path: ' + name)
        ids.add(row['id'])
        paths.add(row['path'])
        safe_path(inputs.root, f"assets/{category}/{row['path']}")
        require(row['path'] not in ('index.tsv', 'readme.md'), 'index selects its metadata')
        kind = row['type']
        require((kind not in EXTENSIONS and category != 'showcase')
                or Path(row['path']).suffix.lower() in EXTENSIONS.get(kind, set()),
                'unsupported asset media: ' + row['path'])
        rows.append(row)
    return rows


def carousel(rows, prefix, token, title, controls):
    if not rows:
        return '<p>No indexed ' + html.escape(title.lower()) + ' media is available.</p>'
    body = ('<link rel="stylesheet" href="' + controls + 'carousel.css">'
            '<script defer src="' + controls + 'carousel.js"></script>'
            '<section class="ecosystem-carousel" role="region" aria-roledescription="carousel"'
            ' aria-label="' + html.escape(title, quote=True) + '">'
            '<div class="carousel-controls" hidden>'
            '<button type="button" data-step="-1" aria-controls="' + token + '">Previous</button>'
            '<span class="carousel-status" aria-live="polite"></span>'
            '<button type="button" data-step="1" aria-controls="' + token + '">Next</button></div>'
            '<ol class="carousel-track" id="' + token + '" tabindex="0" aria-label="' +
            html.escape(title + ' slides', quote=True) + '">')
    for position, row in enumerate(rows, 1):
        destination = prefix + row['path']
        url = quote(destination, safe='/')
        label = row['description'] or row['id']
        body += (f'<li class="carousel-slide" role="group" aria-roledescription="slide"'
                 f' aria-label="{position} of {len(rows)}"><figure>')
        if row['type'] == 'image':
            body += '<img loading="lazy" src="' + url + '" alt="' + html.escape(label, quote=True) + '">'
        elif row['type'] == 'video':
            body += ('<video controls preload="metadata" aria-label="' + html.escape(label, quote=True)
                     + '" poster="' + controls + row['preview'] + '"><source src="' + url + '">'
                     + link(destination, label) + '</video>')
        elif row['type'] == 'pdf':
            body += ('<a href="' + url + '"><img loading="lazy" src="' + controls + row['preview']
                     + '" alt="' + html.escape('First page: ' + label, quote=True) + '"></a>')
        body += '<figcaption>' + link(destination, label)
        if row['datetime']:
            body += '<p>' + html.escape(row['datetime']) + '</p>'
        body += '</figcaption></figure></li>'
    return body + '</ol></section>'


def readme_body(inputs, category, rows, controls, prefix):
    name = f'assets/{category}/readme.md'
    title = category.title()
    if not safe_path(inputs.root, name).exists():
        return carousel(rows, prefix, category, title, controls)
    source = inputs.read(name).decode('utf-8')
    require('<carousel/>' in source, name + ': indexed assets require <carousel/>')
    from .readme import markdown
    return markdown(source, prefix,
                    lambda position: carousel(rows, prefix, f'{category}-{position}', title, controls))


def asset_pages(inputs):
    pages, links = [], []
    showcase = '<p>No indexed showcase media is available.</p>'
    for category in CATEGORIES:
        rows = index_rows(inputs, category)
        if rows is None:
            continue
        directory = 'showcase' if category == 'showcase' else 'assets/' + category
        controls = '../../'
        for row in rows:
            name = f"assets/{category}/{row['path']}"
            data = inputs.attach(name, directory + '/' + row['path'])
            if row['type'] in ('pdf', 'video'):
                row['preview'] = preview(inputs, row['type'], name, data)
        page = ('<h1>' + category.title() + '</h1>'
                + readme_body(inputs, category, rows, controls, controls + directory + '/'))
        pages.append(('_ecosystem/catalog/' + category + '/index',
                      {'title': category.title(), 'body': page}, 'page.html'))
        links.append('<li>' + link('catalog/' + category + '/index.html', category.title()) + '</li>')
        if category == 'showcase':
            showcase = carousel(rows, 'showcase/', 'showcase-summary', 'Showcase', '')
    if pages:
        for name in ('carousel.css', 'carousel.js'):
            inputs.generated(name, Path(__file__).with_name(name).read_bytes())
    navigation = '<h2>Asset indexes</h2><ul>' + ''.join(links) + '</ul>' if links else ''
    return showcase + navigation, pages
