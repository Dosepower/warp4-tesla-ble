# SPDX-License-Identifier: Apache-2.0
"""Shared charge-start guard for the clean production source lints.

Forbids any charge-start *vehicle command* in the WARP Tesla module: protocol
action names as identifiers anywhere, and the generic word ``charge_start`` in
any form except the AutoPoll scheduler's boolean demand flag. The exemption is
by exact form, not by prefix/pattern: exactly one declaration
``bool charge_start = false;`` inside ``struct AutoPoll`` of tesla_vehicle.h,
and plain data-member uses ``a.charge_start`` (never followed by a call).
Any call, function declaration, pointer-to-member, string literal, other
receiver or second declaration of ``charge_start`` is a violation.
"""
import re

MODULE_SUFFIXES = ('.cpp', '.h', '.inc')  # .inc files are #included into module TUs
PROTOCOL = re.compile(r'\b(?:start_charging|charging_start|ChargingStartStopAction|chargingStartStopAction|ACTION_CHARGE_START)\b')
# Any identifier containing charge_start (keeps the former substring breadth:
# request_charge_start(), send_charge_start_cmd, ... are violations).
WORD = re.compile(r'\w*charge_start\w*')
FIELD_DECL = re.compile(r'\bbool\s+charge_start\s*=\s*false\s*;')
FIELD_USE = re.compile(r'(?<![\w.>:])a\s*\.\s*charge_start\b(?!\s*\()')
HEADER = 'tesla_vehicle.h'
# The AutoPoll scheduler's local due-time lambda: one definition, plain calls.
DUE_FILE = 'tesla_auto_poll.inc'
DUE_DEF = re.compile(r'\bauto\s+charge_start_due\s*=\s*\[&\]\s*\{')
DUE_CALL = re.compile(r'(?<![\w.>:])charge_start_due\s*\(\s*\)')


def _autopoll_span(text):
    m = re.search(r'\bstruct\s+AutoPoll\s*\{', text)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(text):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return (m.start(), i + 1)
        i += 1
    return None


def violations(files):
    """files: iterable of (name, text). Returns list of (name, offset, snippet)."""
    bad, decls = [], []
    for name, text in files:
        for m in PROTOCOL.finditer(text):
            bad.append((name, m.start(), m.group(0)))
        allowed = set()
        for m in FIELD_USE.finditer(text):
            allowed.add(m.end() - len('charge_start'))
        if name.endswith(HEADER):
            span = _autopoll_span(text)
            for m in FIELD_DECL.finditer(text):
                pos = m.start() + m.group(0).index('charge_start')
                if span and span[0] <= pos < span[1]:
                    decls.append((name, pos)); allowed.add(pos)
        if name.endswith(DUE_FILE):
            defs = list(DUE_DEF.finditer(text))
            if len(defs) == 1:
                allowed.add(defs[0].start() + defs[0].group(0).index('charge_start_due'))
                for m in DUE_CALL.finditer(text):
                    allowed.add(m.start())
        for m in WORD.finditer(text):
            if m.start() not in allowed:
                bad.append((name, m.start(), text[max(0, m.start() - 30):m.end() + 10]))
    if len(decls) > 1:
        bad.extend((n, p, 'duplicate charge_start field declaration') for n, p in decls[1:])
    return bad


def module_files(module):
    return [(str(p), p.read_text()) for p in sorted(module.glob('*')) if p.suffix in MODULE_SUFFIXES]
