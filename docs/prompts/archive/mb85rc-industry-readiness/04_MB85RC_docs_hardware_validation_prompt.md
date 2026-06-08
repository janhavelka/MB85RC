# Prompt 04 — MB85RC Documentation, Hardware Validation Matrix, and Production Storage Guidance

You are continuing on branch:

```text
hardening/mb85rc-industry-readiness
```

This is Prompt 04. Focus on documentation completeness and hardware validation planning. Do not implement new features unless they are documentation-only or tiny examples.

## Start

```bash
git status --short
git branch --show-current
git pull --ff-only
```

If dirty or pull fails, stop and report.

## Subagents

Spawn:

1. `docs-contract-agent`
2. `fram-datasheet-agent`
3. `hardware-validation-agent`
4. `examples-agent`
5. `integration-review-agent`

## Goals

1. Make README and public headers honest enough for production users.
2. Add hardware validation matrix by supported FRAM variant and address pin mode.
3. Document WP, verify, current-address, partial-write, and no-write-delay behavior.
4. Document Device-ID limitations by variant.
5. Add a concise production storage/journaling pattern.
6. Ensure docs separate:
   - implemented behavior;
   - unit-test coverage;
   - CI/build coverage;
   - hardware validation still pending.

## Datasheet/device-family documentation

Use local docs. Do not invent facts.

Verify support/claims for each variant the library exposes:
- capacity;
- addressing model;
- address pins / I2C address range;
- Device-ID support or no-ID limitation;
- high-speed support if claimed;
- sleep support if claimed;
- WP behavior;
- no EEPROM-style write delay;
- data retention/endurance as documented.

If high-speed or sleep commands are not implemented, say so clearly. Do not add them unless a later prompt requests it.

## README sections to add/update

Add or improve sections:

### 1. Production readiness summary

State the library is production-oriented but hardware validation is still variant/board dependent.

### 2. I2C ownership and concurrency

Include:
- core does not own bus;
- external locking required for shared bus/multitask use;
- not ISR-safe;
- callbacks must not recursively call into same instance.

### 3. FRAM write semantics

Include:
- no post-write delay / no EEPROM ACK polling;
- write/fill non-atomic across chunks;
- simple `write()` success means transport success, not verified persistence;
- WP high can prevent memory modification while reads still work;
- use `verify()` / `writeVerify()` for critical data.

### 4. Current address semantics

Include:
- current-address read is an I2C/FRAM device feature;
- do not use as first operation after power-up;
- use explicit-address reads for deterministic production behavior;
- after failures, prefer explicit-address operations.

### 5. Supported variant table

Create or update a table:

| Variant | Capacity | I2C address model | Memory address bytes/model | Device ID | Max bus speed claimed | Notes |
| --- | ---: | --- | --- | --- | --- | --- |

Use only facts verified from local docs/code.

### 6. Hardware validation matrix

Add:

| Scenario | Variant(s) | Address pins | Command/test | Expected evidence | Status |
| --- | --- | --- | --- | --- | --- |

Include at minimum:
- Device-ID variants;
- no-ID variant;
- A0/A1/A2 or A1/A2 address combinations;
- exact-end read/write;
- boundary rejection;
- sequential read/write no wrap unless intentionally tested;
- WP high;
- bulk write/fill/verify;
- current-address read after explicit address set;
- unplug/NACK;
- brownout/power-cycle persistence;
- pure IDF CLI on ESP32-S2/S3;
- shared bus with another device;
- long soak with repeated write/read/verify on a sacrificial range.

### 7. Production storage pattern

Add concise guidance:
- record format with magic/version/sequence/length/CRC;
- write payload;
- verify;
- write commit marker last;
- scan latest valid record on boot;
- use wear/endurance limits from the actual part datasheet.

Do not implement a filesystem.

## Example comments

Update example comments/readmes:

- Arduino CLI: diagnostic/bring-up.
- ESP-IDF CLI: diagnostic/bring-up unless it now has proper production task/locking pattern.
- If a production-style example exists, label exactly what it demonstrates and what it does not.

## Public headers

Add Doxygen notes near APIs:
- `write`;
- `writeDetailed`/equivalent;
- `fill`;
- `verify`;
- `writeVerify`;
- current-address read APIs;
- probe/begin/recover.

## Verification commands

Run:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Also run docs-related checks if present.

## Report update

Update `docs/MB85RC_HARDENING_FINAL_REPORT.md`:

- mark Phase 04 complete;
- summarize documentation changes;
- include hardware validation matrix status;
- list remaining actual hardware tests needed;
- do not claim any hardware tests were run.

## Commit and sync

If checks pass:

```bash
git add README.md docs include examples
git commit -m "docs: document MB85RC production FRAM semantics"
git push
```

## Stop condition

Stop after this prompt and summarize:
- docs changed;
- variant matrix status;
- hardware validation still pending;
- checks;
- commit hash;
- push result.
