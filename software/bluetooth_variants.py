"""Compile-time Bluetooth feature boundaries used by stock web generation.

The legacy frontend module/translation namespace stays stable for compatibility;
its feature-specific sources and translations are excluded before bundling.
"""
from pathlib import PurePosixPath


def include_frontend(relative, tesla, remote):
    path = PurePosixPath(relative)
    if path.parts[:2] != ('modules', 'tesla_vehicle'):
        return True
    if path.name == 'remote_settings.tsx':
        return remote
    if path.name in ('guided_vehicle.tsx', 'action_policy.ts', 'soc_time.ts', 'pairing_outcome.ts'):
        return tesla
    return True


def prune_translation(translation, tesla, remote):
    section = translation.get('tesla_vehicle')
    if not section:
        return
    if not remote:
        section.pop('remote', None)
    if not tesla:
        section['content'] = {'cancel': section['content']['cancel']}
        for key in ('status', 'script', 'pair_response'):
            section.pop(key, None)
        for key in ('tesla_title', 'active_hint'):
            section['bluetooth'].pop(key, None)
