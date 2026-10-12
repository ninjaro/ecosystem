"""Validate local plot inputs without interpreting benchmark logs or CI evidence."""
import json
import math
import re
import xml.etree.ElementTree as xml

from .inputs import require, safe_path

LIMIT = 8 * 1024 * 1024


def file_stamp(data):
    """Match the native change checksum; this does not authenticate a run."""
    checksum = 14695981039346656037
    for value in data:
        checksum = ((checksum ^ value) * 1099511628211) & ((1 << 64) - 1)
    return {'bytes': len(data), 'checksum': f'fnv1a64:{checksum:016x}'}


def read_bounded(inputs, name):
    path = safe_path(inputs.root, name)
    require(path.is_file() and path.stat().st_size <= LIMIT,
            'benchmark plot input is missing or exceeds 8 MiB: ' + name)
    return inputs.read(name)


def validate_series(summary):
    require(isinstance(summary, dict) and isinstance(summary.get('series'), list)
            and summary['series'], 'benchmark summary has no series')
    names = set()
    count = 0
    for series in summary['series']:
        require(isinstance(series, dict) and isinstance(series.get('algorithm'), str)
                and series['algorithm'] not in names, 'invalid benchmark series identity')
        names.add(series['algorithm'])
        points = series.get('points')
        require(isinstance(points, list) and points, 'benchmark series has no points')
        last = -1
        for point in points:
            require(isinstance(point, dict), 'invalid benchmark point')
            size, rate = point.get('n'), point.get('gflops')
            require(type(size) is int and last < size <= 2147483647,
                    'invalid or unordered benchmark size')
            require(type(rate) in (int, float) and math.isfinite(rate) and rate >= 0,
                    'invalid benchmark throughput')
            last = size
            count += 1
    require(count <= 10000, 'benchmark summary exceeds 10000 points')


def validate_svg(data):
    # Generated plots use only static shapes/text. Keep arbitrary project template
    # overrides from adding scripts, external resources or embedded HTML.
    text = data.decode('utf-8')
    require('\x00' not in text, 'benchmark SVG must be UTF-8 XML')
    require('<!DOCTYPE' not in text.upper() and '<!ENTITY' not in text.upper()
            and re.search(r'<\?(?!xml(?:\s|\?))', text, re.IGNORECASE) is None,
            'benchmark SVG declarations are unsupported')
    try:
        root = xml.fromstring(text)
    except xml.ParseError as error:
        raise ValueError('invalid benchmark SVG: ' + str(error)) from error
    namespace = '{http://www.w3.org/2000/svg}'
    require(root.tag == namespace + 'svg', 'invalid benchmark SVG root')
    tags = {'svg', 'rect', 'text', 'circle', 'polyline', 'line', 'g', 'title', 'desc'}
    allowed_tags = {namespace + tag for tag in tags}
    attributes = {'width', 'height', 'viewBox', 'x', 'y', 'x1', 'x2', 'y1', 'y2',
                  'cx', 'cy', 'r', 'fill', 'stroke', 'stroke-width', 'points',
                  'font-size', 'font-family', 'text-anchor', 'transform'}
    for index, element in enumerate(root.iter()):
        require(index < 50000 and element.tag in allowed_tags,
                'unsupported benchmark SVG element')
        for key, value in element.attrib.items():
            require(key in attributes and 'url' not in value.casefold()
                    and re.fullmatch(r'[A-Za-z0-9#.,%()+ \-]*', value),
                    'unsupported benchmark SVG attribute')


def load_plot(inputs, prefix, identity, result, log):
    marker = prefix + 'presentation.json'
    if not safe_path(inputs.root, marker).exists():
        return None
    data = read_bounded(inputs, marker)
    record = json.loads(data)
    require(isinstance(record, dict) and type(record.get('schema')) is int
            and record['schema'] == 1 and record.get('kind') == 'flops'
            and record.get('artifact') == identity, 'invalid benchmark presentation identity')
    expected = {'result.json', 'bench_log.txt', 'summary.json', 'bench_plot.svg'}
    require(isinstance(record.get('files'), dict) and set(record['files']) == expected,
            'invalid benchmark presentation file set')
    require(len(result) <= LIMIT and len(log) <= LIMIT, 'benchmark plot inputs exceed 8 MiB')
    files = {'result.json': result, 'bench_log.txt': log,
             'summary.json': read_bounded(inputs, prefix + 'summary.json'),
             'bench_plot.svg': read_bounded(inputs, prefix + 'bench_plot.svg')}
    for name, value in files.items():
        require(record['files'][name] == file_stamp(value), 'stale benchmark plot input: ' + name)
    summary = json.loads(files['summary.json'])
    validate_series(summary)
    validate_svg(files['bench_plot.svg'])
    files['presentation.json'] = data
    return summary, files
