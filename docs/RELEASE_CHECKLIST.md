# MB85RC Release Checklist

Use this checklist before tagging and publishing a release.

- Update `library.json`, `idf_component.yml`, `CHANGELOG.md`, README release
  wording, `SECURITY.md` supported versions, and `Doxyfile`.
- Regenerate and check `include/MB85RC/Version.h` with
  `python scripts/generate_version.py` and
  `python scripts/generate_version.py check`.
- Check package/release metadata consistency with
  `python tools/check_metadata_consistency.py`.
- Run native tests: `python -m platformio test -e native`.
- Run the stable Arduino reference build:
  `python -m platformio run -e esp32s3dev_pinned` (PlatformIO 6.1.18 in CI,
  pioarduino Espressif platform 54.03.20), then the broader compatibility
  builds `python -m platformio run -e esp32s3dev` and
  `python -m platformio run -e esp32s2dev`.
- Run guard scripts:
  `python tools/hil_runner.py --parser-self-test`,
  `python tools/hil_runner.py --dry-run --port COM27 --baud 115200 --timeout-s 5 --strict --require-variant MB85RC256V --require-product-id 0x510 --require-capacity 32768 --soak-duration-s 28800`,
  `python tools/check_core_timing_guard.py`,
  `python tools/check_cli_contract.py`, and
  `python tools/check_idf_example_contract.py`.
- Run package validation with `python -m platformio pkg pack`; confirm the
  tarball version matches the release, then remove the generated artifact.
- Run `doxygen Doxyfile`. The strict configuration must complete with no
  undocumented-public-API, parameter/return, or documentation warnings. Remove
  generated `docs/doxygen` output unless the repository intentionally starts
  tracking it.
- Check maintained Markdown links and ensure `README.md`, `docs/README.md`,
  `CONTRIBUTING.md`, and `SECURITY.md` describe the same supported release and
  validation commands.
- Review GitHub Actions results, including the configured pure ESP-IDF
  `esp32s2` and `esp32s3` jobs.
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
  heap thresholds. The diagnostic examples default to a maximum heap drop of
  1024 bytes and a minimum free heap of 8192 bytes; record the rationale for
  board-specific changes.
