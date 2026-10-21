"""Exercise the real packager without compiling, installing or launching DSR."""
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ('README.md', 'MODDING_PLAN.md', 'MODLOG.md',
        'docs/ENGINEERING.md', 'docs/TESTING.md')


class Package(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dsvr-package-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        for relative in ('src/loader/Release/dinput8.dll',
                         'src/core/Release/DSVR.dll',
                         'src/setup/Release/DSVR-Setup.exe',
                         '_deps/minhook-src/LICENSE.txt',
                         '_deps/imgui-src/LICENSE.txt',
                         '_deps/openxr-src/LICENSE',
                         '_deps/openxr-src/src/external/jsoncpp/LICENSE'):
            path = self.build / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'test-only-placeholder')
        self.out = self.root / 'out'

    def run_package(self, *extra):
        return subprocess.run(
            [sys.executable, '-B', str(ROOT / 'tools/package.py'),
             '--build', str(self.build), '--out', str(self.out),
             '--version', 'test-docs', *extra],
            capture_output=True, text=True, timeout=15)

    def test_consolidated_docs_and_manifest(self):
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.out / 'DSVR-test-docs-x64.zip') as archive:
            prefix = 'DSVR-test-docs/'
            files = {name.removeprefix(prefix): archive.read(name)
                     for name in archive.namelist() if not name.endswith('/')}
        self.assertEqual({name for name in files if name.endswith('.md')},
                         set(DOCS) | {'THIRD_PARTY.md'})
        for name in DOCS:
            self.assertEqual(files[name], (ROOT / name).read_bytes())
        manifest = files.pop('DSVR-SHA256.txt').decode('utf-8').splitlines()
        expected = {name: hashlib.sha256(data).hexdigest()
                    for name, data in files.items()}
        self.assertEqual({line.split('  ', 1)[1]: line.split('  ', 1)[0]
                          for line in manifest}, expected)

    def test_existing_release_is_never_replaced(self):
        self.out.mkdir()
        for name in ('DSVR-test-docs', 'DSVR-test-docs-x64.zip'):
            with self.subTest(name=name):
                target = self.out / name
                if name.endswith('.zip'):
                    target.write_bytes(b'preserve archive')
                else:
                    target.mkdir()
                    (target / 'user-work').write_bytes(b'preserve stage')
                result = self.run_package()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual((target if target.is_file() else
                                  target / 'user-work').read_bytes(),
                                 b'preserve archive' if target.is_file() else
                                 b'preserve stage')
                if target.is_file():
                    target.unlink()
                else:
                    (target / 'user-work').unlink()
                    target.rmdir()

    def test_missing_addon_is_refused_without_output(self):
        result = self.run_package('--reshade-addon')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.out.exists())

    def test_optional_addon_includes_notice(self):
        for relative in ('src/reshade/Release/DSVR-ReShade.addon64',
                         '_deps/reshade_sdk-src/LICENSE.md'):
            path = self.build / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'test-only-addon')
        result = self.run_package('--reshade-addon')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.out / 'DSVR-test-docs-x64.zip') as archive:
            for name in ('DSVR-ReShade.addon64', 'licenses/ReShade-LICENSE.md'):
                self.assertEqual(archive.read('DSVR-test-docs/' + name),
                                 b'test-only-addon')

    def test_unsafe_version_is_refused_without_output(self):
        result = self.run_package('--version', '../escape')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.out.exists())

    def run_latest(self, *extra):
        return subprocess.run(
            [sys.executable, '-B', str(ROOT / 'tools/package.py'),
             '--build', str(self.build), '--out', str(self.out),
             '--version', 'test-docs', '--latest', *extra],
            capture_output=True, text=True, timeout=15)

    def test_latest_refreshes_and_verifies(self):
        log = self.root / 'ctest.log'
        # Real zero-failure CTest output has no "tests failed" clause, and
        # PowerShell capture (Tee-Object) stores it as UTF-16.
        log.write_bytes('100% tests passed out of 7\n'.encode('utf-16'))
        result = self.run_latest('--ctest-log', str(log), '--doctor', 'pass')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        stage = self.out / 'DSVR-latest'
        self.assertTrue((stage / 'DSVR.dll').is_file())
        self.assertIn('latest (dev test package',
                      (stage / 'DSVR-VERSION.txt').read_text(encoding='utf-8'))
        with zipfile.ZipFile(self.out / 'DSVR-latest-x64.zip') as archive:
            self.assertIn('DSVR-latest/DSVR.dll', archive.namelist())
        info = json.loads((self.out / 'DSVR-latest-verification.json')
                          .read_text(encoding='utf-8'))
        self.assertEqual(info['ctest'], {'passed': 7, 'failed': 0, 'total': 7})
        self.assertEqual(info['doctor'], 'pass')
        self.assertEqual(info['binaries_sha256']['DSVR.dll'],
                         hashlib.sha256(b'test-only-placeholder').hexdigest())
        self.assertEqual((self.out / 'DSVR-latest-ctest.txt')
                         .read_text(encoding='utf-8'),
                         '100% tests passed out of 7\n')  # shipped as UTF-8
        # A second refresh succeeds and leaves no temp/backup debris.
        self.assertEqual(self.run_latest().returncode, 0)
        self.assertFalse((self.out / '.DSVR-latest.tmp').exists())
        self.assertFalse((self.out / '.DSVR-latest.bak').exists())

    def test_formal_latest_version_is_refused(self):
        result = self.run_package('--version', 'latest')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.out / 'DSVR-latest').exists())

    def test_latest_failure_keeps_previous_package(self):
        self.assertEqual(self.run_latest().returncode, 0)
        before = (self.out / 'DSVR-latest' / 'DSVR.dll').read_bytes()
        (self.build / 'src/core/Release/DSVR.dll').unlink()
        result = self.run_latest()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.out / 'DSVR-latest' / 'DSVR.dll').read_bytes(),
                         before)
        self.assertTrue((self.out / 'DSVR-latest-x64.zip').is_file())


if __name__ == '__main__':
    unittest.main()
