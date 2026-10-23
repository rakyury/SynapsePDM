"""Build/run the actual C start scheduler with explicitly fictional load fixtures.

Example (Linux, sanitizers enabled):
  python pdm_cube/tests/test_start_simulator.py --output artifacts/start-simulation
Windows without sanitizer runtime: add --cc gcc --no-sanitize.
No adapter, hardware GPIO, flash or CAN output is accessed.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--no-sanitize', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('start-simulation-results'))
    args = parser.parse_args()
    if args.no_sanitize and os.name != 'nt':
        parser.error('--no-sanitize is only supported for Windows hosts; CI must sanitize')
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    executable = output / ('start_simulator.exe' if os.name == 'nt' else 'start_simulator')
    compiler = [args.cc] if Path(args.cc).is_file() else shlex.split(args.cc, posix=os.name != 'nt')
    command = compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g']
    if not args.no_sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
    command += ['-I' + str(root / 'src'), str(root / 'tests/start_simulator.c'),
                str(root / 'src/pdm_start_admission.c'), str(root / 'src/pdm_board_profile.c'),
                '-o', str(executable)]
    environment = os.environ.copy()
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    log: list[str] = []
    reports: list[dict] = []

    def run(invocation):
        log.append(shlex.join(invocation))
        result = subprocess.run(invocation, capture_output=True, text=True,
                                timeout=60, env=environment)
        log.extend((result.stdout, result.stderr))
        if result.returncode:
            raise RuntimeError(f'Command failed with exit {result.returncode}: {invocation[0]}')
        return result.stdout

    try:
        guard_source = output / 'hardware_guard.c'
        guard_source.write_text('#include "pdm_board_profile.h"\n'
                                '_Static_assert(PDM_BOARD_HARDWARE_CHANNEL_COUNT == 14, "physical count");\n'
                                'int main(void) { return 0; }\n', encoding='utf-8')
        guard_command = compiler + ['-std=c11', '-I' + str(root / 'src'),
                                    '-fsyntax-only', str(guard_source)]
        run(guard_command)
        for unsupported in (24, 0, 99):
            invocation = guard_command + [f'-DPDM_BOARD_PROFILE={unsupported}']
            log.append(shlex.join(invocation))
            result = subprocess.run(invocation, capture_output=True, text=True,
                                    timeout=60, env=environment)
            log.extend((result.stdout, result.stderr))
            if result.returncode == 0 or 'Only Synapse14 hardware is supported' not in result.stderr:
                raise AssertionError(f'Hardware profile {unsupported} did not fail at the intended compile guard')
        run(command)
        for profile in (14, 24):
            for slots in (2, 3, 4):
                for scenario in ('dispatch', 'total-budget', 'group-budget', 'active-budget', 'fault', 'timeout', 'reset'):
                    report = json.loads(run([str(executable), scenario, str(slots), str(profile)]))
                    if report['scenario'] != scenario or report['profile_channels'] != profile:
                        raise AssertionError('Simulator reported a different scenario/profile')
                    if report['slot_limit'] != slots or report['max_reserved_slots'] > slots:
                        raise AssertionError('Concurrent reservation exceeded configured fixture slots')
                    if report['physical_outputs_on'] != 0 or report['hardware_authorized'] is not False:
                        raise AssertionError('Simulation crossed the non-actuating boundary')
                    if report['output_backend'] != 'simulated_zero_output_backend':
                        raise AssertionError('Host simulation must not claim measured physical outputs')
                    if report['fixture_only'] is not True or report['checks_passed'] is not True:
                        raise AssertionError('Scenario invariant check did not pass')
                    if scenario == 'dispatch' and report['completed_requests'] != profile:
                        raise AssertionError('Not every profile channel completed its virtual start')
                    reports.append(report)
        print(f'{len(reports)} actual-C profile/start scenarios PASS (simulated zero-output backend)')
        return 0
    except (OSError, ValueError, KeyError, AssertionError, RuntimeError, subprocess.TimeoutExpired) as exc:
        log.append(str(exc))
        print(f'Start simulation FAIL: {exc}')
        return 1
    finally:
        (output / 'commands.log').write_text('\n'.join(log), encoding='utf-8')
        (output / 'scenarios.json').write_text(json.dumps({
            'fixture_only': True, 'sanitizers_enabled': not args.no_sanitize,
            'scenarios': reports,
        }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    raise SystemExit(main())
