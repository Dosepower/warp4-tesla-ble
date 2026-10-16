#!/usr/bin/env python3
"""NON-INSTALLABLE bench review transaction; never production/latest/OTA."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import bench_image_report as report
from bench_confinement import review_paths


def export_review(build_dir, sdk, builder, boot_app0, destination):
    """Validate -> copy/hash -> validate exact stage -> atomic isolated export."""
    build_dir = Path(build_dir).resolve()
    destination, stage = review_paths(destination)
    if stage.exists() or stage.is_symlink() or destination.exists():
        raise ValueError('review export requires fresh revoked destination/stage')
    result = report.validate(build_dir, sdk, builder, boot_app0)
    if result['binding']['variant'] != destination.name:
        raise ValueError('review export variant label differs from validated identity')
    expected = {Path(p).name: value for p, value in result['artifacts'].items()
                if Path(p).parent == build_dir}
    for name, piece in zip(('bootloader.bin', 'partitions.bin', 'firmware.bin', 'boot_app0.bin'), result['pieces']):
        expected[name] = {key: piece[key] for key in ('bytes', 'sha256')}
    expected['firmware_esptool.bin'] = {key: result['image'][key] for key in ('bytes', 'sha256')}
    stage.mkdir(parents=True)
    try:
        for name, want in expected.items():
            source = Path(boot_app0) if name == 'boot_app0.bin' else build_dir / name
            shutil.copy2(source, stage / name)
            data = report.read(stage / name)
            if len(data) != want['bytes'] or hashlib.sha256(data).hexdigest() != want['sha256']:
                raise ValueError(f'validated bytes changed during staged copy: {name}')
        final = report.validate(stage, sdk, builder, stage / 'boot_app0.bin')
        if final['image']['sha256'] != result['image']['sha256']:
            raise ValueError('staged image/report identity changed')
        prefix = 'NON_INSTALLABLE-BENCH_REVIEW-' + destination.name + '-'
        renamed = {}
        for name in expected:
            new = prefix + name + '.review-bytes'
            (stage / name).rename(stage / new)
            renamed[str((stage / name).resolve())] = str((destination / new).resolve())
        final['image']['path'] = renamed[final['image']['path']]
        final['artifacts'] = {renamed.get(path, path): value for path, value in final['artifacts'].items()}
        final['export_files'] = {prefix + name + '.review-bytes': value for name, value in expected.items()}
        report_name = prefix + 'report.json'
        report_bytes = (json.dumps(final, indent=2) + '\n').encode()
        (stage / report_name).write_bytes(report_bytes)
        (stage / 'NON_INSTALLABLE-README.txt').write_text(
            'BENCH REVIEW ONLY. NOT INSTALLABLE. NOT FOR END USERS. NOT A RELEASE.\n'
            'No flash, OTA, boot, hardware, resource, crypto or licensing approval.\n'
            'B/C and two remaining A cold boots remain NO-GO.\n')
        for name, want in final['export_files'].items():
            data = report.read(stage / name)
            if len(data) != want['bytes'] or hashlib.sha256(data).hexdigest() != want['sha256']:
                raise ValueError('final staged bytes changed')
        if (stage / report_name).read_bytes() != report_bytes:
            raise ValueError('final staged report changed')
        os.rename(stage, destination)
        return final
    finally:
        if stage.exists():
            shutil.rmtree(stage)
