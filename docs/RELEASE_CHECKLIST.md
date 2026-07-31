# MB85RC Release Checklist

Use this checklist before tagging and publishing a release.

- Set the release version with `python scripts/generate_version.py set X.Y.Z`;
  this synchronizes `library.json`, `idf_component.yml`, README library version,
  Doxygen version/brief, and generated `Version.h`. Then update `CHANGELOG.md`,
  the README published-tag wording, and `SECURITY.md` supported versions.
- Regenerate and check `include/MB85RC/Version.h` with
  `python scripts/generate_version.py` and
  `python scripts/generate_version.py check`.
- Check package/release metadata consistency with
  `python tools/check_metadata_consistency.py`.
- Run native tests: `python -m platformio test -e native`.
- Run the exact Arduino reference builds with PlatformIO 6.1.19 and pioarduino
  Espressif platform 55.03.311:
  `python -m platformio run -e esp32s3dev` and
  `python -m platformio run -e esp32s2dev`, then build the previous 54.03.20
  stack with `python -m platformio run -e esp32s3dev_legacy_54`.
- Run guard scripts: `python tools/hil_runner.py --parser-self-test`,
  `python tools/check_core_timing_guard.py`,
  `python tools/check_cli_contract.py`, and
  `python tools/check_idf_example_contract.py`.
- Preview the reference MB85RC256V fixture plan without opening hardware:
  `python tools/hil_runner.py --dry-run --port COM4 --profile arduino --include-stress --sample-count 500 --soak-duration-s 28800`.
- Run the actual strict reference-fixture HIL (this command deliberately omits
  `--dry-run`):
  `python tools/hil_runner.py --port COM4 --baud 115200 --timeout-s 5 --profile arduino --include-stress --sample-count 500 --strict --require-arduino-version 3.3.11 --require-idf-version v5.5.5 --require-variant MB85RC256V --require-product-id 0x510 --require-capacity 32768 --require-timeout-ms 5 --require-max-write-data 124 --require-max-read-data 124 --heap-max-drop-bytes 1024 --heap-min-free-bytes 8192 --soak-duration-s 28800 --soak-pacing-s 0.1 --soak-max-consecutive-failures 3`.
  Adapt the port, identity, capacity, and documented heap thresholds for another
  fixture; never relax a gate silently.
- Run package validation:
  `python -m platformio pkg pack --output MB85RC.tar.gz`, then
  `python tools/check_package_contents.py MB85RC.tar.gz`. Remove the generated
  artifact after inspection.
- Run `doxygen Doxyfile`. The strict configuration must complete with no
  undocumented-public-API, parameter/return, or documentation warnings. Remove
  generated `docs/doxygen` output unless the repository intentionally starts
  tracking it.
- Check maintained Markdown links and ensure `README.md`, `docs/README.md`,
  `CONTRIBUTING.md`, and `SECURITY.md` describe the same supported release and
  validation commands.
- Review GitHub Actions results, including pure ESP-IDF 6.0.1-floor and 6.0.2
  compatibility builds for `esp32s2` and `esp32s3`.
- Confirm public docs and tests cover all operation classes: one-callback
  steady state; request-qualified multi-step polling with owner deadline,
  cancel/timeout, partial/indeterminate effects, and exactly-once result; and
  explicitly budgeted rare/maintenance whole-range work.
- Confirm `git status --short`, `git diff --stat`, and `git diff --check` show
  only intended release changes and no generated artifacts.
- Tag the release only after PR CI is reviewed.
- Treat the exact production BOM, immutable downstream dependency pin, target
  firmware build, and hardware qualification as external release gates; never
  infer them from native tests or generic-board examples.
- Keep hardware-validation matrix rows pending unless actual board logs and
  evidence are recorded. Production HIL evidence must use strict mode, the
  required variant/product/capacity gates, zero FAIL, zero UNKNOWN, final READY
  health, zero total failures, zero target resets/reconnects, and documented
  heap thresholds. The reference production gates use a maximum heap drop of
  1024 bytes and a minimum free heap of 8192 bytes; record the rationale for
  board-specific changes.
