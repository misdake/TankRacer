"""Check export provenance, required headers and immutable resource hashes."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    manifest = json.loads((ROOT/'source-manifest.json').read_text())
    for name, entry in manifest['unchanged_files'].items():
        assert digest(ROOT/name) == entry['sha256'], f'Changed exported file: {name}'
    for name, entry in manifest.get('extracted_files', {}).items():
        assert digest(ROOT/name) == entry['sha256'], f'Changed extracted host: {name}'
    modules = json.loads((ROOT/'modules.json').read_text())
    assert len(modules) == len(set(modules)) == 97
    for name in modules:
        for extension in ('.c','.h'):
            assert (ROOT/'src'/(name+extension)).is_file(), name+extension
    for directory in ('src','host'):
        for file in (ROOT/directory).iterdir():
            if file.suffix not in ('.c','.h','.inc'):
                continue
            for include in re.findall(r'^#include\s+"([^"]+)"',file.read_text(),re.M):
                candidates = [file.parent/include,ROOT/'src'/include,ROOT/'host'/include]
                assert any(p.is_file() and p.resolve().is_relative_to(ROOT) for p in candidates), (file,include)
    audio = json.loads((ROOT/'assets/audio.json').read_text())
    for entry in audio['sound_packs'].values():
        file = ROOT/'assets'/entry['path']
        assert file.stat().st_size == entry['bytes'] and digest(file) == entry['sha256'], file
    print(f'PASS: {len(modules)} modules, all local includes, {len(manifest["unchanged_files"])} unchanged files, 2 audio packs')

if __name__ == '__main__':
    main()
