"""Bounded synthetic stereo-transaction audit. Never enables native replay.

This is deliberately NOT a reader for current DSVR native trace captures.
Consistency of caller-supplied records does not establish their truth/coverage.
"""
import argparse
import json
import math
from pathlib import Path

MAX_BYTES = 262144
PHASES = ('predict', 'retain', 'consume_left', 'workers_left', 'ready_left',
          'copy_left', 'consume_right', 'workers_right', 'ready_right',
          'copy_right', 'restore', 'release', 'submit')
IDENTITIES = ('capture', 'transaction', 'scene', 'load_epoch', 'simulation',
              'resource_generation', 'plan', 'device', 'thread',
              'tracking_sequence', 'space_epoch', 'predicted_ns')
COUNTERS = ('input', 'physics', 'animation', 'combat', 'present', 'history', 'retirement')
COMMON = ('phase', 'index', 'elapsed_ns', 'identity', 'counters')
EDIT = ('field', 'owner', 'lifetime', 'original', 'written')


def _require(condition, detail):
    if not condition:
        raise ValueError(detail)


def _keys(value, names, label):
    _require(type(value) is dict and set(value) == set(names),
             f'{label}: missing or unknown fields')


def _uint(value, label, minimum=0, maximum=(1 << 64)-1):
    _require(type(value) is int and minimum <= value <= maximum,
             f'{label}: invalid integer')


def _matrix(value):
    _require(type(value) is list and len(value) == 16, 'matrix: expected 16 elements')
    _require(all(type(v) in (int, float) and math.isfinite(v) for v in value),
             'matrix: nonfinite or nonnumeric element')


def _eye(value):
    _keys(value, ('view', 'projection'), 'predicted eye')
    _matrix(value['view']); _matrix(value['projection'])


def _ids(value, label):
    _require(type(value) is list and 1 <= len(value) <= 64, f'{label}: invalid count')
    for item in value:
        _uint(item, label, 1)
    _require(len(set(value)) == len(value), f'{label}: duplicate identity')


def _bytes(value):
    _require(type(value) is str and 0 < len(value) <= 512 and len(value) % 2 == 0 and
             all(c in '0123456789abcdef' for c in value), 'edit: invalid lowercase hex bytes')


def _edit(value, restoring=False):
    extras = ('current_owner', 'current_lifetime', 'observed', 'restored') if restoring else ()
    _keys(value, EDIT + extras, 'owned edit')
    name = value['field']
    _require(type(name) is str and 0 < len(name) <= 64 and
             all(c.isascii() and (c.isalnum() or c in '_.') for c in name),
             'edit: invalid field name')
    _uint(value['owner'], 'edit owner', 1); _uint(value['lifetime'], 'edit lifetime', 1)
    for key in ('original', 'written') + (('observed', 'restored') if restoring else ()):
        _bytes(value[key])
        _require(len(value[key]) == len(value['original']), 'edit: unequal byte widths')
    return (name, value['owner'], value['lifetime'])


def _check(transaction):
    _keys(transaction, ('schema', 'kind', 'budget_ns', 'dropped', 'events'), 'transaction')
    _uint(transaction['schema'], 'schema', 1, 1)
    _require(transaction['kind'] == 'synthetic_native_stereo_transaction',
             'only explicitly synthetic records are supported')
    _uint(transaction['budget_ns'], 'budget_ns', 1, 20_000_000_000)
    _uint(transaction['dropped'], 'dropped', 0, 0)
    events = transaction['events']
    _require(type(events) is list and len(events) == len(PHASES),
             'incomplete or extra transaction phases')
    identity = counters = eyes = workers = None
    edits, ready, images = {}, {}, []
    last_time = -1
    payloads = {'predict': ('eyes',), 'retain': ('workers', 'edits'),
                'restore': ('edits',), 'release': (), 'submit': ('images',)}
    for index, (phase, event) in enumerate(zip(PHASES, events), 1):
        if phase.startswith('consume_'):
            extra = ('eye', 'view', 'projection')
        elif phase.startswith('workers_'):
            extra = ('completed',)
        elif phase.startswith('ready_'):
            extra = ('source', 'subresource', 'completion')
        elif phase.startswith('copy_'):
            extra = ('source', 'subresource', 'completion', 'destination', 'destination_subresource')
        else:
            extra = payloads[phase]
        _keys(event, COMMON + extra, phase)
        _require(event['phase'] == phase, f'expected phase {phase}')
        _uint(event['index'], 'index', index, index)
        _uint(event['elapsed_ns'], 'elapsed_ns', 0, transaction['budget_ns'])
        _require(event['elapsed_ns'] > last_time, 'observation time did not increase')
        last_time = event['elapsed_ns']
        for key, names in [('identity', IDENTITIES), ('counters', COUNTERS)]:
            _keys(event[key], names, key)
            for field in names:
                _uint(event[key][field], field, 1 if key == 'identity' else 0)
        if identity is None:
            identity, counters = event['identity'], event['counters']
        _require(event['identity'] == identity, f'{phase}: identity replacement')
        _require(event['counters'] == counters, f'{phase}: simulation/presentation/retirement advanced')

        if phase == 'predict':
            eyes = event['eyes']
            _require(type(eyes) is list and len(eyes) == 2, 'prediction lacks two eyes')
            for eye in eyes:
                _eye(eye)
            _require(eyes[0]['view'] != eyes[1]['view'], 'identical eye views')
        elif phase == 'retain':
            workers = event['workers']; _ids(workers, 'retained workers')
            _require(type(event['edits']) is list and len(event['edits']) <= 32,
                     'invalid owned edit count')
            for edit in event['edits']:
                key = _edit(edit)
                _require(key not in edits, 'duplicate owned edit')
                edits[key] = edit
        elif phase.startswith('consume_'):
            eye = 0 if phase.endswith('left') else 1
            _uint(event['eye'], 'eye', eye, eye)
            _matrix(event['view']); _matrix(event['projection'])
            _require(all(event[k] == eyes[eye][k] for k in ('view', 'projection')),
                     'consumed matrix differs from predicted eye')
        elif phase.startswith('workers_'):
            _ids(event['completed'], 'completed workers')
            _require(set(event['completed']) == set(workers), 'worker join coverage mismatch')
        elif phase.startswith(('ready_', 'copy_')):
            eye = 0 if phase.endswith('left') else 1
            for key in ('source', 'completion'):
                _uint(event[key], key, 1)
            _uint(event['subresource'], 'subresource')
            source = tuple(event[k] for k in ('source', 'subresource', 'completion'))
            if phase.startswith('ready_'):
                _require(source[:2] not in images,
                         'later render source aliases an already copied eye')
                ready[eye] = source
            else:
                _require(source == ready[eye], 'copy has no matching image readiness')
                _uint(event['destination'], 'destination', 1)
                _uint(event['destination_subresource'], 'destination subresource')
                image = (event['destination'], event['destination_subresource'])
                _require(image not in [item[:2] for item in ready.values()] and image not in images,
                         'copied image aliases source or other eye')
                images.append(image)
        elif phase == 'restore':
            restored = set()
            _require(type(event['edits']) is list and len(event['edits']) == len(edits),
                     'restoration coverage mismatch')
            for edit in event['edits']:
                key = _edit(edit, restoring=True)
                _require(key in edits and key not in restored, 'unknown or duplicate restoration')
                original = edits[key]
                _require(all(edit[k] == original[k] for k in EDIT), 'restoration journal changed')
                _require((edit['current_owner'], edit['current_lifetime']) == key[1:],
                         'restoration owner/lifetime replaced')
                _require(edit['observed'] == edit['written'], 'restoration write conflict')
                _require(edit['restored'] == edit['original'], 'owned edit was not restored')
                restored.add(key)
        elif phase == 'submit':
            _require(type(event['images']) is list and len(event['images']) == 2,
                     'submission lacks two copied images')
            for position, image in enumerate(event['images']):
                _keys(image, ('resource', 'subresource'), 'submitted image')
                _uint(image['resource'], 'submitted resource', 1)
                _uint(image['subresource'], 'submitted subresource')
                _require((image['resource'], image['subresource']) == images[position],
                         'submitted image differs from copied eye')


def _result(errors):
    return dict(consistent=not errors, complete=False, errors=errors,
                consistency='FAIL' if errors else 'PASS',
                input_is_synthetic=True,
                acceptance=dict(native='UNVERIFIED', gpu='UNVERIFIED', comfort='UNVERIFIED'))


def validate(transaction):
    """Audit claims only; never returns native readiness or execution authority."""
    try:
        _check(transaction)
    except (ValueError, TypeError, OverflowError, RecursionError) as error:
        return _result([str(error)])
    return _result([])


def _object(pairs):
    value = {}
    for key, item in pairs:
        _require(key not in value, f'duplicate JSON key: {key}')
        value[key] = item
    return value


def read_transaction(path):
    try:
        with Path(path).open('rb') as stream:
            raw = stream.read(MAX_BYTES + 1)
        _require(len(raw) <= MAX_BYTES, 'transaction exceeds byte limit')
        def invalid_constant(value):
            raise ValueError(f'nonfinite JSON constant: {value}')
        return validate(json.loads(raw.decode('utf-8'), object_pairs_hook=_object,
                                   parse_constant=invalid_constant))
    except (OSError, ValueError, UnicodeError, RecursionError) as error:
        return _result([f'reader: {error}'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path, help='synthetic transaction JSON, not a game trace')
    result = read_transaction(parser.parse_args().path)
    print(json.dumps(result, indent=2))
    return 0 if result['consistent'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
