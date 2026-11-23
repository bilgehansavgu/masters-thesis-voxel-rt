"""Checks the rules of ARCHITECTURE.md that a script can check. Run by ctest as architecture_check.

1. A tracer or encoding name appears in a string only inside the plugin's own folder
   (src/tracers/<name>/, src/encodings/<family>/, shaders/primitives/<name>/), or in a test of
   that plugin: a test file that includes the plugin's own headers. Code elsewhere, and tests
   that run every plugin, ask the plugin (ITracer, IEncoding) instead of naming it.
2. Every header says its side under #pragma once: // Side: CPU | GPU | Shared | View
   (shared/ headers and <Name>Shared.h of a plugin: /* Side: Shared */ on the first line).
3. A header with side CPU, Shared or View includes no Vulkan header and no project header with
   side GPU. CPU code runs in tests without a GPU; window code reads what the GPU work made as
   plain data, it never records GPU commands itself.
4. Rule 3 holds for each .cpp file too, with the side of its header: Foo.cpp and FooPart.cpp are
   the code of the Foo.h beside them (the header with the longest name the .cpp name starts with).
   A .cpp file without such a header (main.cpp, a test) has no side.

Exit code 0 = every rule holds. Each break is printed as file:line: message.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SIDES = ("CPU", "GPU", "Shared", "View")
REGISTRATION = re.compile(r'registry\.add\(\s*"([^"]+)"')
# Every registration, with its first argument: a name the check cannot read is a name it cannot hold.
ANY_REGISTRATION = re.compile(r'registry\.add\(\s*(\S)')
STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SIDE = re.compile(r"^(?://|/\*) Side: (\w+)")
# Headers that bring Vulkan in: the Vulkan headers themselves and the libraries built on them.
VULKAN = re.compile(r"(vulkan/|vk_mem_alloc\.h|VkBootstrap\.h)")


INCLUDE = re.compile(r'^\s*#include [<"]([^>"]+)[>"]')


def plugins(problems):
    """Name of each tracer and encoding -> (the folders that may name it, the include prefix of
    the plugin's own headers)."""
    owners = {}
    for socket in ("tracers", "encodings"):
        for source in (ROOT / "src" / socket).rglob("*.cpp"):
            # Without line comments, as check_names reads code: a comment that shows a call is no call.
            text = "\n".join(line.split("//", 1)[0] for line in source.read_text(encoding="utf-8").splitlines())
            for match in ANY_REGISTRATION.finditer(text):
                if match.group(1) != '"':
                    number = text.count("\n", 0, match.start()) + 1
                    problems.append(f"{source.relative_to(ROOT)}:{number}: registers a plugin under a name that is "
                                    "not a string literal; this check reads names only from registry.add(\"name\", ...)")
            for name in REGISTRATION.findall(text):
                plugin_folder = source.relative_to(ROOT / "src" / socket).parts[0]
                headers = ROOT / "src" / socket / plugin_folder / "include"
                prefixes = [f"{d.relative_to(headers).as_posix()}/" for d in headers.glob("thesis/*/*")
                            if d.is_dir()] if socket == "tracers" else \
                           [f"{d.relative_to(headers).as_posix()}/" for d in headers.glob("thesis/*") if d.is_dir()]
                owners[name] = ([ROOT / "src" / socket / plugin_folder, ROOT / "shaders" / "primitives" / name],
                                prefixes)
    return owners


def includes_of(lines):
    return [m.group(1) for line in lines if (m := INCLUDE.match(line))]


def names_in(literal, owners):
    """Plugin names a string literal is: the whole name, or a part of names cut at "_" ("brick2012_",
    "_mip"). Prose that happens to contain a name ("GPU hardware") is not a name."""
    if literal in owners:
        return [literal]
    if len(literal) >= 4 and (literal.startswith("_") or literal.endswith("_")):
        return [name for name in owners if name.startswith(literal) or name.endswith(literal)]
    return []


def check_names(owners, problems):
    for folder in ("src", "shaders", "tests"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix not in (".h", ".cpp", ".slang"):
                continue
            lines = path.read_text(encoding="utf-8").splitlines()
            included = includes_of(lines) if folder == "tests" else []
            for number, line in enumerate(lines, 1):
                if line.lstrip().startswith("#include"):
                    continue
                code = line.split("//", 1)[0]
                for literal in STRING.findall(code):
                    for name in names_in(literal, owners):
                        folders, prefixes = owners[name]
                        if any(path.is_relative_to(owner) for owner in folders):
                            continue
                        if any(header.startswith(prefix) for header in included for prefix in prefixes):
                            continue  # a test of this plugin
                        problems.append(f"{path.relative_to(ROOT)}:{number}: names plugin '{name}' outside its "
                                        "folder; ask the plugin through its interface instead")


def public_header(include):
    """The file of a project header named the way other libraries include it, or None."""
    if not include.startswith("thesis/"):
        return None
    for root in [ROOT / "shared" / "include", *(ROOT / "src").rglob("include")]:
        if (root / include).is_file():
            return root / include
    return None


def check_sides(problems):
    folders = ("src", "shared", "tests")
    headers = [p for folder in folders for p in (ROOT / folder).rglob("*.h")]
    side_of = {}
    for path in headers:
        lines = path.read_text(encoding="utf-8").splitlines()
        sides = [m.group(1) for line in lines[:4] if (m := SIDE.match(line))]
        side_of[path] = sides[0] if len(sides) == 1 and sides[0] in SIDES else None
    for path in sorted(headers):
        if side_of[path] is None:
            problems.append(f"{path.relative_to(ROOT)}:1: needs one side tag under #pragma once: "
                            f"// Side: {' | '.join(SIDES)}")

    # A .cpp file is the code of the header beside it that its name starts with, so it has that side.
    def side_of_source(source):
        owners = [h for h in headers if h.parent == source.parent and source.stem.startswith(h.stem)]
        return side_of[max(owners, key=lambda h: len(h.stem))] if owners else None

    sources = [p for folder in folders for p in (ROOT / folder).rglob("*.cpp")]
    files = [(p, side_of[p]) for p in headers] + [(p, side_of_source(p)) for p in sources]
    for path, side in sorted(files):
        if side not in ("CPU", "Shared", "View"):
            continue
        where = path.relative_to(ROOT)
        lines = path.read_text(encoding="utf-8").splitlines()
        for number, line in enumerate(lines, 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            include = m.group(1)
            if VULKAN.match(include):
                problems.append(f"{where}:{number}: side {side} includes the Vulkan header '{include}'")
                continue
            target = public_header(include) or (path.parent / include).resolve()
            if side_of.get(target) == "GPU" or (include.startswith("thesis/gpu/") and target not in side_of):
                advice = ("read what the GPU work made as plain data instead" if side == "View"
                          else "move the code that needs it to a GPU file")
                problems.append(f"{where}:{number}: side {side} includes '{include}', a GPU header; {advice}")


def main():
    problems = []
    owners = plugins(problems)
    if not owners:
        problems.append("scripts/check_architecture.py: found no plugin registrations")
    check_names(owners, problems)
    check_sides(problems)
    for problem in problems:
        print(problem)
    print(f"architecture check: {len(problems)} problem(s), {len(owners)} plugins")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
