#!/usr/bin/env python3
"""Synchronize native inputs without preserving stale archive timestamps.

This is a source snapshot, not proof of binary contents. Build successfully
afterward and bind the EXE/map hashes separately. Generated guest code and the
pinned SDK are deliberately outside this helper's scope.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def host_profile_inputs(cmake_cache):
    """Record generated adapters actually selected by CMake; never overwrite them."""
    if cmake_cache is None:
        return {}
    cache = Path(cmake_cache).resolve()
    matches = re.findall(r'^PORT_HOST_PROFILE_DIR:[^=]+=(.*)$', cache.read_text(), re.M)
    if len(matches) != 1:
        raise ValueError('Need one PORT_HOST_PROFILE_DIR in CMake cache')
    if not matches[0]:
        return {}
    profile = Path(matches[0])
    if not profile.is_absolute():
        raise ValueError('Host profile directory must be absolute')
    return {str((profile/(name+'.cpp')).resolve()):
            hashlib.sha256((profile/(name+'.cpp')).read_bytes()).hexdigest()
            for name in ('stubs', 'boot_trace', 'frontend_policy', 'video_queue', 'edge_decompress')}


def sync_native(source, workspace, cmake_cache=None):
    source, workspace = Path(source).resolve(), Path(workspace).resolve()
    if source == workspace:
        raise ValueError('Source and prepared workspace must be different')
    # Validate before mutating native inputs. These sources live outside port/
    # and deliberately contain different guest addresses for the selected ELF.
    generated_host = host_profile_inputs(cmake_cache)
    changed, bindings = [], {}
    for directory, suffixes in [('port', {'.h', '.c', '.cpp', '.inc', '.txt'}),
                                ('cmake', {'.cmake', '.h', '.patch', '.exports'})]:
        for path in sorted((source/directory).iterdir()):
            if not path.is_file() or path.suffix not in suffixes:
                continue
            name = path.relative_to(source).as_posix()
            target = (workspace/name).resolve()
            if not target.is_relative_to(workspace):
                raise ValueError('Prepared input escapes workspace: '+name)
            data = path.read_bytes()
            if not target.exists() or target.read_bytes() != data:
                target.parent.mkdir(parents=True, exist_ok=True)
                # write_bytes sets a new modification time. copy2/Copy-Item
                # can restore a time older than the already compiled object.
                target.write_bytes(data)
                changed.append(name)
            assert target.read_bytes() == data, name
            bindings[name] = hashlib.sha256(data).hexdigest()
    return {'scope': 'prepared native source snapshot before build',
            'source': str(source), 'workspace': str(workspace),
            'changed': changed, 'sources': bindings,
            'generated_host_sources': generated_host,
            'cmake_cache': str(Path(cmake_cache).resolve()) if cmake_cache else None}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--workspace', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--cmake-cache', type=Path,
                   help='Also hash the generated host adapters selected by this existing build cache')
    a = p.parse_args()
    result = sync_native(a.source, a.workspace, a.cmake_cache)
    a.manifest.write_text(json.dumps(result, indent=2)+'\n')
    print(f"Bound {len(result['sources'])} native inputs; updated {len(result['changed'])} with fresh timestamps")


if __name__ == '__main__':
    main()
