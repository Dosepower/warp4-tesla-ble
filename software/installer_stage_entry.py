"""Fixed immutable payload for the existing named materializer.

Only host-created, readonly bundled argv is used. No build-supplied completion or
record file is consulted. This module deliberately has no approval API.
"""
import json
import os

with open('/snapshot/stage-control/argv.json', encoding='utf-8') as stream:
    argv = json.load(stream)
if not isinstance(argv, list) or not argv or not all(isinstance(a, str) for a in argv):
    raise ValueError('STAGE_ENTRY_ARGV')
if not argv[0].startswith('/snapshot/') or '..' in argv[0].split('/'):
    raise ValueError('STAGE_ENTRY_TOOL')
os.execv(argv[0], argv)
