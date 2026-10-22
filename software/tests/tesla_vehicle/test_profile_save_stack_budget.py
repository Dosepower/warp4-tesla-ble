#!/usr/bin/env python3
"""Exact ESP32 target-frame regression for the captured profile-Save call chain.

Not an emulator or a live target reproduction. The unchanged installed call chain
reached a saved 192-byte XtExcFrame just 4 bytes above pxStack, inside its 16-byte
canary. Only replace the private_key_presence frame size with the newly compiled
one; all other observed frames, interrupt cost and the 8192-byte allocation stay
fixed. Require 1 KiB additional margin above the canary on that observed path.
"""
import argparse
import json
import os
from pathlib import Path


def check(path: Path) -> dict:
    rows = [line.split('\t') for line in path.read_text().splitlines()
            if 'TeslaBleStorageAdapter::private_key_presence()' in line]
    if len(rows) != 1 or rows[0][2] != 'static':
        raise AssertionError('Need exactly one static exact-target private_key_presence frame')
    candidate_frame = int(rows[0][1])
    installed_frame = 4752
    captured_saved_frame_margin = 4
    canary_bytes = 16
    required_additional_margin = 1024
    replayed_margin = captured_saved_frame_margin + installed_frame - candidate_frame
    result = {
        'kind': 'captured-target-call-chain frame-budget replay, not native stack emulation',
        'installed_frame_bytes': installed_frame,
        'compiled_candidate_frame_bytes': candidate_frame,
        'captured_saved_frame_above_stack_base_bytes': captured_saved_frame_margin,
        'replayed_saved_frame_above_stack_base_bytes': replayed_margin,
        'canary_bytes': canary_bytes,
        'required_additional_margin_bytes': required_additional_margin,
        'pass': replayed_margin >= canary_bytes + required_additional_margin,
    }
    print(json.dumps(result, sort_keys=True))
    if not result['pass']:
        raise AssertionError('Profile Save reproduces insufficient ESP32 stack budget')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    built_usage = Path(__file__).resolve().parents[2] / '.pio/build/warp4_tesla_ble_nimble_review/src/modules/tesla_vehicle/tesla_ble_storage.cpp.su'
    parser.add_argument('stack_usage', type=Path, nargs='?',
                        default=Path(os.environ.get('TESLA_PROFILE_SAVE_STACK_USAGE', built_usage)),
                        help='Exact-target tesla_ble_storage.cpp.su; requires the target build first')
    check(parser.parse_args().stack_usage)
