#!/usr/bin/env python3
"""Host-only tests: no real SSH, kobo-touch, or device access."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('bench-notebook-edits.py')
SPEC = importlib.util.spec_from_file_location('bench_notebook_edits', SCRIPT)
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


class Clock:
    now = 0
    def sleep(self, seconds):
        self.now += seconds
    def monotonic(self):
        return self.now


class FakeRunner:
    def __init__(self, mode='ok', delay=0):
        self.mode = mode
        self.calls = []
        self.lines = []
        self.pending = []
        self.delay = delay
        self.polls = 0

    def __call__(self, argv, *, input=None):
        self.calls.append((argv, input))
        if argv[0] == 'ssh':
            assert '/proc/mounts' in input
            if 'cat /mnt/onboard/.kobo/version' in input:
                return 'serial,4.38.23852\n'
            if 'wc -l' in input:
                return str(10 + len(self.lines)) + '\n'
            if 'tail -n' in input:
                self.polls += 1
                if self.pending and self.polls >= self.delay:
                    self.lines.extend(self.pending)
                    self.pending = []
                return ''.join(self.lines)
            if BENCH.EXPERIMENT in input:
                return ''
            raise AssertionError(input)
        assert argv[0] == BENCH.TOUCH
        if argv[1] == 'tap' and argv[3] in ('652', '582') and self.mode != 'timeout':
            op = 'page-move-later' if argv[3] == '652' else 'page-move-earlier'
            if self.mode == 'settle-only':
                op += '-settle'
            result = 'failed' if self.mode == 'failed' else 'ok'
            if self.mode == 'wrong':
                op = 'page-duplicate'
            self.pending.append(f'perf: notebook-edit op={op} result={result} total-ms=20 engine-ms=2\n')
            if self.mode == 'extra-settle':
                self.pending[-1] = self.pending[-1].rstrip() + ' variant=position\n'
                self.pending.append(f'perf: notebook-edit op={op}-settle result=ok variant=position total-ms=150 paint-ms=90 paints=3\n')
            self.polls = 0
        return ''


class BenchmarkTests(unittest.TestCase):
    def args(self, out, *extra):
        return BENCH.build_argument_parser().parse_args(['--out', str(out), '--timeout', '1', *extra])

    def execute(self, args, runner):
        clock = Clock()
        with contextlib.redirect_stdout(io.StringIO()):
            return BENCH.run_benchmark(args, runner=runner, sleep=clock.sleep, monotonic=clock.monotonic)

    def test_pair_iterations_capture_metadata_and_gate_each_edit(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'result'
            runner = FakeRunner(delay=2)
            self.execute(self.args(out, '--iterations', '2', '--shot-every', '2'), runner)
            metadata = json.loads((out / 'run.json').read_text())
            self.assertEqual(metadata['status'], 'complete')
            self.assertEqual(metadata['completed_iterations'], 2)
            self.assertEqual(metadata['firmware'], 'serial,4.38.23852')
            self.assertEqual(len((out / 'trace.txt').read_text().splitlines()), 4)
            taps = [a for a, _ in runner.calls if a[1] == 'tap']
            self.assertEqual(len(taps), 8)
            self.assertEqual([a[2:4] for a in taps[:4]], [['1252', '112'], ['998', '652'], ['1252', '112'], ['1010', '582']])
            self.assertTrue(all('--shot' in a for a in taps))
            self.assertEqual(sum(a[1] == 'screen' for a, _ in runner.calls), 8)
            ssh_inputs = [v for a, v in runner.calls if a[0] == 'ssh']
            self.assertTrue(any('tail -n +11' in v for v in ssh_inputs))
            self.assertTrue(all('>' not in v for v in ssh_inputs))

    def test_failure_timeout_and_wrong_op_abort_before_second_move(self):
        for mode, message in [('failed', 'result=failed'), ('timeout', 'timeout'), ('wrong', 'unexpected')]:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / 'result'
                runner = FakeRunner(mode)
                with self.assertRaisesRegex(BENCH.BenchmarkError, message):
                    self.execute(self.args(out), runner)
                self.assertEqual(sum(a[1] == 'tap' for a, _ in runner.calls), 2)
                metadata = json.loads((out / 'run.json').read_text())
                self.assertEqual(metadata['status'], 'aborted')
                self.assertIn(message, metadata['error'])
                self.assertTrue((out / 'trace.txt').exists())

    def test_dry_run_does_not_execute_or_create_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'absent'
            result = subprocess.run([sys.executable, str(SCRIPT), '--dry-run', '--out', str(out)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(out.exists())
            self.assertIn('ssh', result.stdout)
            self.assertIn('sh -s', result.stdout)
            self.assertIn('tap 998 652', result.stdout)
            self.assertIn('expect page-move-earlier result=ok', result.stdout)

    def test_variant_settle_records_are_extra_and_retained(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'result'
            self.execute(self.args(out, '--iterations', '2'), FakeRunner('extra-settle'))
            analysis = BENCH.SUMMARY.analyze_logs([out / 'trace.txt'])
            self.assertEqual(len(analysis.notebook_edits), 8)
            self.assertEqual(len(BENCH.synchronous_ops(analysis.notebook_edits)), 4)
            report = BENCH.SUMMARY.edit_report(analysis)
            self.assertEqual(report['groups']['page-move-later-settle@position']['n'], 2)

    def test_settle_record_cannot_satisfy_expected_operation(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(BENCH.BenchmarkError, 'timeout'):
                self.execute(self.args(Path(tmp) / 'result'), FakeRunner('settle-only'))

    def test_repeated_equal_operations_require_their_own_settle(self):
        parse = BENCH.SUMMARY.parse_notebook_edit
        edit = parse('perf: notebook-edit op=x result=ok variant=setpart total-ms=10')
        settle = parse('perf: notebook-edit op=x-settle result=ok variant=setpart total-ms=20')
        self.assertFalse(BENCH.last_edit_settled([edit, settle, edit]))
        self.assertTrue(BENCH.last_edit_settled([edit, settle, edit, settle]))
        wrong = parse('perf: notebook-edit op=x-settle result=ok total-ms=20')
        self.assertFalse(BENCH.last_edit_settled([edit, wrong]))

    def test_set_experiment_is_guarded_atomic_and_recorded_before_taps(self):
        for value in ('move=position dup=setpart layer-activate=none settle=1',
                      'move=stock dup=stock layer-activate=stock settle=0', 'settle=1', ''):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / 'result'
                runner = FakeRunner()
                self.execute(self.args(out, '--set-experiment', value), runner)
                command = runner.calls[0][1]
                self.assertIn('/proc/mounts', command)
                self.assertIn('$3 == "vfat"', command)
                self.assertIn('sync', command)
                self.assertIn('mv "$tmp"', command) if value else self.assertIn('rm -f', command)
                self.assertEqual(json.loads((out / 'run.json').read_text())['set_experiment'], value)
                dry = subprocess.run([sys.executable, str(SCRIPT), '--dry-run', '--set-experiment', value],
                                     capture_output=True, text=True)
                self.assertEqual(dry.returncode, 0, dry.stderr)
                self.assertIn(BENCH.EXPERIMENT, dry.stdout)

    def test_invalid_experiments_fail_before_any_runner_call(self):
        for value in ('move=bad', 'dup=scroll', 'dup=setpart dup=stock', 'settle=2', 'settle=1 settle=0', 'unknown=stock',
                      'move=position;touch /tmp/no', 'x' * 257, '   '):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as tmp:
                runner = FakeRunner()
                with self.assertRaises(ValueError):
                    self.execute(self.args(Path(tmp) / 'result', '--set-experiment', value), runner)
                self.assertFalse(runner.calls)

    def test_layer_activate_scenario(self):
        _, scenario = BENCH.load_scenario(SCRIPT.with_name('bench-notebook-edits.example.json'), 'layer-activate')
        self.assertEqual([s['tap'] for s in scenario['steps']], [[1252, 112], [936, 361], [990, 211]])
        self.assertEqual(scenario['steps'][1]['wait_s'], 1.5)
        self.assertEqual(scenario['steps'][-1]['expect_op'], 'layer-activate')

    def test_bad_config_rejected_before_runner(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp) / 'config.json'
            config.write_text(json.dumps({'scenarios': [{'name': 'bad', 'op': 'page-move-later',
                                                         'steps': [{'tap': [1404, 112]}]}]}))
            args = self.args(Path(tmp) / 'result', '--config', str(config), '--scenario', 'bad')
            runner = FakeRunner()
            with self.assertRaisesRegex(ValueError, 'coordinates'):
                self.execute(args, runner)
            self.assertEqual(runner.calls, [])

    def test_truncated_trace_aborts_before_tap(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake = FakeRunner()
            counts = 0
            def runner(argv, *, input=None):
                nonlocal counts
                if input and 'wc -l' in input:
                    counts += 1
                    return '10\n' if counts == 1 else '0\n'
                return fake(argv, input=input)
            with self.assertRaisesRegex(BENCH.BenchmarkError, 'truncated'):
                self.execute(self.args(Path(tmp) / 'result'), runner)
            self.assertFalse(any(a[1] == 'tap' for a, _ in fake.calls))


if __name__ == '__main__':
    unittest.main()
