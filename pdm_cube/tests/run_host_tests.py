"""Compile every Cube host suite independently; retain logs and JUnit."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--no-sanitize', action='store_true', help='For hosts without ASan/UBSan; CI always sanitizes')
    parser.add_argument('--output', type=Path, default=Path('host-test-results'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suites = sorted((root / 'tests').glob('test_*.c'))
    report = ET.Element('testsuite', name='Cube host C tests', tests=str(len(suites)))
    failures = 0
    for test in suites:
        started = time.monotonic()
        case = ET.SubElement(report, 'testcase', name=test.stem)
        log = []
        try:
            includes = [root / 'src']
            if test.stem == 'test_r2_core':
                sources = sorted((root / 'src/r2_shared').rglob('*.c'))
            elif test.stem == 'test_adc':
                sources = [root / 'src/pdm_adc.c']
            elif test.stem == 'test_board_profile':
                sources = [root / 'src/pdm_board_profile.c']
            elif test.stem == 'test_start_admission':
                sources = [root / 'src/pdm_start_admission.c']
            elif test.stem == 'test_adc_hal':
                sources = [root / 'src/pdm_adc.c', root / 'src/pdm_adc_hal.c']
                includes.insert(0, root / 'tests/adc_mocks')
            elif test.stem.startswith('test_output_safety'):
                sources = [root / 'src/pdm_output_safety.c']
            elif test.stem.startswith('test_link'):
                sources = [root / 'src/pdm_link.c', root / 'src/r2_shared/vm/vm.c',
                           root / 'src/r2_shared/channel_engine/channel.c']
            elif test.stem == 'test_main_outputs':
                sources = [root / 'src/pdm_link.c', root / 'src/pdm_output_safety.c', root / 'src/pdm_adc.c', root / 'src/pdm_board_profile.c']
                sources += sorted((root / 'src/r2_shared').rglob('*.c'))
                includes.insert(0, root / 'tests/main_mocks')
            elif test.stem.startswith('test_can'):
                sources = [root / 'src/pdm_can.c']
                sources += sorted((root / 'tests/can_mocks').glob('*.c'))
                includes.insert(0, root / 'tests/can_mocks')
            else:
                raise ValueError(f'Unregistered suite: {test.name}; add its link inputs')
            executable = output / (test.stem + ('.exe' if os.name == 'nt' else ''))
            command = shlex.split(args.cc, posix=os.name != 'nt')
            command += ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g']
            if not args.no_sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
            command += [f'-I{path}' for path in includes]
            command += [str(test), *map(str, sources), '-o', str(executable)]
            environment = os.environ.copy()
            environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
            environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
            for invocation in (command, [str(executable)]):
                log.append(shlex.join(invocation))
                result = subprocess.run(invocation, capture_output=True, text=True, env=environment, timeout=120)
                log += [result.stdout, result.stderr]
                if result.returncode:
                    raise RuntimeError(f'Command exited {result.returncode}')
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
            failures += 1
            log.append(str(exc))
            ET.SubElement(case, 'failure', message=str(exc)).text = '\n'.join(log)
        case.set('time', f'{time.monotonic() - started:.3f}')
        ET.SubElement(case, 'system-out').text = '\n'.join(log)
        (output / f'{test.stem}.txt').write_text('\n'.join(log), encoding='utf-8')
        failed = case.find('failure') is not None
        print(f'{test.name}: {"FAIL" if failed else "PASS"}', flush=True)
        if failed:
            print('\n'.join(log), flush=True)
    if not suites:
        failures += 1
        ET.SubElement(report, 'error', message='No host suites discovered')
    report.set('failures', str(failures))
    ET.ElementTree(report).write(output / 'junit.xml', encoding='utf-8', xml_declaration=True)
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
