# Prompt 00 — MB85RC Hardening Kickoff, Branch, AGENTS.md, and Work Plan

You are working in the MB85RC repository after the IDF-merged industry-readiness audit.

We will harden this repository in **small committed phases**. Do not try to fix everything in one pass. This is Prompt 00 of the sequence.

## Overall approach

The user will send follow-up prompts one by one. Each prompt is a focused chunk. After each prompt:

1. Run the relevant checks.
2. Create/update the phase report section.
3. Commit the completed phase.
4. Push/sync the branch.
5. Stop and summarize exactly what changed, what passed, what failed, and what remains.

Use subagents where available. Keep their output factual and code-grounded.

## Start branch

Start from a clean worktree.

```bash
git status --short
git branch --show-current
```

If the worktree is dirty, stop and report the dirty files. Do not overwrite user work.

Create or switch to the hardening branch:

```bash
git checkout -b hardening/mb85rc-industry-readiness
```

If the branch already exists:

```bash
git checkout hardening/mb85rc-industry-readiness
```

Then sync safely:

```bash
git pull --ff-only
```

If fast-forward pull is not possible, stop and report.

## Read before editing

Read at minimum:

```text
docs/MB85RC_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md
README.md
AGENTS.md
include/MB85RC/
src/
test/
examples/01_basic_bringup_cli/
examples/espidf_basic/
tools/
.github/workflows/
platformio.ini
library.json
CMakeLists.txt
idf_component.yml
```

If the audit report has a different name, locate and use the existing industry-readiness audit report.

## Subagents to spawn

Spawn focused subagents and ask them for file/line-grounded findings only:

### 1. Core contracts subagent

Audit:
- framework neutrality in `include/` and `src/`;
- injected/non-owning I2C transport;
- copy/move semantics;
- thread/ISR/reentrancy contract;
- public API status/error model;
- current-address cache semantics after failure;
- zero-length and boundary behavior;
- range checking and address overflow checks.

### 2. FRAM semantics subagent

Audit:
- RAMXEED/Fujitsu MB85 device-family docs in `docs/`;
- supported variants and addressing models;
- Device-ID support by variant;
- no EEPROM-style write delay;
- WP behavior;
- sequential read/write wraparound;
- current-address read limitations;
- high-speed/sleep features, if mentioned but not implemented.

### 3. Tests/fault-injection subagent

Audit:
- fake backing-store behavior;
- partial multi-chunk failure coverage;
- WP-high ACK/no-persistence simulation;
- current-address invalidation after failed transactions;
- Device-ID positive/negative coverage;
- copy/move compile-time coverage.

### 4. IDF/CI subagent

Audit:
- pure ESP-IDF component metadata;
- `examples/espidf_basic`;
- `idf.py` build coverage in CI;
- guard script invocation;
- IDF error mapping;
- IDF example task/tick/input blocking pattern;
- shared-bus locking honesty.

### 5. Docs/examples subagent

Audit:
- README/API docs;
- Doxygen/public header contracts;
- example labels: diagnostic/bring-up/production;
- hardware validation matrix;
- production storage/journaling guidance.

## Important interpretation of the audit

The audit says the foundation is strong:
- framework-neutral core,
- injected I2C callbacks,
- variant-aware address encoding,
- range checks,
- chunked read/write,
- current-address semantics,
- no EEPROM-style write delay,
- native tests already passing.

Do **not** perform a broad rewrite.

The main hardening targets are:
1. Pure ESP-IDF build coverage.
2. Explicit partial multi-chunk write/fill semantics.
3. WP-high persistence honesty and write-verify convenience.
4. Copy/move deletion.
5. Public-header thread/ISR/reentrancy contract.
6. IDF CLI/input/task polish.
7. Hardware validation documentation by FRAM variant/address pins.

## Update AGENTS.md

Update or create `AGENTS.md` with MB85RC-specific rules.

Include these rules:

```markdown
# MB85RC hardening rules

- Core code under `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, global buses, hidden delays, or heap-heavy framework types.
- The MB85RC core must not own the I2C bus. Bus ownership, locking, timeouts, retry policy, and recovery policy belong in the injected transport or application bus manager.
- FRAM writes are not EEPROM writes: do not add ACK polling or post-write programming delays unless a specific datasheet variant requires it.
- Multi-chunk write/fill operations are not atomic. Public docs and tests must make prefix-commit behavior explicit.
- I2C ACK on a write does not prove persistence when the hardware WP pin is high. Critical data paths must use verify or application-level journaling.
- Device-ID support is variant-specific. Do not require Device ID for variants that do not support it.
- Current-address read semantics must be conservative after power-up, failed transactions, or address-setting uncertainty.
- Public APIs are not ISR-safe if they can perform I2C or block.
- Instances are not internally thread-safe unless explicitly changed and tested; external serialization is required.
- Do not claim pure ESP-IDF or hardware validation unless the exact commands were run and recorded.
```

## Create phase tracking report

Create or update:

```text
docs/MB85RC_HARDENING_FINAL_REPORT.md
```

For now, add a skeleton with:

```markdown
# MB85RC Industry-Readiness Hardening Final Report

Date: <YYYY-MM-DD>
Branch: `hardening/mb85rc-industry-readiness`

## Phase Status

| Phase | Status | Commit | Notes |
| --- | --- | --- | --- |
| 00 Kickoff / AGENTS / plan | In progress | TBD | ... |
| 01 Core contracts | Pending | TBD | ... |
| 02 Partial write + WP persistence | Pending | TBD | ... |
| 03 ESP-IDF CI and CLI polish | Pending | TBD | ... |
| 04 Docs, examples, hardware validation matrix | Pending | TBD | ... |
| 05 Final verification and release report | Pending | TBD | ... |
```

Do not fill fake results.

## Checks for this phase

Run only safe checks:

```bash
git status --short
python --version
python -m platformio --version
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
```

If a command does not exist, record it exactly.

## Commit and sync

If the phase is complete and checks are acceptable:

```bash
git add AGENTS.md docs/MB85RC_HARDENING_FINAL_REPORT.md
git commit -m "docs: start MB85RC industry hardening plan"
git push -u origin hardening/mb85rc-industry-readiness
```

If push fails, report the failure and do not pretend it synced.

## Stop condition

Stop after this phase. Report:
- branch name;
- files changed;
- subagents used;
- checks run;
- commit hash;
- push/sync result;
- any concerns before Prompt 01.
