# Prompt 05 — MB85RC Final Verification, Comprehensive Report, and Merge Readiness

You are continuing on branch:

```text
hardening/mb85rc-industry-readiness
```

This is the final prompt in this chunked hardening sequence. Do not add new features unless they fix a clear final-review defect. Focus on verification, cleanup, final report, commit, and sync.

## Start

```bash
git status --short
git branch --show-current
git pull --ff-only
```

If dirty with unexpected files, inspect and report before proceeding.

## Subagents

Spawn:

1. `final-code-review-agent`
2. `tests-ci-review-agent`
3. `docs-accuracy-agent`
4. `release-readiness-agent`

Each must check for overclaims, missing tests, accidental artifacts, and repo consistency.

## Final review checklist

### Code/API

Confirm:

- core remains framework-neutral;
- no accidental Arduino/ESP-IDF dependencies in `include/` or `src/`;
- I2C remains injected and non-owning;
- copy/move is disabled;
- status/error mappings are not degraded;
- simple APIs remain compatible where intended;
- extended write/verify APIs are documented and tested;
- no hidden post-write delays or EEPROM ACK polling were added;
- Device-ID behavior remains variant-aware;
- current-address cache/state behavior is conservative after failures;
- no build artifacts are staged.

### Tests

Confirm native tests cover:

- copy/move deletion;
- boundary/overflow;
- partial multi-chunk write/fill;
- accepted-prefix reporting;
- failure on first/middle/last chunk;
- WP-high ACK/no-persistence simulation;
- writeVerify success/failure;
- Device-ID success/failure where supported;
- current-address invalidation after failure;
- no framework leakage guard.

### CI/build

Confirm workflows include:

- native tests;
- Arduino ESP32-S2 build;
- Arduino ESP32-S3 build;
- core guard;
- CLI contract guard;
- IDF example contract guard;
- package validation;
- pure ESP-IDF S2/S3 build, or clearly documented pending status.

### Docs

Confirm README/docs clearly state:

- non-atomic write/fill behavior;
- accepted prefix vs verified persistence;
- WP caveat;
- no EEPROM-style write delay;
- current-address caveat;
- thread/ISR/reentrancy contract;
- diagnostic vs production examples;
- hardware validation not yet done unless actually run.

## Full verification commands

Run all available local commands:

```bash
git status --short
python --version
python -m platformio --version
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio pkg pack
```

Remove generated package artifacts unless the repo intentionally tracks them.

Attempt if available:

```bash
idf.py --version
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

If unavailable, record exactly.

If CI is available to the agent, inspect workflow syntax. Do not claim CI passed unless it actually did.

## Final comprehensive report

Complete:

```text
docs/MB85RC_HARDENING_FINAL_REPORT.md
```

The report must include:

1. Branch name.
2. Starting audit findings.
3. Phase-by-phase summary.
4. Public API changes.
5. Core changes.
6. ESP-IDF/CI changes.
7. Test additions.
8. Documentation changes.
9. Exact local command results.
10. Exact commands not run and why.
11. Hardware validation matrix status.
12. Remaining risks.
13. Future work:
    - hardware validation by variant/address pins;
    - WP pin real-board validation;
    - pure ESP-IDF validation if not locally run;
    - optional high-speed/sleep support only if desired;
    - production journaling example if needed.
14. Merge readiness verdict.
15. Release wording recommendation.

## Merge readiness wording

Use conservative language.

Acceptable if all software checks pass:

```text
This branch is ready to merge as a production-readiness hardening step. It should not yet be described as fully field-proven across all MB85RC variants until hardware validation is completed on representative parts and boards.
```

Avoid:

```text
Fully industry-grade and hardware-proven.
```

unless actual hardware/fault validation supports it.

## Final commit and sync

If the final report changes after verification:

```bash
git add docs/MB85RC_HARDENING_FINAL_REPORT.md README.md include src test tools examples .github CMakeLists.txt idf_component.yml
git commit -m "docs: finalize MB85RC hardening report"
git push
```

If there are no changes, do not make an empty commit unless the repo convention requires it.

Then print:

```bash
git status --short
git log --oneline -5
```

## Stop condition

Stop and report:

- whether branch is merge-ready;
- latest commit hash;
- pushed/synced status;
- tests/checks passed;
- checks not run;
- remaining hardware validation;
- any reason not to merge.
