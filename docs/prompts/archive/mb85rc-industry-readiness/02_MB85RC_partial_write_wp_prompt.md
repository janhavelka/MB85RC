# Prompt 02 — MB85RC Partial Multi-Chunk Writes, Fill Semantics, and WP Persistence

You are continuing on branch:

```text
hardening/mb85rc-industry-readiness
```

This is Prompt 02. Focus on the biggest production-readiness issue: multi-chunk writes/fills are not atomic, and ACK does not prove persistence when WP is high.

## Start

```bash
git status --short
git branch --show-current
git pull --ff-only
```

If dirty or pull fails, stop and report.

## Subagents

Spawn:

1. `fram-partial-write-agent`
2. `wp-persistence-agent`
3. `api-design-agent`
4. `tests-fault-agent`
5. `docs-review-agent`
6. `integration-review-agent`

Each subagent must inspect code/tests/docs before implementation.

## Core facts to preserve

FRAM is not EEPROM:
- do **not** add post-write delays;
- do **not** add ACK polling for write completion;
- writes are accepted at bus speed if WP permits memory update.

But:
- a large logical write may be split into multiple I2C chunks;
- a later chunk can fail after earlier chunks have already reached hardware;
- the public caller currently gets failure but not a precise committed/accepted extent;
- if WP is high, the device can ACK the write transaction while memory content does not change, so “bus success” is not the same as “data persisted.”

## Design rule: distinguish accepted, committed, and verified

Be precise in names and docs.

Recommended terms:

- `bytesAttempted`: bytes the caller asked to write/fill.
- `bytesAccepted`: bytes sent in chunks that returned `Status::OK` from I2C transport.
- `bytesVerified`: bytes later confirmed by readback/verify.
- Avoid claiming `bytesCommitted` unless the implementation can truly prove memory changed. With real hardware and WP pin, bus ACK alone may not prove persistence.

If the existing report used “committed extent,” refine it to “accepted prefix” unless verified.

## Required API work

Add optional extended-result APIs without breaking existing simple APIs.

Recommended additions:

```cpp
struct WriteResult {
    Status status;
    uint32_t address;
    size_t bytesRequested;
    size_t bytesAccepted;
    size_t failedChunkOffset;
    size_t failedChunkLength;
    bool complete;
};

Status write(...);          // existing simple API preserved
WriteResult writeDetailed(...);   // or writeWithResult/writeReport, choose repo style

Status fill(...);           // existing simple API preserved
WriteResult fillDetailed(...);

struct VerifyResult {
    Status status;
    uint32_t address;
    size_t bytesRequested;
    size_t bytesVerified;
    size_t firstMismatchOffset;
    uint8_t expected;
    uint8_t actual;
    bool match;
};

VerifyResult verifyDetailed(...);
Status writeVerify(...);    // convenience: write then verify for critical data
Status fillVerify(...);     // optional if clean
```

Use repository naming conventions. Do not over-design. If a smaller shape fits better, use it, but it must answer:
- how many bytes were requested;
- how many bytes were accepted by successful I2C chunks;
- where the first failed chunk started;
- whether the full operation completed;
- where verify first mismatched.

Existing APIs should remain easy:
- `write()` returns the first failing `Status`;
- `fill()` returns the first failing `Status`;
- documentation says these are non-atomic across chunks.

## Required behavior

### Multi-chunk write/fill

For write/fill split into chunks:

1. If preflight validation fails, no bus traffic occurs and result shows zero accepted.
2. After each successful chunk, increment `bytesAccepted`.
3. If a chunk fails, return immediately with exact failure status and accepted-prefix info.
4. Do not try to roll back. FRAM writes cannot be generally rolled back by a low-level driver.
5. Invalidate current-address cache/state conservatively on failure.
6. Keep existing simple `write()` and `fill()` behavior compatible, but document limitations.

### WP-high behavior

Add fake-transport/backing-store mode where:
- write transactions return OK/ACK;
- backing store does not change.

Use it to test:
- simple `write()` returns OK if transport accepted the transaction;
- `verify()` detects mismatch;
- `writeVerify()` returns a mismatch/verify failure;
- docs warn users that WP is external hardware state and cannot be inferred from ACK.

Do not add a fake “WP detected” status unless the real device exposes such a signal. It likely does not.

### Critical-data recommendation

Add docs/examples recommending:
- `writeVerify()` for provisioning/configuration writes;
- application-level journaling for power-loss-safe logs/config;
- checksum/CRC and generation counters at application layer;
- do not assume multi-chunk write is transactionally atomic.

## Tests required

Native fake tests must cover:

1. Single-chunk successful writeDetailed result.
2. Multi-chunk successful writeDetailed result.
3. Failure on first chunk: zero accepted.
4. Failure on middle chunk: prefix accepted, suffix unchanged in fake backing store.
5. Failure on last chunk: prefix accepted, failure offset exact.
6. fillDetailed same failure-position coverage.
7. Preflight out-of-range: no bus traffic.
8. Address+length overflow: no bus traffic.
9. WP-high simulation: write OK but verify mismatch.
10. writeVerify success when WP disabled.
11. writeVerify failure when WP high / backing store unchanged.
12. Current-address invalidation after failed multi-chunk operation.

Use deterministic fake backing store content so prefix/suffix assertions are exact.

## Documentation required

Update README and public headers:

- `write()` and `fill()` are not atomic across chunks.
- The result of simple `write()` means I2C transaction success, not necessarily persistence if WP is asserted.
- `writeDetailed()` / equivalent reports accepted prefix, not guaranteed persistence.
- `verify()` / `writeVerify()` are required for persistence confidence.
- No EEPROM-style write delay is used or required for supported FRAM parts.
- Power-loss safety must be implemented by the application using journaling/checksums if needed.

Add a small “production storage pattern” section:
- write record header with magic/version/length/sequence/CRC;
- write payload;
- verify;
- mark record valid last;
- scan latest valid record on boot.

Keep it concise; do not build a full filesystem.

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
python -m platformio pkg pack
```

Remove generated package artifacts if not intended to be tracked.

## Report update

Update `docs/MB85RC_HARDENING_FINAL_REPORT.md`:

- mark Phase 02 complete;
- document exact API additions;
- explain accepted-prefix vs persistence/verification distinction;
- list tests added;
- list commands and results;
- list remaining hardware WP validation needed.

## Commit and sync

If checks pass:

```bash
git add include src test README.md docs tools
git commit -m "feat: report MB85RC partial write results"
git push
```

## Stop condition

Stop after this prompt and summarize:
- APIs added;
- tests added;
- how WP-high is represented;
- exact check results;
- commit hash;
- push result.
