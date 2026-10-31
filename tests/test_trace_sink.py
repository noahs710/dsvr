"""External writer lifecycle tests; no game, hooks or save files."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import msvcrt

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateEventW.restype = ctypes.c_void_p
kernel.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]


def start(executable, path):
    read_fd, write_fd = os.pipe()
    handle = msvcrt.get_osfhandle(read_fd)
    event = kernel.CreateEventW(None, False, False, None)
    assert event
    os.set_handle_inheritable(handle, True)
    os.set_handle_inheritable(event, True)
    startup = subprocess.STARTUPINFO()
    startup.lpAttributeList = {'handle_list': [handle, event]}
    try:
        child = subprocess.Popen([executable, str(handle), str(event), str(path)],
                                 startupinfo=startup, close_fds=True)
    finally:
        os.close(read_fd)
        os.set_handle_inheritable(event, False)
    return child, write_fd, event


def wait_report(path):
    report = Path(str(path) + '.transport.json')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if report.exists():
            try:
                return json.loads(report.read_text())
            except (ValueError, OSError):
                pass
        time.sleep(.01)
    raise AssertionError('external writer did not finalize')


def main():
    executable = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix='dsvr-sink-') as directory:
        path = Path(directory) / 'normal.jsonl'
        child, writer, event = start(executable, path)
        assert kernel.WaitForSingleObject(event, 5000) == 0
        payload = b'{"event":"header","qpc":1}\n{"event":"future_extension","qpc":2}\n'
        os.write(writer, payload + b'\x1e\n')
        assert kernel.WaitForSingleObject(event, 5000) == 0
        assert path.read_bytes() == payload
        os.write(writer, b'{"event":"footer","qpc":3}\n')
        os.close(writer)
        assert child.wait(timeout=10) == 0
        kernel.CloseHandle(event)
        report = wait_report(path)
        assert report['producer_footer_seen'] and report['complete_lines'] == 3
        assert report['trailing_bytes'] == 0 and not report['io_error']
        assert report['event_counts']['future_extension'] == 1
        original = path.read_bytes()
        child, writer, event = start(executable, path)
        os.close(writer)
        assert child.wait(timeout=10) != 0
        kernel.CloseHandle(event)
        assert path.read_bytes() == original
        path = Path(directory) / 'killed.jsonl'
        producer = subprocess.Popen([sys.executable, __file__, executable, '--producer', str(path)])
        try:
            deadline = time.monotonic() + 10
            while not Path(str(path) + '.ready').exists() and time.monotonic() < deadline:
                time.sleep(.01)
            assert Path(str(path) + '.ready').exists()
            producer.kill()
            producer.wait(timeout=5)
            report = wait_report(path)
            assert not report['producer_footer_seen'] and report['complete_lines'] == 2
            assert report['trailing_bytes'] == 7 and not report['io_error']
            assert path.read_bytes().endswith(b'partial')
        finally:
            if producer.poll() is None:
                producer.kill()
                producer.wait(timeout=5)
            import time as _wt
            _wt.sleep(1.0)
    print('external writer: normal close, durable checkpoint, collision refusal, forced producer termination PASS')


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[2] == '--producer':
        path = Path(sys.argv[3])
    if len(sys.argv) == 5:
        raise AssertionError('unexpected arguments')
    if len(sys.argv) == 4 and sys.argv[2] == '--producer':
        child, writer, event = start(sys.argv[1], Path(sys.argv[3]))
        assert kernel.WaitForSingleObject(event, 5000) == 0
        os.write(writer, b'{"event":"header","qpc":1}\n{"event":"movement","qpc":2}\n\x1e\n')
        assert kernel.WaitForSingleObject(event, 5000) == 0
        os.write(writer, b'partial')
        Path(sys.argv[3] + '.ready').write_text('ready')
        time.sleep(60)
    else:
        main()
