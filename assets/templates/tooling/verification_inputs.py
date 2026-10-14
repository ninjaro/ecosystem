#!/usr/bin/env python3
"""Fingerprint configured translation units; uncertainty means no reusable key.

This is lint/AST evidence, never test, coverage, or CodeQL acceptance.
"""
import concurrent.futures
import hashlib
import functools
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


@functools.lru_cache(maxsize=None)
def digest(file):
    return hashlib.sha256(Path(file).read_bytes()).hexdigest()


@functools.lru_cache(maxsize=None)
def time_dependent(file):
    return bool(re.search(rb"\b__(?:DATE|TIME|TIMESTAMP)__\b", Path(file).read_bytes()))


def inputs(root, database, pid, configuration, tidy):
    root = Path(root).resolve()
    shared = {"schema": 1, "manifest": digest(root / "manifest.json"),
              "helper": digest(__file__), "configuration": digest(configuration),
              "tidy": digest(tidy), "actor": digest(f"/proc/{pid}/exe"),
              "environment": {name: os.environ.get(name) for name in
                              ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
                               "SDKROOT", "COMPILER_PATH", "GCC_EXEC_PREFIX",
                               "LIBRARY_PATH", "LANG", "LC_ALL", "TZ", "PATH",
                               "SOURCE_DATE_EPOCH", "LD_LIBRARY_PATH", "LD_PRELOAD")}}
    libraries = sorted({line.split()[-1] for line in Path(f"/proc/{pid}/maps").read_text().splitlines()
                        if ("libclang" in line or "libLLVM" in line) and line.split()[-1].startswith("/")})
    if not libraries:
        raise ValueError("libclang identity unavailable")
    shared["libclang"] = {name: digest(name) for name in libraries}
    @functools.lru_cache(maxsize=None)
    def runtime_libraries(program):
        result = subprocess.run(["ldd", str(program)], capture_output=True, text=True)
        if result.returncode:
            raise ValueError("tool runtime identity unavailable")
        files = re.findall(r"(?:=>\s+)?(/[^\s]+)\s+\(", result.stdout)
        if not files:
            raise ValueError("tool runtime dependencies unavailable")
        return {name: digest(name) for name in sorted(set(files))}
    shared["tidy_runtime"] = runtime_libraries(tidy)
    shared["actor_runtime"] = runtime_libraries(f"/proc/{pid}/exe")
    driver = shutil.which("clang++")
    if not driver:
        raise ValueError("analysis driver unavailable")
    shared["analysis_driver"] = {"binary": digest(driver), "runtime": runtime_libraries(driver)}
    search = subprocess.run([driver, "-E", "-x", "c++", "/dev/null", "-v"], capture_output=True, text=True)
    if search.returncode or "#include <...> search starts here:" not in search.stderr:
        raise ValueError("analysis include search unavailable")
    directories = search.stderr.split("#include <...> search starts here:", 1)[1].split("End of search list.", 1)[0]
    system_args = [arg for directory in directories.splitlines() if directory.strip()
                   for arg in ("-isystem", directory.strip())]
    # Libclang may have another resource-directory version from the compiler.
    # Its builtin headers must invalidate evidence too, even if -M chose the
    # driver copy. Preserve installed paths rather than assuming a major version.
    builtin_roots = {Path(driver).resolve().parent.parent / "lib/clang"}
    builtin_roots.update(Path(name).resolve().parent / "clang" for name in libraries)
    shared["builtin_headers"] = {str(file): digest(file) for folder in sorted(builtin_roots)
                                 for file in sorted(folder.rglob("*")) if file.is_file()}

    configuration_text = Path(configuration).read_text()
    if "InheritParentConfig" in configuration_text and not re.search(r"(?m)^\s*InheritParentConfig\s*:\s*false\s*$", configuration_text, re.I):
        raise ValueError("unmodelled inherited clang-tidy configuration")
    shared["tidy_version"] = subprocess.check_output([tidy, "--version"], text=True)
    templates = os.environ.get("MANIFESTO_TEMPLATE_ROOT")
    if templates:
        shared["templates"] = {str(file): digest(file) for file in sorted(Path(templates).rglob("*")) if file.is_file()}
    entries = json.loads(Path(database).read_text())

    def key(entry):
        try:
            directory = Path(entry["directory"])
            if not directory.is_absolute():
                raise ValueError("relative compile directory")
            args = entry.get("arguments") or shlex.split(entry["command"])
            compiler = Path(args[0])
            if not compiler.is_absolute() or not compiler.is_file():
                raise ValueError("unknown compiler")
            if any(arg.startswith(("@", "-fmodule", "-include-pch", "--config")) for arg in args):
                raise ValueError("unmodelled response/module/driver configuration")
            if any(arg.startswith(("-fplugin", "-Xclang")) for arg in args):
                raise ValueError("unmodelled compiler plugin")
            source = (directory / entry["file"]).resolve()
            if not source.is_relative_to(root) or source.relative_to(root).parts[0] == ".ecosystem":
                raise ValueError("non-authored compilation input; no cached attestation")
            expansion = subprocess.run(args + ["-###"], cwd=directory, capture_output=True, text=True)
            if expansion.returncode or '"-cc1"' not in expansion.stderr:
                raise ValueError("expanded compiler configuration unavailable")
            command = [str(compiler)]
            skip = False
            for arg in args[1:]:
                if skip:
                    skip = False
                elif arg in ("-o", "-MF", "-MT", "-MQ"):
                    skip = True
                elif arg not in ("-c", "-MD", "-MMD", "-MP"):
                    command.append(arg)
            # -M includes system and external headers; -MM would be unsafe.
            command.extend(system_args + ["-M", "-MT", "manifesto_inputs"])
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            if result.returncode or not result.stdout.startswith("manifesto_inputs:"):
                raise ValueError("compiler dependency extraction failed")
            names = shlex.split(result.stdout.partition(":")[2].replace("\\\n", ""))
            headers = sorted({(directory / name.replace("$$", "$" )).resolve() for name in names} | {source})
            payload = {**shared, "directory": str(directory), "arguments": args, "driver_expansion": expansion.stderr,
                       "compiler": digest(compiler), "compiler_runtime": runtime_libraries(str(compiler)),
                       "dependencies": {str(file): digest(file) for file in headers}}
            # Time-dependent preprocessing cannot safely reuse an older result.
            if any(time_dependent(file) for file in headers):
                raise ValueError("time-dependent input")
            value = hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()
            return str(source), {"key": value, "reason": "configured compiler and complete header closure"}
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            return str((Path(entry.get("directory", root)) / entry.get("file", "")).resolve()), {"reason": str(error)}

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
        rows = list(workers.map(key, entries))
    # Preserve every variant AND its order: libclang selects the first command,
    # while clang-tidy may execute several configured commands for the file.
    grouped = {}
    for name, row in rows:
        grouped.setdefault(name, []).append(row)
    result = {}
    for name, variants in grouped.items():
        if all("key" in row for row in variants):
            value = hashlib.sha256(json.dumps([row["key"] for row in variants]).encode()).hexdigest()
            result[name] = {"key": value, "reason": "all configured variants and complete header closures"}
        else:
            result[name] = {"reason": "unverified configured compilation variant"}
    return result


if __name__ == "__main__":
    try:
        if sys.argv[1] == "--digest":
            print(hashlib.sha256(sys.argv[2].encode()).hexdigest())
        else:
            print(json.dumps({"sources": inputs(*sys.argv[1:])}))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps({"sources": {}, "reason": str(error)}))
