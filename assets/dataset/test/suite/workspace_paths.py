"""Local browser-validation paths; independent of the umbrella workspace."""
from pathlib import Path
import re
import tempfile


def owned_directory(root, relative):
    current = Path(root)
    if current.is_symlink() or not current.is_dir():
        raise ValueError("invalid validation root")
    for part in Path(relative).parts:
        if part in ("..", ".", "/"):
            raise ValueError("invalid validation path")
        current /= part
        if current.is_symlink():
            raise ValueError("symlink validation directory: " + str(current))
        current.mkdir(mode=0o700, exist_ok=True)
    return current


def browser_workspace(project, label):
    # The browser fixture/site/profile is reproducible state. Preserve it,
    # including failures, rather than deleting the only diagnostic evidence.
    if not re.fullmatch(r"[A-Za-z0-9_-]+", label):
        raise ValueError("invalid browser validation label")
    parent = owned_directory(project, ".ecosystem/agents/browser")
    return Path(tempfile.mkdtemp(prefix=label + "-", dir=parent))


def screenshot_path(project, run, requested=None):
    owned_directory(project, "assets/dataset/test")
    base = owned_directory(project, "assets/dumps/test/.agent_cache")
    if requested is None:
        return owned_directory(project, base.relative_to(project) / run / "screenshots") / "no-js.png"
    target = Path(requested).absolute()
    if not target.is_relative_to(base) or target.is_symlink():
        raise ValueError("browser screenshots must be under " + str(base))
    owned_directory(project, target.parent.relative_to(project))
    return target
