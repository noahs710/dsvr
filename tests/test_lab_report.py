"""Read a report emitted by the production scenario engine, not a simulator."""
import json
import subprocess
import sys


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


result = subprocess.run([sys.argv[1], "--report"], check=True, timeout=10,
                        capture_output=True, text=True)
report = json.loads(result.stdout)
check(report["scenario"] == "idle-walk-stop-v1", "scenario identity missing")
check(report["scenario_verdict"] == "PASS", "finished fixture incorrectly classified")
check(report["runtime_verified"] is False, "fixture claimed native runtime acceptance")
check(report["phase"] == "passed" and report["reason"] == "none", "status lost")
check(all(n > 0 for n in report["polls"]), "unconsumed scenario reported as complete")
check(all(n >= 2 for n in report["observations"]), "missing native-sample coverage")
check(abs(report["walk_units"] - .2) < .0001, "root evidence lost")
check(report["limits"]["lease_ms"] == 100 and report["limits"]["walk_ms"] == 500,
      "fixed experiment limits missing")
print("test_lab_report: PASS")
