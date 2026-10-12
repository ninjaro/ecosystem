"""Bounded project-owned inputs and HTML text helpers."""
import html
from pathlib import Path
import stat
from urllib.parse import quote

LIMIT = 256 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError('Project presentation: ' + message)


def safe_path(root, name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name
            and all(p not in ('', '.', '..') for p in name.split('/')),
            'unsafe input path: ' + str(name))
    current = Path(root)
    for part in name.split('/'):
        current /= part
        require(not current.is_symlink(), 'symlink input: ' + name)
    return current


class Inputs:
    def __init__(self, root):
        self.root = Path(root)
        self.total = 0
        self.files = {}

    def read(self, name):
        path = safe_path(self.root, name)
        require(path.is_file() and stat.S_ISREG(path.stat().st_mode), 'missing file: ' + name)
        require(path.stat().st_size <= LIMIT - self.total, 'inputs exceed 256 MiB')
        data = path.read_bytes()
        self.total += len(data)
        require(self.total <= LIMIT, 'inputs exceed 256 MiB')
        return data

    def attach(self, name, destination):
        data = self.read(name)
        require(destination not in self.files, 'duplicate output: ' + destination)
        self.files[destination] = data
        return data

    def generated(self, destination, data):
        require(destination not in self.files, 'duplicate output: ' + destination)
        require(len(data) <= LIMIT - self.total, 'inputs exceed 256 MiB')
        self.total += len(data)
        self.files[destination] = data


def pre(value):
    return '<pre>' + html.escape(str(value)) + '</pre>'


def link(destination, label):
    return '<a href="' + quote(destination, safe='/') + '">' + html.escape(label) + '</a>'
