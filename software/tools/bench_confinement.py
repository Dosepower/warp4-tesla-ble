#!/usr/bin/env python3
"""Shared fail-closed confinement for private bench workspace mutations.

Inspect lexical paths BEFORE resolution. Validate the complete transaction before
its first mutation, then recheck captured identities before deletion. This is a
static/substitution defense for an exclusively owned workspace, not protection
against hostile concurrent renames/mounts or a crash-durability guarantee.
"""
import argparse
import os
from pathlib import Path
import shutil
import stat
import sys

EXPORTS = ('ordinary', 'ordinary.unchecked', 'minimal', 'minimal.unchecked')
SDKS = ('warp4-bench-diag-sdk', 'warp4-bench-minimal-sdk')
LOGS = ('idf-export.log',) + tuple(
    variant + '-sdk-' + action + '.log'
    for variant in ('diag', 'minimal')
    for action in ('clean', 'configure', 'begin', 'build', 'finalize')) + tuple(
    variant + '-firmware-' + action + '.log'
    for variant in ('ordinary', 'minimal') for action in ('clean', 'build'))


def lexical(path):
    # Path.absolute does not erase '..', unlike resolve/abspath. Check before
    # either a containment comparison or filesystem access can normalize it.
    path = Path(path)
    if '..' in path.parts:
        raise ValueError('confinement: lexical traversal refused')
    return path.absolute()


def stamp(path, directory):
    try:
        info = path.lstat()
    except FileNotFoundError:
        return None
    if stat.S_ISLNK(info.st_mode):
        raise ValueError('confinement: symlinked path refused: ' + str(path))
    wanted = stat.S_ISDIR if directory else stat.S_ISREG
    if not wanted(info.st_mode):
        raise ValueError('confinement: unexpected path type: ' + str(path))
    if not directory and info.st_nlink != 1:
        raise ValueError('confinement: hard-linked/shared-inode file refused: ' + str(path))
    # Directory link counts change when unrelated sibling directories appear.
    # They are not directory identity; keep the alias check for FILES only.
    return info.st_dev, info.st_ino, info.st_mode, None if directory else info.st_nlink


def link_stamp(path):
    """Only for explicitly selected symlink leaves to UNLINK, never follow."""
    info = path.lstat()
    if not stat.S_ISLNK(info.st_mode):
        raise ValueError('confinement: expected unlink-only symlink: ' + str(path))
    return info.st_dev, info.st_ino, info.st_mode, os.readlink(path)


class Preflight:
    def __init__(self, directories=(), files=(), links=()):
        self.paths = {}
        for directory, targets in ((True, directories), (False, files)):
            for target in targets:
                path = lexical(target)
                for parent in reversed(path.parents):
                    self.paths[parent] = (True, stamp(parent, True))
                self.paths[path] = (directory, stamp(path, directory))
        self.links = {}
        for target in links:
            path = lexical(target)
            for parent in reversed(path.parents):
                self.paths[parent] = (True, stamp(parent, True))
            self.links[path] = link_stamp(path)

    def revalidate(self):
        for path, (directory, identity) in self.paths.items():
            if stamp(path, directory) != identity:
                raise ValueError('confinement: target/ancestor substituted or changed: ' + str(path))
        for path, identity in self.links.items():
            if link_stamp(path) != identity:
                raise ValueError('confinement: unlink-only symlink substituted or changed: ' + str(path))


def preflight(directories=(), files=(), links=()):
    return Preflight(directories, files, links)


def output_plan(roots=(), directories=(), files=(), links=()):
    """Validate ALL existing output descendants, never follow aliases.

    Explicit expected leaves additionally catch directories at file locations.
    The returned plan must be revalidated immediately before first mutation.
    Source/input symlinks are not output roots. Exclusive workspace required.
    """
    roots = tuple(lexical(p) for p in roots)
    links = tuple(lexical(p) for p in links)
    preflight(directories=(*roots, *directories), files=files, links=links).revalidate()
    ds, fs = list(directories) + list(roots), list(files)
    def unreadable(error):
        raise error  # os.walk otherwise silently skips unreadable descendants
    for root in roots:
        if not root.exists():
            continue
        for directory, children, leaves in os.walk(root, followlinks=False, onerror=unreadable):
            for name in children:
                path = Path(directory) / name
                if path in links:
                    continue
                stamp(path, True)  # includes directory symlinks, even dangling
                ds.append(path)
            for name in leaves:
                path = Path(directory) / name
                if path in links:
                    continue
                stamp(path, False)
                fs.append(path)
    return preflight(directories=ds, files=fs, links=links)


SDK_OUTPUT_FILES = ('bench_sdk_start.unchecked.json', 'sdkconfig', 'sdkconfig.old',
    'bench_sdk_inputs.json') + tuple('out/tools/esp32-arduino-libs/' + p for p in (
    'package.json', 'tools.json', 'versions.txt', 'bench_sdk_provenance.json',
    'esp32/lib/libmbedtls_2.a', 'esp32/lib/libesp_driver_sdm_2.a',
    'esp32/bin/bootloader_qio_80m.bin'))


def sdk_output_plan(builder):
    builder = lexical(builder)
    return output_plan(roots=(builder / 'build', builder / 'out'),
                       files=tuple(builder / p for p in SDK_OUTPUT_FILES))


def blob_output_plan(store):
    store = lexical(store)
    preflight(directories=(store,)).revalidate()
    # Content store is flat: a directory at a future digest is a wrong type,
    # not an output subtree that may be discovered only after earlier copies.
    return preflight(directories=(store,), files=tuple(store.iterdir()) if store.exists() else ())


def hook_cleanup(project, build, targets):
    project, build = lexical(project), lexical(build)
    if build == project or not build.is_relative_to(project):
        raise ValueError('confinement: hook BUILD_DIR must be beneath project')
    leaf = build / 'bench_image_report.json'
    # This leaf is unlink-only: never follow it, including dangling aliases.
    links = (leaf,) if leaf.is_symlink() else ()
    files = tuple(build / p for p in ('bootloader.bin', 'firmware_esptool.bin',
                  'bench_build_inputs.unchecked.json')) + (() if links else (leaf,))
    plan = output_plan(roots=(build,), directories=(project, *targets), files=files, links=links)
    remove_directories(plan, targets)
    leaf.unlink(missing_ok=True)


def remove_directories(plan, targets):
    targets = tuple(lexical(path) for path in targets)
    if any(path not in plan.paths or not plan.paths[path][0] for path in targets):
        raise ValueError('confinement: deletion target not in complete preflight')
    plan.revalidate()  # ALL targets/ancestors before the FIRST removal.
    for path in targets:
        if path.exists():
            shutil.rmtree(path)


def review_paths(destination):
    destination = lexical(destination)
    if destination.parent.name != 'bench-exports' or destination.parent.parent.name != '.pio':
        raise ValueError('review export requires isolated .pio/bench-exports destination')
    if destination.name not in ('ordinary', 'minimal'):
        raise ValueError('unknown review export variant')
    stage = destination.with_name(destination.name + '.unchecked')
    preflight(directories=(destination, stage))
    return destination, stage


def driver_paths(project):
    project = lexical(project)  # software directory; never resolve an alias.
    pio = project / '.pio'
    exports = tuple(pio / 'bench-exports' / name for name in EXPORTS)
    sdks = tuple(pio / name for name in SDKS)
    cleanup = tuple(sdk / name for sdk in sdks for name in ('build', 'out'))
    evidence = pio / 'bench-evidence/astra-fixes'
    logs = tuple(evidence / name for name in LOGS)
    # Include later PIO clean roots so a later variant cannot invalidate an
    # otherwise apparently safe first cleanup. SDK inputs beneath the roots
    # (e.g. esp-idf) may legitimately be links; cleanup never follows them.
    builds = tuple(pio / 'build' / name for name in
                   ('warp4_tesla_ble_iram_bench', 'warp4_tesla_ble_iram_bench_minimal'))
    # Inspect every retained SDK/output leaf BEFORE export cleanup or logs.
    # Build/output trees are removed later, but unsafe aliases reject now too.
    for sdk in sdks:
        sdk_output_plan(sdk).revalidate()
    blob_output_plan(pio / 'bench-input-blobs').revalidate()
    output_roots = (*cleanup, *builds, pio / 'bench-input-blobs')
    plan = output_plan(roots=output_roots,
        directories=(project, *exports, *sdks, evidence),
        files=(*logs, *(sdk / p for sdk in sdks for p in SDK_OUTPUT_FILES),
               *(build / p for build in builds for p in ('bootloader.bin',
                 'firmware_esptool.bin', 'bench_build_inputs.unchecked.json'))))
    return plan, exports, evidence


def initialize_driver(project):
    plan, exports, evidence = driver_paths(project)
    remove_directories(plan, exports)
    # No mkdir or log creation is permitted before the complete preflight.
    evidence.mkdir(parents=True, exist_ok=True)


def cleanup_sdk(project, sdk):
    project, sdk = lexical(project), lexical(sdk)
    expected = tuple(project / '.pio' / name for name in SDKS)
    if sdk not in expected:
        raise ValueError('confinement: substituted SDK root; exact project path required')
    targets = tuple(sdk / name for name in ('build', 'out'))
    plan = sdk_output_plan(sdk)
    remove_directories(plan, targets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('driver-init', 'driver-preflight', 'sdk-clean', 'outputs'))
    parser.add_argument('project', help='lexical software project directory')
    parser.add_argument('sdk', nargs='?')
    args = parser.parse_args()
    try:
        if args.action == 'driver-init':
            initialize_driver(args.project)
        elif args.action == 'driver-preflight':
            driver_paths(args.project)[0].revalidate()
        elif args.action == 'outputs':
            output_plan(roots=(args.project,) + ((args.sdk,) if args.sdk else ())).revalidate()
        elif args.sdk is None:
            raise ValueError('confinement: explicit SDK path required')
        else:
            cleanup_sdk(args.project, args.sdk)
    except (ValueError, OSError) as exc:
        print('bench confinement rejected: ' + str(exc), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
