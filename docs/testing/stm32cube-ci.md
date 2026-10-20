# STM32Cube validation

The active firmware target is `pdm_cube`, compiled for STM32F446ZE with
STM32Cube HAL and the Racing Dash C components. PlatformIO Core is pinned to
6.1.18 and the STM32 platform to 20.0.0. CI records resolved package versions,
ELF text/data/BSS memory, image hashes and full build output as artifacts.

Run the same host tests locally with:

```sh
python pdm_cube/tests/run_host_tests.py
python -m platformio run -d pdm_cube -e stm32f446_cube
```

The runner discovers `test_*.c` and separately links each suite. It supports
Racing Dash core, output-safety and CAN/mock suites as those branches land.
An unknown suite fails instead of silently escaping CI. Compilation failures,
crashes, sanitizer findings and timeouts fail the job and remain in per-suite
logs and JUnit. ASan and UBSan are mandatory in Linux CI; Windows hosts without
the runtime may use `--cc gcc --no-sanitize`, which is not sanitizer evidence.

Cppcheck runs separately from host execution and real ARM compilation. There
are no automatic hardware tests and no flashing or output actuation in CI.
Published binaries are experimental, with the existing boot-map restriction;
successful checks do not authorize loading them on a PDM.

The old `SynapsePDM/` Arduino source and both historical build profiles remain
as reference. Their STM32SD/STM32duino incompatibility is unresolved; the old
workflow is now explicitly manual because the approved architecture switched
to HAL, not because that compatibility problem was fixed. The automatic
firmware compile gate is the replacement STM32Cube job.

CI retains original legacy safety tests and legacy Cppcheck separately. Do not
interpret their success as validation of the new HAL's physical output pins.
