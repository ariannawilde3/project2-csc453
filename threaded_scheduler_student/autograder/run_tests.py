#!/usr/bin/env python3
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from lib.compare import compare_exact
from lib.invariants import check_invariants

ROOT = Path(__file__).resolve().parents[1]
TEST_ROOT = ROOT / 'autograder' / 'tests'
BINARY = ROOT / 'scheduler_sim'


def build():
    subprocess.run(['make', 'clean'], cwd=ROOT, check=True)
    subprocess.run(['make'], cwd=ROOT, check=True)


def run_case(case_path):
    case = json.loads(Path(case_path).read_text())
    repeat = case.get('repeat', 1)
    timeout = case.get('timeout_sec', 2)

    def arg_value(flag, default=None):
        args = case['args']
        if flag not in args:
            return default
        idx = args.index(flag)
        if idx + 1 >= len(args):
            return default
        return args[idx + 1]

    workload = ROOT / arg_value('--input')
    cpus = int(arg_value('--cpus', '1'))
    policy = arg_value('--policy')
    quantum = arg_value('--quantum')
    quantum = int(quantum) if quantum is not None else None

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        for i in range(repeat):
            trace = td / f'{case["name"]}_{i}.trace'
            stats = td / f'{case["name"]}_{i}.stats'
            cmd = [str(BINARY)] + case['args'] + ['--trace-out', str(trace), '--stats-out', str(stats)]
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            if proc.returncode != 0:
                return False, f'nonzero exit\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}'
            if case['mode'] == 'exact':
                ok, msg = compare_exact(trace, stats, ROOT / case['expected_trace'], ROOT / case['expected_stats'])
            else:
                ok, msg = check_invariants(
                    trace,
                    workload_path=workload,
                    stats_path=stats,
                    cpus=cpus,
                    policy=policy,
                    quantum=quantum,
                )
            if not ok:
                return False, f'iteration {i}: {msg}'
    return True, 'ok'


def main():
    build()
    cases = sorted(TEST_ROOT.rglob('*.json'))
    failures = 0
    for case in cases:
        ok, msg = run_case(case)
        status = 'PASS' if ok else 'FAIL'
        print(f'[{status}] {case.stem}: {msg}')
        if not ok:
            failures += 1
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
