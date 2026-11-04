import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPORT = Path(__file__).resolve().parents[1] / 'tools/hmd/report.py'
PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aU1sAAAAASUVORK5CYII=')


class ReportTests(unittest.TestCase):
    def run_report(self, mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            runtime = root / 'openxr_simulator.dll'
            manifest = root / 'openxr_simulator.json'
            runtime.write_bytes(b'pinned simulator runtime fixture')
            manifest.write_text('{"runtime":"OpenXR Simulator"}')
            receipt = {'state': 'complete', 'host_exit': 0, 'cleanup_ok': True,
                       'runtime_cleanup_ok': True, 'runtime_registration_unchanged': True,
                       'runtime_name': 'OpenXR Simulator', 'runtime_version': 'v1.7.1+DSVR-controller-state',
                       'process_override_used': True, 'runtime_module_loaded': True,
                       'layer_module_loaded': True, 'process_forced': False,
                       'process_peak_private_mb': 350, 'memory_cap_mb': 4096,
                       'process_memory_samples': 2, 'runtime_data_bytes': 1024,
                       'mcp_process_exit': 0, 'mcp_process_forced': False,
                       'mcp_memory_peak_mb': 200, 'mcp_memory_samples': 1,
                       'runtime_dll': str(runtime), 'runtime_dll_sha256': hashlib.sha256(runtime.read_bytes()).hexdigest(),
                       'runtime_manifest': str(manifest), 'runtime_manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
                       'stages': [],
                       'scope': 'production XR modules in standalone host'}
            (root / 'memory.csv').write_text('time_utc,role,pid,private_mb,working_set_mb\n2026-11-03T00:00:00Z,host,10,300,250\n2026-11-03T00:00:01Z,host,10,350,260\n2026-11-03T00:00:01Z,mcp_driver,11,200,180\n')
            host = {'ok': True, 'cleanup': True, 'theater_submits': 110,
                    'mono_submits': 110, 'native_submits': 110, 'input_samples': 330,
                    'submitted_frames': 330,
                    'trigger_left_samples': 20, 'trigger_right_samples': 60,
                    'squeeze_left_samples': 60, 'squeeze_right_samples': 30,
                    'primary_left_samples': 10, 'primary_right_samples': 10,
                    'secondary_left_samples': 10, 'secondary_right_samples': 10,
                    'menu_left_samples': 10, 'menu_right_samples': 10,
                    'stick_click_left_samples': 10, 'stick_click_right_samples': 10,
                    'stick_forward_samples': 30, 'stick_reverse_samples': 20,
                    'grip_pose_samples': 300, 'aim_pose_samples': 300,
                    'aim_offset_samples': 300, 'hand_motion_samples': 20,
                    'hand_rotation_samples': 20, 'active_neutral_samples': 50,
                    'inactive_action_samples': 30, 'not_focused_samples': 0,
                    'input_sync_failures': 0,
                    'hands_lost': 30, 'hands_recovered': 60,
                    'max_head_x': .12, 'max_head_yaw_component': .29}
            required_tools = ['get_diagnostics', 'get_session_state', 'get_head_tracking', 'set_head_pose',
                              'set_headset_profile', 'set_fov', 'set_ipd', 'enable_pose_sweep',
                              'set_session_focus', 'set_controller_state', 'capture_screenshot', 'hover_ui_at',
                              'click_ui_at', 'hold_ui_at', 'scroll_ui_at', 'release_ui_pointer',
                              'get_frame_info', 'get_projection_log', 'validate_stereo', 'get_flicker_status',
                              'get_ui_flicker_status', 'read_logs', 'perf_sessions', 'perf_live_summary',
                              'perf_start_capture', 'perf_stop_capture', 'perf_add_marker',
                              'perf_list_captures', 'perf_analyze_capture', 'perf_compare_captures']
            controller_inputs = []
            for hand in ('left', 'right'):
                controller_inputs.extend([
                    {'hand': hand, 'state': {'position': {'x': .2, 'y': -.2, 'z': -.3}}},
                    {'hand': hand, 'state': {'trigger': .8}}, {'hand': hand, 'state': {'grip': .7}},
                    {'hand': hand, 'state': {'primary': True}}, {'hand': hand, 'state': {'secondary': True}},
                    {'hand': hand, 'state': {'menu': True}}, {'hand': hand, 'state': {'thumbstick_click': True}},
                    {'hand': hand, 'state': {'thumbstick': [.8, 0.0]}},
                    {'hand': hand, 'state': {'tracked': False}}, {'hand': hand, 'state': {'tracked': True}},
                ])
            mcp_captures = []
            for index in range(4):
                filename = f'mcp-{index}.png'
                (root / filename).write_bytes(PNG)
                mcp_captures.append({'file': filename, 'mime_type': 'image/png',
                                     'sha256': hashlib.sha256(PNG).hexdigest()})
            mcp = {'verdict': 'PASS', 'cleanup_ok': True, 'failures': [],
                   'tool_catalog': required_tools, 'tool_calls': [{'tool': name} for name in required_tools],
                   'controller_inputs': controller_inputs,
                   'profile_checks': [{'profile': p} for p in ('quest2', 'index', 'quest3')],
                   'captures': mcp_captures,
                   'cleanup': [{'operation': name, 'ok': True} for name in (
                       'ui_release_left', 'ui_release_right', 'release_left', 'release_right',
                       'focus', 'pose_sweep', 'head_pose', 'fov', 'ipd', 'profile')]}
            events = [{'event': 'started', 'synthetic_inputs': False,
                       'runtime_passthrough': True, 'scenario': 'game-smoke-simulator-v1'},
                      {'event': 'submit', 'result': 0, 'layers': 1},
                      {'event': 'finished', 'capture_failures': 0}]
            for frame in (30, 100, 200, 300):
                for chain in (1, 2):
                    filename = f'eye-{frame}-{chain}.png'
                    (root / filename).write_bytes(PNG)
                    events.append({'event': 'capture', 'chain': chain, 'frame': frame,
                                   'ok': True, 'file': filename, 'width': 1, 'height': 1,
                                   'lit_fraction': 1.0, 'mean_rgb': [30, 30, 180] if chain == 2 else [180, 30, 30]})
            if mutation:
                mutation(root, receipt, host, events)
            (root / 'receipt.json').write_text(json.dumps(receipt))
            (root / 'host.json').write_text(json.dumps(host))
            (root / 'mcp-run.json').write_text(json.dumps(mcp))
            (root / 'layer.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
            process = subprocess.run([sys.executable, str(REPORT), str(root)], capture_output=True, text=True)
            result = json.loads((root / 'report.json').read_text())
            self.assertTrue((root / 'report.html').is_file())
            return process.returncode, result

    def test_complete_run(self):
        code, result = self.run_report()
        self.assertEqual(code, 0)
        self.assertEqual(result['verdict'], 'PASS')
        self.assertEqual(result['visual_review'], 'PENDING')

    def test_missing_eye_fails(self):
        code, result = self.run_report(lambda root, *_: (root / 'eye-30-2.png').unlink())
        self.assertNotEqual(code, 0)
        self.assertEqual(result['verdict'], 'FAIL')

    def test_cleanup_conflict_fails(self):
        code, _ = self.run_report(lambda root, receipt, *_: receipt.update(cleanup_ok=False))
        self.assertNotEqual(code, 0)

    def test_runtime_failure_fails(self):
        code, _ = self.run_report(lambda root, receipt, host, events:
                                 events.append({'event': 'submit', 'result': -2, 'layers': 1}))
        self.assertNotEqual(code, 0)

    def test_timeout_cannot_pass_from_partial_host_result(self):
        code, _ = self.run_report(lambda root, receipt, *_: receipt.update(host_exit=None, state='failed'))
        self.assertNotEqual(code, 0)

    def test_capture_path_cannot_escape_run(self):
        code, _ = self.run_report(lambda root, receipt, host, events:
                                 events[-1].update(file='../outside.png'))
        self.assertNotEqual(code, 0)

    def test_missing_layer_finish_fails(self):
        code, _ = self.run_report(lambda root, receipt, host, events:
                                 events.pop(2))
        self.assertNotEqual(code, 0)

    def test_missing_motion_observation_fails(self):
        code, _ = self.run_report(lambda root, receipt, host, events: host.update(max_head_x=0))
        self.assertNotEqual(code, 0)

    def test_missing_controller_action_fails(self):
        code, result = self.run_report(lambda root, receipt, host, events:
                                       host.update(secondary_left_samples=0))
        self.assertNotEqual(code, 0)
        self.assertTrue(any('both-hand action coverage' in failure for failure in result['failures']))

    def test_action_sync_failure_fails(self):
        code, result = self.run_report(lambda root, receipt, host, events:
                                       host.update(input_sync_failures=1))
        self.assertNotEqual(code, 0)
        self.assertTrue(any('both-hand action coverage' in failure for failure in result['failures']))


if __name__ == '__main__':
    unittest.main()
