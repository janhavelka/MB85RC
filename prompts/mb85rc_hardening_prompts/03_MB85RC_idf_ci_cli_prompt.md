# Prompt 03 — MB85RC Pure ESP-IDF CI, Guard Scripts, and IDF CLI Polish

You are continuing on branch:

```text
hardening/mb85rc-industry-readiness
```

This is Prompt 03. Focus on pure ESP-IDF build proof, CI guard coverage, and IDF example honesty/polish.

## Start

```bash
git status --short
git branch --show-current
git pull --ff-only
```

If dirty or pull fails, stop and report.

## Subagents

Spawn:

1. `idf-build-agent`
2. `ci-agent`
3. `idf-cli-agent`
4. `idf-transport-agent`
5. `integration-review-agent`

## Goals

1. Add or fix CI coverage for pure ESP-IDF builds on ESP32-S2 and ESP32-S3.
2. Ensure `tools/check_idf_example_contract.py` is invoked directly in CI.
3. Keep the ESP-IDF example native IDF only; no Arduino dependency.
4. Improve or document IDF CLI blocking behavior.
5. Ensure IDF transport error mapping remains precise.
6. Keep example bus ownership honest: diagnostic example vs production bus manager.

## Pure ESP-IDF build coverage

Add CI job using a suitable ESP-IDF action/container. The exact workflow can follow repository conventions, but it must run equivalent commands:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

Use the actual example path. The audit says it is probably:

```text
examples/espidf_basic
```

Do not silently use the ADS1115 path `examples/esp_idf/basic` unless this repo really uses that path.

Record ESP-IDF version in CI output if practical:

```bash
idf.py --version
```

If CI cannot run IDF builds in this environment, add documented workflow files and report that local validation is pending.

## Guard script CI

Ensure CI runs:

```bash
python tools/check_idf_example_contract.py
```

The guard should fail if the IDF example includes Arduino/Wire symbols or uses Arduino compatibility.

If the guard is too weak, strengthen it carefully:
- scan IDF example source and CMake files;
- disallow `Arduino.h`, `Wire.h`, `String`, `Serial`, Arduino build flags;
- allow ESP-IDF and FreeRTOS only in example/adapters, not core.

## IDF example review

Audit `examples/espidf_basic/main/main.cpp`.

Check:

1. Uses native IDF entrypoint `app_main`.
2. Uses native IDF I2C APIs.
3. Maps `esp_err_t` precisely enough:
   - timeout;
   - invalid arg;
   - fail;
   - NACK if distinguishable;
   - generic bus error where unavoidable.
4. Does not allocate unbounded buffers.
5. Does not hide destructive operations behind vague commands.
6. Labels itself as diagnostic/bring-up if it owns the bus and blocks on console input.
7. Does not imply production shared-bus management unless it actually implements external serialization.

## CLI blocking/tick issue

The audit says `fgets()` can block before `tick()`.

Choose the cleanest bounded fix:

Option A — If simple and robust:
- Move input reading to a separate FreeRTOS task.
- Keep periodic driver `tick()` in a timed loop/task.
- Protect shared driver access with a mutex.

Option B — If Option A is too broad:
- Clearly label the IDF CLI as diagnostic-only.
- Document that console input can block periodic `tick()` and is not a production scheduler template.
- Keep code changes minimal.

Option C — If nonblocking console input is easy in this environment:
- Implement bounded/nonblocking read/polling.
- Ensure tick executes at documented cadence.

Do not over-engineer a production console framework.

## Shared-bus locking

If the IDF example owns the bus/device handles only for diagnostics, document that.

If a mutex already exists or can be added cleanly:
- lock around I2C callback operations;
- do not lock recursively in a way that deadlocks public API calls;
- document transport-level locking boundary.

If not adding locking:
- explicitly state that production systems must serialize access externally.

## Tests/checks

Add or update tests/guards, not necessarily unit tests, for:

1. IDF example has no Arduino dependency.
2. IDF CMake includes only needed include paths, not broad repo-root if avoidable.
3. CI includes IDF S2/S3 build commands.
4. README verification commands match CI.

## Verification commands

Run locally:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Attempt if available:

```bash
idf.py --version
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

If `idf.py` is unavailable, record exactly.

## Report update

Update `docs/MB85RC_HARDENING_FINAL_REPORT.md`:

- mark Phase 03 complete;
- list CI changes;
- state whether local IDF builds ran;
- state whether CI is expected to run them;
- document IDF CLI/task/tick decision;
- document diagnostic vs production status of examples.

## Commit and sync

If checks pass:

```bash
git add .github tools examples README.md docs CMakeLists.txt idf_component.yml
git commit -m "ci: add MB85RC ESP-IDF build coverage"
git push
```

Review `git diff --cached` before committing to ensure no accidental build artifacts or SDK files are included.

## Stop condition

Stop after this prompt and summarize:
- CI coverage added;
- local IDF status;
- IDF CLI changes;
- check results;
- commit hash;
- push result.
