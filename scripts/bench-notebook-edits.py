#!/usr/bin/env python3
"""Benchmark configured notebook taps. Start with a session backup already verified.

The notebook must be open with its toolbar visible, on a page that supports the
chosen moves. --set-experiment explicitly updates only the experiment file.
This driver never navigates to a notebook.
Every action gets before/after screenshots; --shot-every retains numbered images
instead of overwriting the latest pair. Pair scenarios gate each edit separately.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('notebook_performance', HERE / 'summarize-notebook-performance.py')
SUMMARY = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUMMARY
SPEC.loader.exec_module(SUMMARY)
EXPERIMENT = '/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/perf-experiment.txt'
TRACE = '/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/plugin-status.txt'
TOUCH = '/data/t3/projects/kobo-touch/kobo-touch'
# Refuse reads of the bare mount point when USB mass storage has unmounted it.
MOUNT_GUARD = "awk '$2 == \"/mnt/onboard\" && $3 == \"vfat\" {found=1} END {exit !found}' /proc/mounts || exit 1; "


class BenchmarkError(RuntimeError):
    pass


class Runner:
    def __call__(self, argv, *, input=None):
        result = subprocess.run(argv, input=input, text=True, capture_output=True,
                                timeout=15, env={**os.environ, 'KOBO_HOST': '192.168.129.92'})
        if result.returncode:
            raise BenchmarkError(f'{shlex.join(argv)}: {result.stderr.strip()}')
        return result.stdout


def ssh(runner, command):
    # Supplying stdin directly is equivalent to echo '<command>' | ssh kobo sh -s.
    return runner(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', 'kobo', 'sh', '-s'],
                  input=MOUNT_GUARD + command + '\n')


def experiment_command(value):
    if len(value.encode('utf-8')) > 256:
        raise ValueError('experiment exceeds 256 bytes')
    allowed = {'move': {'stock', 'position'}, 'dup': {'stock', 'setpart'},
               'layer-activate': {'stock', 'none'}, 'settle': {'0', '1'}}
    seen = set()
    for token in value.split():
        pair = token.split('=')
        if (len(pair) != 2 or pair[0] not in allowed or pair[1] not in allowed[pair[0]]
                or pair[0] in seen):
            raise ValueError('invalid experiment key/value or duplicate key')
        seen.add(pair[0])
    if value == '':
        return f'rm -f {shlex.quote(EXPERIMENT)} && sync'
    if not seen:
        raise ValueError('use an empty string to remove the experiment')
    # No newline is added: the on-device reader has a strict 256-byte limit.
    return (f'tmp={shlex.quote(EXPERIMENT)}.tmp.$$; '
            "trap 'rm -f \"$tmp\"' EXIT HUP INT TERM; "
            f'printf %s {shlex.quote(value)} > "$tmp" && '
            f'mv "$tmp" {shlex.quote(EXPERIMENT)} && sync')


def synchronous_ops(records):
    # Settle diagnostics are extra records, never substitutes for the exact op.
    return [s.op for s in records if not s.op.endswith('-settle')]


def last_edit_settled(records):
    indices = [i for i, s in enumerate(records) if not s.op.endswith('-settle')]
    if not indices:
        return True
    index = indices[-1]
    last = records[index]
    return any(s.op == last.op + '-settle' and s.variant == last.variant
               for s in records[index + 1:])


def load_scenario(path, name):
    config = json.loads(path.read_text())
    scenarios = config['scenarios']
    names = [s['name'] for s in scenarios]
    if len(names) != len(set(names)):
        raise ValueError('duplicate scenario names')
    matches = [s for s in scenarios if s['name'] == name]
    if not matches:
        raise ValueError(f'unknown scenario {name!r}; choose from {", ".join(names)}')
    scenario = matches[0]
    steps = scenario['steps']
    if not steps:
        raise ValueError('scenario has no steps')
    op = scenario['op']
    if not isinstance(op, str) or not re.fullmatch('[a-z0-9-]+', op):
        raise ValueError('invalid scenario op')
    for step in steps:
        tap = step['tap']
        if (not isinstance(tap, list) or len(tap) != 2 or
                any(type(x) is not int for x in tap) or
                not (0 <= tap[0] < 1404 and 0 <= tap[1] < 1872)):
            raise ValueError('tap must contain two integer portrait coordinates')
        wait = step.get('wait_s', 0)
        if type(wait) not in (int, float) or not math.isfinite(wait) or wait < 0:
            raise ValueError('wait_s must be finite and non-negative')
        if 'expect_op' in step and not re.fullmatch('[a-z0-9-]+', step['expect_op']):
            raise ValueError('invalid step expect_op')
    # Simple scenarios expect one record after their final tap. Multi-edit
    # scenarios mark every edit-producing step, preventing blind continuation.
    if not any('expect_op' in s for s in steps):
        steps[-1]['expect_op'] = op
    if 'expect_op' not in steps[-1]:
        raise ValueError('last step must expect an edit record')
    return config, scenario


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def run_benchmark(args, *, runner=None, sleep=time.sleep, monotonic=time.monotonic):
    config, scenario = load_scenario(args.config, args.scenario)
    command = experiment_command(args.set_experiment) if args.set_experiment is not None else None
    out = args.out or Path('bench-results') / (datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + args.label)
    if args.dry_run:
        def planned(argv, *, input=None):
            prefix = f'printf %s {shlex.quote(input)} | ' if input else 'KOBO_HOST=192.168.129.92 '
            print(prefix + shlex.join(argv))
            return '0\n' if input and 'wc -l' in input else ''
        runner = planned
        if command is not None:
            ssh(runner, command)
        ssh(runner, 'cat /mnt/onboard/.kobo/version')
        ssh(runner, f'wc -l < {TRACE}')
        for iteration in range(args.iterations):
            for index, step in enumerate(scenario['steps']):
                before, after = shot_paths(out, iteration, index, args.shot_every)
                runner([TOUCH, 'screen', str(before)])
                runner([TOUCH, 'tap', *map(str, step['tap']), '--shot', str(after)])
                print(f"wait {step.get('wait_s', 0)} seconds")
                ssh(runner, f'wc -l < {TRACE}')
                ssh(runner, f'tail -n +1 {TRACE}')
                if 'expect_op' in step:
                    print(f"poll every 0.5s up to {args.timeout}s: expect {step['expect_op']} result=ok")
                    print('collect extra -settle records for up to 3.25s after synchronous completion')
        print(f'Would save {out}/trace.txt and {out}/run.json, then print summary')
        return out

    runner = runner or Runner()
    out.mkdir(parents=True, exist_ok=False)
    metadata = {'config': config, 'scenario': args.scenario, 'label': args.label,
                'iterations': args.iterations, 'started_at': timestamp(), 'status': 'running',
                'timeout_s': args.timeout, 'shot_every': args.shot_every,
                'set_experiment': args.set_experiment}
    raw = ''
    start = None
    def collect():
        nonlocal raw
        count = int(ssh(runner, f'wc -l < {TRACE}').strip())
        if count < start:
            raise BenchmarkError('trace was truncated or rotated; stop')
        raw = ssh(runner, f'tail -n +{start + 1} {TRACE}')
        (out / 'trace.txt').write_text(raw)
        analysis = SUMMARY.analyze_sources([('trace.txt', raw.splitlines())])
        if analysis.malformed_edits:
            raise BenchmarkError('malformed notebook-edit record: ' + analysis.malformed_edits[0].reason)
        if any(s.result == 'failed' for s in analysis.notebook_edits):
            raise BenchmarkError('notebook-edit result=failed; stop')
        return analysis.notebook_edits
    try:
        if command is not None:
            ssh(runner, command)
        metadata['firmware'] = ssh(runner, 'cat /mnt/onboard/.kobo/version').strip()
        start = int(ssh(runner, f'wc -l < {TRACE}').strip())
        metadata['initial_line_count'] = start
        expected = []
        for iteration in range(args.iterations):
            for index, step in enumerate(scenario['steps']):
                # Check for delayed failures or unrelated edits before every tap.
                records = collect()
                if synchronous_ops(records) != expected:
                    raise BenchmarkError('unexpected edit records before tap; stop')
                before, after = shot_paths(out, iteration, index, args.shot_every)
                runner([TOUCH, 'screen', str(before)])
                runner([TOUCH, 'tap', *map(str, step['tap']), '--shot', str(after)])
                deadline = monotonic() + args.timeout
                sleep(step.get('wait_s', 0))
                if 'expect_op' in step:
                    expected.append(step['expect_op'])
                settle_deadline = None
                while True:
                    records = collect()
                    actual = synchronous_ops(records)
                    if actual != expected[:len(actual)] or len(actual) > len(expected):
                        raise BenchmarkError(f'unexpected edit records: expected {expected}, got {actual}')
                    if actual == expected:
                        if 'expect_op' not in step or last_edit_settled(records):
                            break
                        # Let deferred painting finish before the next edit can
                        # cancel its observer, and collect the final settle record.
                        # Older plugins may omit it; it is never the expected op.
                        if settle_deadline is None:
                            settle_deadline = monotonic() + 3.25
                        if monotonic() >= settle_deadline:
                            break
                        sleep(min(.5, max(0, settle_deadline - monotonic())))
                        continue
                    if monotonic() >= deadline:
                        raise BenchmarkError(f'timeout waiting for {expected[-1]}; stop')
                    sleep(min(.5, max(0, deadline - monotonic())))
            metadata['completed_iterations'] = iteration + 1
        metadata['status'] = 'complete'
        print(SUMMARY.format_text(SUMMARY.analyze_sources([('trace.txt', raw.splitlines())])))
        return out
    except Exception as error:
        metadata['status'] = 'aborted'
        metadata['error'] = str(error)
        raise
    finally:
        metadata['finished_at'] = timestamp()
        (out / 'trace.txt').write_text(raw)
        (out / 'run.json').write_text(json.dumps(metadata, indent=2) + '\n')


def shot_paths(out, iteration, step, every):
    name = f'{iteration+1:04d}-{step+1:02d}' if every and (iteration+1) % every == 0 else 'latest'
    return out / f'{name}-before.png', out / f'{name}-after.png'


def positive(value):
    n = int(value)
    if n <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return n


def timeout_value(value):
    n = float(value)
    if not math.isfinite(n) or n <= 0:
        raise argparse.ArgumentTypeError('must be positive and finite')
    return n


def build_argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=HERE / 'bench-notebook-edits.example.json')
    parser.add_argument('--scenario', default='move-pair')
    parser.add_argument('--iterations', type=positive, default=1)
    parser.add_argument('--label', default='edits')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--timeout', type=timeout_value, default=30)
    parser.add_argument('--shot-every', type=positive, default=0, metavar='N')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--set-experiment', default=None, metavar='KEY=VALUE ...',
                        help='write experiment file before run; empty string removes it')
    return parser


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    if not re.fullmatch('[A-Za-z0-9_-]+', args.label):
        parser.error('--label must use letters, digits, underscore or hyphen')
    try:
        out = run_benchmark(args)
    except (OSError, ValueError, KeyError, TypeError, BenchmarkError, subprocess.SubprocessError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    print(f'Output: {out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
