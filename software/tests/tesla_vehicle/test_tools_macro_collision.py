#!/usr/bin/env python3
"""Firmware-preprocessor collision probe for the Tesla module sources.

The host mocks never include the real src/tools.h, so a Tesla identifier that
collides with one of its macros compiles on the host but breaks the firmware
build (e.g. a lambda named `defer` vs. `#define defer auto DEFER(__LINE__) =
defer_dummy{} *[&]()`). This probe reads the REAL macro set from src/tools.h
and rejects, in every tesla_vehicle source (comments/strings stripped):
  * any object-like macro name used as an identifier other than its intended
    statement form `NAME {` (e.g. `defer { ... };`), and
  * any declaration/assignment of a macro name (`NAME =`).
Usage: test_tools_macro_collision.py [software-root]
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
TOOLS = ROOT / 'src' / 'tools.h'
MODULE = ROOT / 'src' / 'modules' / 'tesla_vehicle'

DEFINE = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)(\()?', re.M)
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def macros(text):
    object_like, function_like = set(), set()
    for name, paren in DEFINE.findall(text):
        (function_like if paren else object_like).add(name)
    return object_like, function_like


def strip(text):
    # Keep line structure for diagnostics; drop preprocessor lines (include guards etc.).
    text = STRIP.sub(lambda m: '\n' * m.group(0).count('\n'), text)
    return re.sub(r'^[ \t]*#[^\n]*', '', text, flags=re.M)


def main():
    object_like, function_like = macros(TOOLS.read_text())
    assert 'defer' in object_like, 'probe expects the real src/tools.h defer macro'
    failures = []
    sources = sorted(p for p in MODULE.rglob('*') if p.suffix in ('.cpp', '.h', '.inc') and 'generated' not in p.parts)
    for path in sources:
        code = strip(path.read_text(errors='replace'))
        for name in sorted(object_like | function_like):
            for m in re.finditer(r'(?<![\w.>:])' + re.escape(name) + r'(?!\w)', code):
                rest = code[m.end():m.end() + 8].lstrip()
                line = code.count('\n', 0, m.start()) + 1
                if name in object_like and not rest.startswith('{'):
                    failures.append(f'{path.relative_to(ROOT)}:{line}: identifier `{name}` collides with object-like macro in src/tools.h')
                elif rest.startswith('=') and not rest.startswith('=='):
                    failures.append(f'{path.relative_to(ROOT)}:{line}: `{name}` declared/assigned but is a src/tools.h macro')
    for failure in failures:
        print('RED:', failure)
    print(f'checked {len(sources)} sources against {len(object_like)} object-like / {len(function_like)} function-like tools.h macros')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
