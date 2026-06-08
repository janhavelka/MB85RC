# MB85RC Release Checklist

Use this checklist before tagging and publishing a release.

- Update `library.json`, `idf_component.yml`, `CHANGELOG.md`, README release
  wording, and `Doxyfile`.
- Regenerate and check `include/MB85RC/Version.h` with
  `python scripts/generate_version.py` and
  `python scripts/generate_version.py check`.
- Run native tests: `python -m platformio test -e native`.
- Run Arduino ESP32 builds: `python -m platformio run -e esp32s3dev` and
  `python -m platformio run -e esp32s2dev`.
- Run guard scripts:
  `python tools/check_core_timing_guard.py`,
  `python tools/check_cli_contract.py`, and
  `python tools/check_idf_example_contract.py`.
- Run package validation with `python -m platformio pkg pack`; confirm the
  tarball version matches the release, then remove the generated artifact.
- Run `doxygen Doxyfile`; remove generated `docs/doxygen` output unless the
  repository intentionally starts tracking it.
- Review GitHub Actions results, including the configured pure ESP-IDF
  `esp32s2` and `esp32s3` jobs.
- Confirm `git status --short`, `git diff --stat`, and `git diff --check` show
  only intended release changes and no generated artifacts.
- Tag the release only after PR CI is reviewed.
- Keep hardware-validation matrix rows pending unless actual board logs and
  evidence are recorded.
