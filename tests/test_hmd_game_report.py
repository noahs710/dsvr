import base64
import hashlib
import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPORT = Path(__file__).resolve().parents[1] / 'tools/hmd/game_report.py'
PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aU1sAAAAASUVORK5CYII=')
TOOLS = [
    'get_diagnostics', 'get_session_state', 'get_head_tracking', 'set_head_pose',
    'set_headset_profile', 'set_fov', 'set_ipd', 'enable_pose_sweep', 'set_session_focus',
    'set_controller_state', 'capture_screenshot', 'hover_ui_at', 'click_ui_at', 'hold_ui_at',
    'scroll_ui_at', 'release_ui_pointer', 'get_frame_info',
    'get_projection_log', 'validate_stereo', 'get_flicker_status', 'get_ui_flicker_status', 'read_logs',
    'perf_sessions', 'perf_live_summary', 'perf_start_capture', 'perf_stop_capture',
    'perf_add_marker', 'perf_list_captures', 'perf_analyze_capture', 'perf_compare_captures',
]


class GameReportTests(unittest.TestCase):
    def run_report(self, mutate=None, visual_review=True):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            layer_dir = root / 'layer'
            layer_dir.mkdir()
            layer_dll = layer_dir / 'DSVR-HmdLayer.dll'
            layer_manifest = layer_dir / 'DSVR_hmd_lab.json'
            runtime_dll = root / 'openxr_simulator.dll'
            runtime_manifest = root / 'openxr_simulator.json'
            layer_dll.write_bytes(b'fixture layer')
            layer_manifest.write_text('{}')
            runtime_dll.write_bytes(b'fixture OpenXR-Simulator runtime')
            runtime_manifest.write_text('{"runtime":"OpenXR Simulator"}')
            actions = []
            for name in ('trigger', 'squeeze', 'thumbstick', 'primary', 'secondary', 'menu', 'stick_click'):
                for hand in ('left', 'right'):
                    actions.append({'name': name, 'hand': hand, 'calls': 10, 'active': 10,
                                    'nonzero': 2 if name in ('trigger', 'squeeze') else 0})
            poses = [{'name': name, 'hand': hand, 'samples': 10, 'max_displacement': 0.14}
                     for name in ('grip_pose', 'aim_pose') for hand in ('left', 'right')]
            summary = {'actions': actions, 'poses': poses,
                       'head': {'samples': 20, 'max_x': 0.12, 'max_yaw_component': 0.29552}}
            simulator = {
                'runtime_name': 'OpenXR Simulator',
                'runtime_version': 'v1.7.1+DSVR-controller-state',
                'process_override_used': True,
                'runtime_module_loaded': True,
                'layer_module_loaded': True,
                'runtime_dll': str(runtime_dll),
                'runtime_dll_sha256': hashlib.sha256(runtime_dll.read_bytes()).hexdigest(),
                'runtime_manifest': str(runtime_manifest),
                'runtime_manifest_sha256': hashlib.sha256(runtime_manifest.read_bytes()).hexdigest(),
                'source_commit': '3aafc530beaee24cde490dfad06fc2ff07514458',
                'controller_patch_sha256': 'ac3dd6545bd75036faf4aacd2c2e07b878f8918b8cc2003d1f88396e39c16401',
                'official_release_sha256': '8bc7039f9762cfebbec583884c1cec4abb3095733323230eb9ba741bc86f976a',
            }
            start = datetime(2026, 11, 3, tzinfo=timezone.utc)
            memory_rows = ['time_utc,role,pid,private_mb,working_set_mb']
            for second in range(601):
                stamp = (start + timedelta(seconds=second)).isoformat().replace('+00:00', 'Z')
                memory_rows.append(f'{stamp},game,123,500,400')
            memory_rows.append(f'{(start + timedelta(seconds=1)).isoformat()},mcp_driver,124,100,80')
            (root / 'memory.csv').write_text('\n'.join(memory_rows) + '\n')
            receipt = {
                'state': 'Restored', 'input_mode': 'OpenXRSimulator',
                'synthetic_layer': {'scenario': 'game-smoke-simulator-v1', 'directory': str(layer_dir),
                                    'dll_sha256': hashlib.sha256(layer_dll.read_bytes()).hexdigest(),
                                    'manifest_sha256': hashlib.sha256(layer_manifest.read_bytes()).hexdigest()},
                'simulator': simulator, 'simulator_control_exit': 0, 'simulator_control_forced': False,
                'runtime_registration_unchanged': True, 'memory_samples': 602,
                'files': [{}, {}, {}], 'roots': [{}, {}], 'pids': [{'id': 123}], 'unclosed_traces': [],
            }
            cases = {'cases': [{'label': 'simulator_hmd_1', 'seconds': 600, 'state': 'declared',
                                'start_qpc': 200, 'end_qpc': 900}]}
            controls = []
            for hand in ('left', 'right'):
                controls.extend([
                    {'hand': hand, 'state': {'position': {'x': .2, 'y': -.2, 'z': -.3}}},
                    {'hand': hand, 'state': {'trigger': .8}}, {'hand': hand, 'state': {'grip': .75}},
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
            mcp = {
                'verdict': 'PASS', 'cleanup_ok': True, 'failures': [], 'cleanup': [
                    {'operation': name, 'ok': True} for name in (
                        'ui_release_left', 'ui_release_right', 'release_left', 'release_right',
                        'focus', 'pose_sweep', 'head_pose', 'fov', 'ipd', 'profile')],
                'tool_catalog': TOOLS, 'tool_calls': [{'tool': tool} for tool in TOOLS],
                'controller_inputs': controls, 'captures': mcp_captures,
                'soak_requested_seconds': 600, 'soak_started_utc': start.isoformat().replace('+00:00', 'Z'),
                'soak_finished_utc': (start + timedelta(seconds=600)).isoformat().replace('+00:00', 'Z'),
                'soak_elapsed_seconds': 600, 'soak_samples': [{'frame': i} for i in range(600)],
            }
            events = [
                {'event': 'started', 'synthetic_inputs': False, 'runtime_passthrough': True,
                 'scenario': 'game-smoke-simulator-v1'},
                {'event': 'session', 'result': 0},
                {'event': 'submit', 'result': 0, 'layers': 1},
                {'event': 'finished', 'capture_failures': 0, 'input_summary': summary},
            ]
            captures = []
            for frame in (1000, 1030, 1060, 1100):
                for chain in (1, 2):
                    filename = f'eye-{frame}-{chain}.png'
                    (root / filename).write_bytes(PNG)
                    captures.append({'event': 'capture', 'file': filename, 'frame': frame,
                                     'chain': chain, 'ok': True, 'width': 1, 'height': 1,
                                     'lit_fraction': 0.5})
            events.extend(captures)
            if mutate:
                mutate(receipt, cases, events, summary, mcp, root)
            (root / 'receipt.json').write_text(json.dumps(receipt))
            (root / 'combat-cases.json').write_text(json.dumps(cases))
            (root / 'mcp-run.json').write_text(json.dumps(mcp))
            (root / 'layer.jsonl').write_text(''.join(json.dumps(event) + '\n' for event in events))
            if visual_review is not False:
                reviewed = [hashlib.sha256((root / event['file']).read_bytes()).hexdigest() for event in captures]
                reviewed.extend(item['sha256'] for item in mcp_captures)
                if visual_review == 'mismatch':
                    reviewed[0] = '0' * 64
                verdict = 'FAIL' if visual_review == 'fail' else 'PASS'
                (root / 'visual-review.json').write_text(json.dumps({
                    'verdict': verdict, 'reviewer': 'codex', 'notes': 'Inspected all DSVR and MCP captures.',
                    'image_sha256': reviewed,
                }))
            trace = [{'event': 'header', 'qpc': 100, 'pid': 123},
                     {'event': 'footer', 'qpc': 1000, 'pid': 123, 'events': 0,
                      'dropped': 0, 'interrupted': False, 'io_error': False}]
            (root / 'DSVR-trace-123.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in trace))
            process = subprocess.run([sys.executable, '-B', str(REPORT), str(root)],
                                     capture_output=True, text=True)
            result = json.loads((root / 'game-report.json').read_text())
            self.assertTrue((root / 'game-report.html').is_file())
            return process.returncode, result

    def test_complete_openxr_simulator_game_run(self):
        code, result = self.run_report()
        self.assertEqual(code, 0)
        self.assertEqual(result['verdict'], 'PASS')
        self.assertEqual(result['physical_melee_gate'], 'UNVERIFIED')
        self.assertEqual(result['visual_review'], 'PASS')
        self.assertEqual(len(result['images']), 8)
        self.assertEqual(len(result['mcp_images']), 4)
        self.assertEqual(result['memory_soak']['samples'], 601)

    def test_missing_simulator_controller_value_fails(self):
        def remove_trigger(_receipt, _cases, _events, summary, _mcp, _root):
            next(action for action in summary['actions']
                 if action['name'] == 'trigger' and action['hand'] == 'left')['nonzero'] = 0
        code, result = self.run_report(remove_trigger)
        self.assertNotEqual(code, 0)
        self.assertTrue(any('nonzero simulator-driven trigger' in failure for failure in result['failures']))

    def test_unrestored_transaction_fails(self):
        code, result = self.run_report(lambda receipt, *_: receipt.update(state='RecoveryBlocked'))
        self.assertNotEqual(code, 0)
        self.assertTrue(any('transaction did not restore' in failure for failure in result['failures']))

    def test_missing_simulator_layer_provenance_fails(self):
        code, result = self.run_report(lambda _receipt, _cases, events, *_: events.pop(0))
        self.assertNotEqual(code, 0)
        self.assertTrue(any('runtime-passthrough scenario' in failure for failure in result['failures']))

    def test_missing_visual_review_fails(self):
        code, result = self.run_report(visual_review=False)
        self.assertNotEqual(code, 0)
        self.assertEqual(result['visual_review'], 'PENDING')
        self.assertTrue(any('Visual review is missing' in failure for failure in result['failures']))

    def test_visual_review_must_match_current_images(self):
        code, result = self.run_report(visual_review='mismatch')
        self.assertNotEqual(code, 0)
        self.assertTrue(any('does not identify the current captured images' in failure for failure in result['failures']))

    def test_visual_review_failure_fails_run(self):
        code, result = self.run_report(visual_review='fail')
        self.assertNotEqual(code, 0)
        self.assertTrue(any('Recorded visual review failed' in failure for failure in result['failures']))

    def test_memory_growth_fails(self):
        def grow_memory(_receipt, _cases, _events, _summary, _mcp, root):
            lines = (root / 'memory.csv').read_text().splitlines()
            for index in range(1, 602):
                row = lines[index].split(',')
                row[3] = str(500 + index)
                lines[index] = ','.join(row)
            (root / 'memory.csv').write_text('\n'.join(lines) + '\n')
        code, result = self.run_report(grow_memory)
        self.assertNotEqual(code, 0)
        self.assertTrue(any('grew' in failure and 'limit 512 MiB' in failure for failure in result['failures']))


if __name__ == '__main__':
    unittest.main()
