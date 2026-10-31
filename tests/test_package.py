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
        'docs/ENGINEERING.md', 'docs/TESTING.md', 'docs/ATTRIBUTION.md')


class Package(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dsvr-package-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        for relative in ('src/loader/Release/dinput8.dll',
                         'src/core/Release/DSVR.dll',
                         'src/core/Release/DSVR-TraceWriter.exe',
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

    def test_attribution_ships_and_keeps_textures_out_of_scope(self):
        # Operator decision 29 Oct: texture replacement is a separate mod.
        # Attribution must ship, state the out-of-scope policy, and name no
        # texture mod (MODLOG 29 Oct; do not re-add texture guidance).
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.out / 'DSVR-test-docs-x64.zip') as archive:
            text = archive.read('DSVR-test-docs/docs/ATTRIBUTION.md').decode('utf-8')
        self.assertIn("out of DSVR's scope", text)
        self.assertNotIn('DSR 2020 Textures', text)
        self.assertNotIn('nexusmods.com', text)

    def test_game_and_texture_art_is_never_bundled(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            'dsvr_package', str(ROOT / 'tools' / 'package.py'))
        packager = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packager)
        for name in ('BHDTR2/maps/Map000.tpf', 'textures/a.dds', 'art.png',
                     'eventmsg/EventMsg.esd', 'obj/Pl.wep_param', 'm.hkx'):
            self.assertIsNotNone(packager.game_asset_violation(name), name)
        for name in ('DSVR.dll', 'DSVR-Setup.exe', 'docs/ATTRIBUTION.md',
                     'tools/vr_trace.py', 'licenses/OpenXR-LICENSE',
                     'licenses/ReShade-LICENSE.md'):
            self.assertIsNone(packager.game_asset_violation(name), name)
        self.assertTrue(packager.assert_no_game_assets(
            {Path('somewhere/in/build'): 'BHDTR2/maps/Map000.tpf'}))
        self.assertEqual(packager.assert_no_game_assets(
            {Path('build/DSVR.dll'): 'DSVR.dll'}), [])

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


    def test_release_command_uses_existing_tag_and_formal_assets(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "dsvr_publish_release", ROOT / "tools" / "publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        stage = self.out / "DSVR-test-docs"
        stage.mkdir(parents=True)
        (stage / "DSVR-Setup.exe").write_bytes(b"setup")
        (stage / "DSVR-SHA256.txt").write_text("manifest\n", encoding="utf-8")
        archive = self.out / "DSVR-test-docs-x64.zip"
        archive.write_bytes(b"zip")
        command = publisher.release_command(self.out, "test-docs")
        self.assertEqual(command[:4], ["gh", "release", "create", "vtest-docs"])
        self.assertIn("--verify-tag", command)
        self.assertIn(str(archive), command)
        self.assertIn(str(stage / "DSVR-Setup.exe"), command)
        self.assertIn(str(stage / "DSVR-SHA256.txt"), command)



    def test_release_requires_zero_failure_ctest_log(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "dsvr_publish_release", ROOT / "tools" / "publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        log = self.root / "ctest.log"
        log.write_bytes("100% tests passed out of 9\n".encode("utf-16"))
        self.assertTrue(publisher.ctest_passed(log))
        log.write_bytes("2 tests failed out of 9\n".encode("utf-16"))
        self.assertFalse(publisher.ctest_passed(log))
        log.write_text("incomplete output", encoding="utf-8")
        self.assertFalse(publisher.ctest_passed(log))

    def test_release_requires_clean_doctor_log(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "dsvr_publish_release", ROOT / "tools" / "publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        log = self.root / "doctor.log"
        log.write_text("PASS: pinned build supported\n[OK] Game not running\n[OK] DSVR installed\n", encoding="utf-8")
        self.assertTrue(publisher.doctor_passed(log))
        log.write_text("PASS: pinned build supported\n[FAIL] Game version\n", encoding="utf-8")
        self.assertFalse(publisher.doctor_passed(log))
    def test_release_requires_an_existing_remote_tag(self):
        import importlib.util
        from types import SimpleNamespace
        spec = importlib.util.spec_from_file_location(
            "dsvr_publish_release", ROOT / "tools" / "publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            return SimpleNamespace(returncode=0, stdout="abc123\trefs/tags/vtest-docs\n")
        self.assertTrue(publisher.remote_tag_exists("test-docs", runner=run))
        self.assertEqual(calls[0], ["git", "ls-remote", "--exit-code", "--tags",
                                    "origin", "refs/tags/vtest-docs"])
        missing = lambda command, **kwargs: SimpleNamespace(returncode=2, stdout="")
        self.assertFalse(publisher.remote_tag_exists("test-docs", runner=missing))
    def test_release_gate_counts_unchecked_lane_tasks(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "dsvr_publish_release", ROOT / "tools" / "publish_release.py")
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        lanes = self.root / "lanes"
        lanes.mkdir()
        for letter in "ABCDEFGHIJK":
            path = lanes / f"{letter}.md"
            if letter == "A":
                text = "- [ ] A1 open\n- [x] A2 done\n"
            elif letter == "B":
                text = "- [ ] B1 open\n"
            elif letter == "K":
                text = "- [ ] K5 publish\n- [ ] K6 report\n"
            else:
                text = "- [x] closed\n"
            path.write_text(text, encoding="utf-8")
        self.assertEqual(publisher.open_gate_count(lanes), 2)
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

    def test_latest_mid_refresh_failure_keeps_old_zip_and_verification(self):
        # A lock/kill mid-loop may leave the live stage mixed (new+old files),
        # but the zip + verification.json must stay on the old build: they are
        # the only shippable identity until a refresh fully succeeds.
        log = self.root / 'ctest.log'
        log.write_bytes('100% tests passed out of 7\n'.encode('utf-16'))
        self.assertEqual(self.run_latest('--ctest-log', str(log),
                                         '--doctor', 'pass').returncode, 0)
        old_zip = (self.out / 'DSVR-latest-x64.zip').read_bytes()
        old_verification = (self.out / 'DSVR-latest-verification.json') \
            .read_bytes()
        # Change a payload file so the refresh has something new to copy,
        # then lock one live destination so a mid-loop copy raises.
        (self.build / 'src/core/Release/DSVR.dll') \
            .write_bytes(b'test-only-placeholder-v2')
        locked = (self.out / 'DSVR-latest' / 'DSVR.dll')
        with open(locked, 'r+b') as guard:
            if hasattr(__import__('msvcrt'), 'locking'):
                import msvcrt
                msvcrt.locking(guard.fileno(), msvcrt.LK_NBLCK, 1)
            result = self.run_latest('--ctest-log', str(log),
                                     '--doctor', 'pass')
            self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.out / 'DSVR-latest-x64.zip').read_bytes(),
                         old_zip)
        self.assertEqual((self.out / 'DSVR-latest-verification.json')
                         .read_bytes(), old_verification)


if __name__ == '__main__':
    unittest.main()
