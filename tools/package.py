"""Package both firmware builds and reviewed source; never upload a release."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOP = ['application.fam', 'build-lock.json', 'build-lock-official.json', 'sources-lock.json',
       'requirements-dev.txt', 'requirements-host.txt', 'LICENSE', 'NOTICE.md', 'readme.md',
       'readme-zh-cn.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'CONTRIBUTING-zh-cn.md',
       '.gitignore', '.gitattributes']
DOCS = ['PROTOCOL.md', 'RESEARCH.md', 'VALIDATION.md', 'HARDWARE-TEST.md',
        'RELEASE-1.0.0.md', 'RELEASE-1.0.0-zh-cn.md']


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_files():
    names = TOP + ['docs/' + n for n in DOCS] + ['.github/workflows/build.yml',
             'evidence/release-validation.json']
    for folder, allowed in [('src', {'.c', '.h'}), ('tests', {'.c', '.h', '.py'}), ('tools', {'.py'})]:
        names += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / folder).rglob('*'))
                  if p.is_file() and p.suffix in allowed and '__pycache__' not in p.parts]
    return {name: (ROOT / name).read_bytes() for name in sorted(names)}


def check_public(files):
    forbidden = [rb'(?i)[a-z]:[/\\]Users[/\\]', rb'gh[pousr]_[A-Za-z0-9]{20,}',
                 rb'github_pat_[A-Za-z0-9_]{20,}', rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----']
    for name, data in files.items():
        assert Path(name).suffix.lower() not in {'.nfc', '.shd', '.dll', '.exe', '.pfx', '.pem'}, name
        for pattern in forbidden:
            assert not re.search(pattern, data), f'Private material/path detected in {name}'
    for name in ['docs/PROTOCOL.md', 'docs/RESEARCH.md']:
        assert files[name] == b'deleted\n', f'Internal analysis must not be packaged: {name}'


def add_test_reports(files):
    tests = json.loads((ROOT / 'build/test-results.json').read_text())
    assert len(tests['results']) == 7 and all(x['exit_code'] == 0 for x in tests['results'])
    assert tests['source_sha256'], 'Missing test source hashes'
    for name, sha in tests['source_sha256'].items():
        assert digest(files[name]) == sha, f'Changed since tests: {name}'
    client = json.loads((ROOT / 'build/public-client-test.json').read_text())
    assert client['exit_code'] == 0 and 'PUBLIC DART CLIENT PASS' in client['stdout']
    assert client['source_sha256'] == json.loads(files['sources-lock.json'])['public_client']['sha256']
    assert client['core_sha256'] == digest((ROOT / 'build/test_hinata.exe').read_bytes())
    files['evidence/test-results.json'] = (ROOT / 'build/test-results.json').read_bytes()
    files['evidence/public-client-test.json'] = (ROOT / 'build/public-client-test.json').read_bytes()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--without-tests', action='store_true',
                        help='Package builds without runtime test reports; record this in the manifest')
    args = parser.parse_args()
    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit('Refusing to overwrite a non-empty release directory')
    files = source_files()
    version = json.loads(files['build-lock.json'])['version']
    assert re.search(r'fap_version\s*=\s*\(' + r'\s*,\s*'.join(version.split('.')) + r'\)',
                     files['application.fam'].decode())
    assert f'HINATA USB {version}' in files['src/hinata_usb.c'].decode()
    builds = {}
    downloads = {}
    for flavor, lock_name in [('official', 'build-lock-official.json'), ('unleashed', 'build-lock.json')]:
        lock = json.loads(files[lock_name])
        build_data = (ROOT / 'dist' / flavor / 'build-manifest.json').read_bytes()
        build = json.loads(build_data)
        artifact = (ROOT / 'dist' / flavor / 'hinata_usb.fap').read_bytes()
        assert lock['version'] == build['app_version'] == version
        assert build['firmware_flavor'] == flavor
        assert build['sdk'] == lock['firmware'] and build['api'] == lock['api']
        assert build['target'] == lock['hardware']
        assert build['sha256'] == digest(artifact) and build['size'] == len(artifact)
        assert build['source_sha256'], 'Missing build source provenance'
        for name, sha in build['source_sha256'].items():
            assert digest(files[name]) == sha, f'Changed since {flavor} build: {name}'
        name = build['release_filename']
        assert Path(name).name == name and name.endswith('.fap') and name not in downloads
        downloads[name] = artifact
        files[f'apps/{flavor}/USB/hinata_usb.fap'] = artifact
        files[f'evidence/build-{flavor}.json'] = build_data
        builds[flavor] = build
    if not args.without_tests:
        add_test_reports(files)
    check_public(files)
    manifest = {name: dict(size=len(data), sha256=digest(data)) for name, data in sorted(files.items())}
    files['MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'hinata-usb-{version}.zip'
    prefix = f'hinata-usb-{version}/'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(prefix + name, (2026, 10, 19, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert all(z.read(prefix + name) == data for name, data in files.items())
    for name, data in downloads.items():
        (out / name).write_bytes(data)
    # Standalone release notes use stable repository links, including the other language.
    for source, name in [('docs/RELEASE-1.0.0.md', 'RELEASE-NOTES.md'),
                         ('docs/RELEASE-1.0.0-zh-cn.md', 'RELEASE-NOTES-zh-cn.md')]:
        notes = files[source].decode()
        def link(match):
            label, target = match.groups()
            relative = target.removeprefix('../') if target.startswith('../') else 'docs/' + target
            return f'[{label}](https://github.com/yamiYori/flipper-hinata/blob/main/{relative})'
        notes = re.sub(r'\[([^]]+)\]\((?!https?://)([^)]+)\)', link, notes)
        (out / name).write_text(notes, encoding='utf-8', newline='\n')
    release = dict(version=version, builds=builds,
                   runtime_tests='not_run_for_this_package' if args.without_tests else 'passed',
                   archive=dict(filename=archive.name, size=archive.stat().st_size,
                                sha256=digest(archive.read_bytes())), file_count=len(files))
    (out / 'release-manifest.json').write_text(json.dumps(release, indent=2) + '\n', encoding='utf-8')
    sums = ''.join(f'{digest(p.read_bytes())}  {p.name}\n' for p in sorted(out.iterdir()) if p.is_file())
    (out / 'SHA256SUMS').write_text(sums, encoding='utf-8', newline='\n')
    print(json.dumps({'output': str(out), 'version': version, 'builds': list(builds),
                      'runtime_tests': release['runtime_tests'], 'archive': release['archive']}, indent=2))


if __name__ == '__main__':
    main()
