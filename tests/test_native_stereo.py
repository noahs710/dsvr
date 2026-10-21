"""Synthetic contract tests, not captures from a native renderer."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_stereo", ROOT / "tools/native_stereo.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def fixture():
    identity = dict(capture=1, transaction=2, scene=3, load_epoch=4,
                    simulation=5, resource_generation=6, plan=7, device=8,
                    thread=9, tracking_sequence=10, space_epoch=11, predicted_ns=12)
    left = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., .03, 0., 0., 1.]
    right = left.copy()
    right[12] = -.03
    eyes = [dict(view=left, projection=left[:12] + [0., 0., .1, 0.]),
            dict(view=right, projection=left[:12] + [0., 0., .1, 0.])]
    phases = ['predict', 'retain', 'consume_left', 'workers_left', 'ready_left',
              'copy_left', 'consume_right', 'workers_right', 'ready_right',
              'copy_right', 'restore', 'release', 'submit']
    counters = dict(input=10, physics=20, animation=30, combat=40,
                    present=0, history=0, retirement=0)
    events = [dict(phase=p, index=i+1, elapsed_ns=i*100, identity=identity.copy(),
                   counters=counters.copy()) for i, p in enumerate(phases)]
    events[0]['eyes'] = eyes
    edit = dict(field='view', owner=21, lifetime=22, original='00000000', written='01000000')
    events[1].update(workers=[31, 32], edits=[edit])
    for eye, offset in enumerate((2, 6)):
        events[offset].update(eye=eye, **copy.deepcopy(eyes[eye]))
        events[offset+1]['completed'] = [31, 32]
        events[offset+2].update(source=51, subresource=0, completion=61+eye)
        events[offset+3].update(source=51, subresource=0, completion=61+eye,
                                destination=71+eye, destination_subresource=0)
    events[10]['edits'] = [dict(edit, current_owner=21, current_lifetime=22,
                                observed='01000000', restored='00000000')]
    events[12]['images'] = [dict(resource=71, subresource=0), dict(resource=72, subresource=0)]
    return dict(schema=1, kind='synthetic_native_stereo_transaction',
                budget_ns=10000, dropped=0, events=events)


class Contract(unittest.TestCase):
    def reject(self, value):
        result = audit.validate(value)
        self.assertFalse(result['consistent'], result)
        self.assertFalse(result['complete'])
        self.assertTrue(result['errors'])
        self.assertTrue(all(v == 'UNVERIFIED' for v in result['acceptance'].values()))

    def test_baseline_never_certifies_runtime(self):
        result = audit.validate(fixture())
        self.assertTrue(result['consistent'], result)
        self.assertFalse(result['complete'])
        self.assertTrue(all(v == 'UNVERIFIED' for v in result['acceptance'].values()))

    def test_every_phase_required(self):
        for index in range(13):
            with self.subTest(index=index):
                value = fixture(); del value['events'][index]; self.reject(value)

    def test_duplicate_and_out_of_order_phases(self):
        for a, b in [(0, 2), (3, 4), (9, 11), (11, 12)]:
            value = fixture(); value['events'][a], value['events'][b] = value['events'][b], value['events'][a]
            self.reject(value)
        value = fixture(); value['events'].append(value['events'][-1]); self.reject(value)

    def test_each_identity_required_and_stable(self):
        for key in fixture()['events'][0]['identity']:
            for replacement in [0, True, '1', -1, 2**64, None]:
                with self.subTest(key=key, replacement=replacement):
                    value = fixture(); value['events'][6]['identity'][key] = replacement; self.reject(value)
            value = fixture(); value['events'][6]['identity'][key] += 100; self.reject(value)
            value = fixture(); del value['events'][6]['identity'][key]; self.reject(value)

    def test_all_counters_must_be_observed_and_unchanged(self):
        for key in fixture()['events'][0]['counters']:
            for index in range(1, 13):
                value = fixture(); value['events'][index]['counters'][key] += 1; self.reject(value)
            value = fixture(); del value['events'][6]['counters'][key]; self.reject(value)
            value = fixture(); value['events'][6]['counters'][key] = False; self.reject(value)

    def test_command_matrix_must_match_prediction(self):
        for index in [2, 6]:
            for key in ['view', 'projection']:
                value = fixture(); value['events'][index][key][0] += .01; self.reject(value)

    def test_matrix_shape_type_and_finiteness(self):
        for invalid in [None, [], [0]*15, [0]*17, [True]*16,
                        [float('nan')]*16, [float('inf')]*16, [10**1000]*16]:
            value = fixture(); value['events'][0]['eyes'][0]['view'] = invalid; self.reject(value)

    def test_identical_eye_views_and_wrong_eye(self):
        value = fixture(); value['events'][0]['eyes'][1]['view'] = value['events'][0]['eyes'][0]['view'].copy()
        value['events'][6]['view'] = value['events'][2]['view'].copy(); self.reject(value)
        for eye in [0, True, '1', 2]:
            value = fixture(); value['events'][6]['eye'] = eye; self.reject(value)

    def test_worker_join_coverage(self):
        for invalid in [[], [31], [31, 31], [31, 33], [31, True]]:
            value = fixture(); value['events'][7]['completed'] = invalid; self.reject(value)
        value = fixture(); value['events'][1]['workers'] = []; self.reject(value)

    def test_ready_copy_association(self):
        for field in ['source', 'subresource', 'completion']:
            value = fixture(); value['events'][9][field] += 1; self.reject(value)

    def test_image_alias_and_submission_association(self):
        value = fixture(); value['events'][9]['destination'] = 71; self.reject(value)
        value = fixture(); value['events'][12]['images'].reverse(); self.reject(value)
        value = fixture(); value['events'][5]['destination'] = 51; self.reject(value)
        # An image retained for the left eye must not become a later render
        # source, even if its right-eye destination is a different allocation.
        value = fixture(); value['events'][8]['source'] = 71
        value['events'][9]['source'] = 71; self.reject(value)

    def test_different_subresources_are_distinct_images(self):
        value = fixture(); value['events'][9]['destination'] = 71
        value['events'][9]['destination_subresource'] = 1
        value['events'][12]['images'][1] = dict(resource=71, subresource=1)
        self.assertTrue(audit.validate(value)['consistent'])

    def test_malformed_nested_objects(self):
        for event_index, field in [(0, 'eyes'), (1, 'workers'), (1, 'edits'),
                                   (3, 'completed'), (10, 'edits'), (12, 'images'),
                                   (6, 'identity'), (6, 'counters')]:
            for invalid in [None, True, 'invalid', 123, {}]:
                value = fixture(); value['events'][event_index][field] = invalid
                self.reject(value)

    def test_owned_restore_conflict_and_coverage(self):
        for field, replacement in dict(current_owner=99, current_lifetime=99,
                                       observed='02000000', restored='03000000',
                                       field='unowned', original='ffffffff', written='ffffffff').items():
            value = fixture(); value['events'][10]['edits'][0][field] = replacement; self.reject(value)
        value = fixture(); value['events'][10]['edits'] = []; self.reject(value)
        value = fixture(); value['events'][10]['edits'] *= 2; self.reject(value)
        value = fixture(); value['events'][1]['edits'][0]['original'] = 'xyz'; self.reject(value)

    def test_monotonicity_deadline_and_integrity(self):
        value = fixture(); value['events'][6]['index'] = 6; self.reject(value)
        value = fixture(); value['events'][6]['elapsed_ns'] = 0; self.reject(value)
        value = fixture(); value['budget_ns'] = 100; self.reject(value)
        value = fixture(); value['dropped'] = 1; self.reject(value)

    def test_unknown_fields_never_grant_authority(self):
        for key in ['authorized', 'ready', 'offline', 'native_verified']:
            value = fixture(); value[key] = True; self.reject(value)
            value = fixture(); value['events'][0][key] = True; self.reject(value)

    def test_malformed_top_level(self):
        for value in [None, [], True, {}, dict(fixture(), schema=True),
                      dict(fixture(), events=[None]*13), dict(fixture(), events=[{}]*1000)]:
            self.reject(value)

    def test_reader_and_cli(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'fixture.json'
            path.write_text(json.dumps(fixture()), encoding='utf-8')
            self.assertTrue(audit.read_transaction(path)['consistent'])
            run = subprocess.run([sys.executable, '-B', str(ROOT/'tools/native_stereo.py'), str(path)],
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertFalse(json.loads(run.stdout)['complete'])
            for raw in ['{"schema":1,"schema":1}', '{"x":NaN}', '[]', '{', 'x'*262145]:
                path.write_text(raw, encoding='utf-8'); self.reject_read(path)
            path.write_bytes(b'\xff'); self.reject_read(path)
            path.unlink(); self.reject_read(path)

    def reject_read(self, path):
        self.assertFalse(audit.read_transaction(path)['consistent'])


if __name__ == '__main__':
    unittest.main()
