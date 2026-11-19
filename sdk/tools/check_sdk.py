"""Verify the vendored SDK without depending on an upstream .git directory."""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def check_sdk(repository, sdk, version_only=False):
    lock = json.loads((repository / 'sdk.lock.json').read_text(encoding='utf-8'))
    snapshot = json.loads((repository / 'esp-idf.snapshot.json').read_text(encoding='utf-8'))
    for key in ('repository', 'tag', 'commit'):
        if snapshot[key] != lock[key]:
            raise ValueError('SDK snapshot differs from sdk.lock.json: ' + key)
    if (sdk / 'version.txt').read_text(encoding='utf-8').strip() != lock['tag']:
        raise ValueError('SDK version differs from sdk.lock.json')
    if version_only:
        print('SDK version OK: ' + lock['tag'])
        return
    for name, expected in snapshot['sha256'].items():
        actual = hashlib.sha256((sdk / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError('SDK file differs from imported snapshot: ' + name)
    print('SDK snapshot OK: {} ({} files)'.format(lock['tag'], len(snapshot['sha256'])))


if __name__ == '__main__':
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', nargs='?', type=Path, default=repository / 'sdk/esp-idf')
    parser.add_argument('--version-only', action='store_true', help='Check pinned metadata and version without hashing source files')
    args = parser.parse_args()
    try:
        check_sdk(repository, args.sdk, args.version_only)
    except (OSError, ValueError, KeyError) as error:
        sys.exit('SDK verification failed: ' + str(error))
