"""Run with python3 tools/test_sdk_snapshot.py; no third-party packages needed."""
import hashlib
import json
from pathlib import Path
import tempfile

from check_sdk import check_sdk


def main():
    build = Path(__file__).resolve().parent.parent / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as directory:
        repository = Path(directory)
        sdk = repository / 'sdk/esp-idf'
        sdk.mkdir(parents=True)
        lock = dict(repository='upstream', tag='v5.5.6', commit='pinned')
        (repository / 'sdk.lock.json').write_text(json.dumps(lock))
        contents = {'version.txt': b'v5.5.6\n', 'controller.a': b'required dependency'}
        for name, data in contents.items():
            (sdk / name).write_bytes(data)
        snapshot = dict(lock, sha256={name: hashlib.sha256(data).hexdigest()
                                      for name, data in contents.items()})
        manifest = repository / 'sdk/esp-idf.snapshot.json'
        manifest.write_text(json.dumps(snapshot))
        check_sdk(repository, sdk)
        check_sdk(repository, sdk, version_only=True)

        def rejected():
            try:
                check_sdk(repository, sdk)
            except (ValueError, OSError):
                return
            raise AssertionError('Invalid SDK snapshot was accepted')

        (sdk / 'controller.a').write_bytes(b'changed dependency')
        rejected()
        (sdk / 'controller.a').unlink()
        rejected()
        (sdk / 'controller.a').write_bytes(contents['controller.a'])
        (sdk / 'version.txt').write_text('v5.4.0\n')
        rejected()
        try:
            check_sdk(repository, sdk, version_only=True)
        except ValueError:
            pass
        else:
            raise AssertionError('Version check accepted a different SDK version')
        (sdk / 'version.txt').write_bytes(contents['version.txt'])
        snapshot['commit'] = 'different'
        manifest.write_text(json.dumps(snapshot))
        rejected()
    print('PASS: valid snapshot; changed/missing dependency, version and commit rejection')


if __name__ == '__main__':
    main()
