# MB85RC Code Audit Reassessment

Date: 2026-08-30

Branch: `main`

Starting revision: `3b7373e9656c17a2ad76c95f29118d2d4bf89193`

## Audit provenance and method

The original audit is no longer present in the current tree. It was read in
full from Git history as
`docs/MB85RC_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md`; its audited baseline was
`bebad8a0f832b8bf9a4dce63239fe82795d6d819`. The historical completion report,
`docs/MB85RC_HARDENING_FINAL_REPORT.md`, was also read in full at revision
`d43d5fcb7dde0637c6ddaeff3006c675c65142e0`.

This reassessment did not rely on that completion report's conclusions. It
examined the current source, public headers, examples, tests, CI workflows,
documentation, HIL tooling, and the actual history/diff from the audited
baseline. Three independent parallel reviews covered requirements and scope,
implementation and edge cases, and tests/CI/simplicity. Every reported issue
was then reproduced or disproved against the current code, and the final diff
was reviewed again after correction.

## Original findings reassessed

| ID | Original finding | Fresh verdict | Current disposition |
| --- | --- | --- | --- |
| H1 | Native ESP-IDF was not built in CI or during the original audit. | Valid at the audited baseline. | Resolved. CI directly builds `examples/espidf_basic` for ESP32-S2 and ESP32-S3 with ESP-IDF v6.0.1 and v6.0.2. The last CI run on the starting revision passed all four combinations. Keeping this as a direct CI matrix is the simplest proper guard. |
| H2 | Multi-chunk write/fill failure hid the already accepted prefix. | Valid. | Resolved by the existing `WriteResult`, `WriteCommit`, `writeDetailed()`, and `fillDetailed()` contracts and tests. This audit additionally fixed two truthfulness gaps: Sleep/Waking preflight now returns a bus-silent preflight result with `NOT_APPLICABLE`, and failed-write commit claims are trusted only when both TX and RX completion evidence is internally consistent. |
| M1 | An ACKed write can be discarded while hardware WP is asserted. | Valid hardware behavior, not a transport error the driver can detect. | Resolved at the proper abstraction boundary: ACK is documented as transport acceptance rather than persistence, verified-write/readback APIs are available, and WP-high behavior has native tests. Trying to infer the physical WP pin in the core would be incorrect. |
| M2 | Copy/move behavior of a stateful, non-owning driver was implicit. | Valid. | Resolved. Copy construction, copy assignment, move construction, and move assignment are explicitly deleted in the public class. This is simpler and safer than inventing ownership-transfer semantics. |
| M3 | The native ESP-IDF CLI blocks in `fgets()`, so `tick()` is not continuously serviced. | Valid as an example responsiveness observation, not a production-core defect. | Accepted and explicitly documented as a diagnostic-only blocking CLI. The core performs no background I2C, delayed write, retry, or recovery work in `tick()`; the only timed transition is Sleep wake state from caller-supplied time, and the CLI wake handler waits, ticks, and recovers synchronously. Adding a console task/queue would add concurrency and locking complexity without fixing a current correctness problem. Production applications are told to use their own serialized bus owner. |
| L1 | Range current-address read performed one transaction per byte without an exact public cost contract/test. | Valid. | Fully resolved. The implementation deliberately retains one-byte current-address transactions because a bulk read would change the device-pointer and transport contract. README and public Doxygen now state exactly `len` one-byte transport callbacks and recommend addressed `read()` for bulk work; the native test asserts the exact callback count. |
| L2 | An ESP-IDF compatibility check inspected text but did not itself compile the example. | Valid. | Resolved together with H1. The lightweight contract check remains useful, while the CI `esp-idf-build` job supplies the direct compiler/linker proof. |

The original cross-cutting thread/ISR concern is also correctly handled:
callbacks are synchronous, instances are not internally thread-safe, calls
must be serialized, and public I2C APIs are not ISR-safe. No hidden retry,
allocation, bus recovery, or clock ownership was introduced.

## Additional gaps confirmed and fixed

The fresh review found the following defects or inaccuracies in the completed
work:

1. `writeDetailed()` and `fillDetailed()` reached their chunk loop while the
   device was ASLEEP or WAKING. Although no transport callback ran, the result
   misleadingly described a failed chunk and an indeterminate write. Both APIs
   now perform the existing awake preflight after argument/range validation and
   return truthful preflight metadata with zero bus traffic and no health-count
   mutation.
2. Failed memory writes could retain backend `ACCEPTED` or `NOT_COMMITTED`
   claims even when the backend simultaneously reported an impossible nonzero
   RX completion. Commit normalization now requires the expected RX count as
   well as valid TX evidence; malformed evidence becomes `INDETERMINATE`.
3. Successful AUTO re-identification to a different variant, or failed AUTO
   selection/validation, could leave high-speed enablement and cached current
   address state from the previous variant. Variant-dependent state is now
   cleared on identity loss or actual variant change, while a read of the same
   variant preserves valid state.
4. Two address-error paths converted `uint32_t` directly to signed status
   detail, turning `UINT32_MAX` into `-1`. They now use the existing saturating
   detail conversion and consistently report `INT32_MAX`.
5. The native-IDF HIL plan treated High-speed and Sleep `UNSUPPORTED` results
   as failures even for variants that honestly advertise no support. The runner
   now accepts only capability-consistent pairs (`Support: yes` with `OK`, or
   `Support: no` with the exact expected `UNSUPPORTED` line), and the ESP-IDF
   CLI emits that symbolic result instead of an ambiguous generic `FAIL`. The
   runner still rejects contradictory capability output and any additional
   `BUSY`, timeout, unsupported, or other failure line in the same capture.
   Parser regression cases cover honest, contradictory, and mixed-status
   outputs.
6. Non-strict HIL reports labeled the strict gate `PASS` even though the gate
   was not run. JSON, Markdown, and console output now consistently say
   `NOT RUN`.
7. `getConfig()` documentation said it returned a copy although it returns a
   const reference. The wording now describes the cached configuration
   accurately.

All new native regressions are registered in the test runner. The review
confirmed that each production-code test fails when its corresponding fix is
removed, and that declarations and registrations both total 158.

## Scope and simplicity decisions

- High-speed and Sleep support was a later extension beyond the original
  P0/P1 audit work. It is now part of the released v4.1 public API and has
  substantive tests and documentation, so removing it would be an unrelated
  breaking change. This audit fixed only defects exposed in that code.
- The blocking diagnostic CLI was not converted into a multi-task example.
  Its limitation is accurate and bounded; production ownership remains outside
  the library.
- No new retry policy, bus manager, dynamic allocation, WP inference, or
  background scheduler was added. Corrections reuse existing validation and
  status mechanisms.

## Verification results

The final working tree passed:

- `python tools/check_core_timing_guard.py`
- `python tools/check_cli_contract.py`
- `python tools/check_idf_example_contract.py`
- `python tools/check_metadata_consistency.py`
- `python scripts/generate_version.py check`
- `python tools/hil_runner.py --parser-self-test`
- `git diff --check`
- `.\scripts\pio.cmd test -e native`: **158/158 passed**
- `.\scripts\pio.cmd run -e esp32s3dev`: **passed**
- `.\scripts\pio.cmd run -e esp32s2dev`: **passed**
- `.\scripts\pio.cmd run -e esp32s3dev_legacy_54`: **passed**
- `.\scripts\pio.cmd pkg pack` followed by
  `python tools/check_package_contents.py`: **passed**
- `doxygen Doxyfile`: **passed with no warnings**

The initial S2 build was launched while a different Arduino framework version
was being provisioned by another PlatformIO process and failed before project
compilation because the shared framework path was temporarily unresolved. The
required wrapper was rerun alone and the complete S2 compile/link passed; no
manual PlatformIO Core installation or source workaround was used.

Local `idf.py` is not installed, so direct local ESP-IDF compilation was not
claimed. GitHub Actions run
[`31029906298`](https://github.com/janhavelka/MB85RC/actions/runs/31029906298)
passed on the clean starting revision, including native tests, library
validation, documentation, Arduino builds, and the ESP-IDF v6.0.1/v6.0.2 by
ESP32-S2/ESP32-S3 matrix. The audit commit is to receive the same CI matrix
after push.

## Remaining hardware qualification boundary

No new physical HIL run was possible in this workspace. Existing evidence is
useful regression/endurance evidence, but it does not qualify untested
hardware behavior. The following remain explicitly pending and must not be
reported as completed production qualification:

- a clean, immutable release build on the documented production fixture;
- WP-high behavior and write-timeout reconciliation on hardware;
- controlled power interruption and post-restart data validation;
- device removal, NACK/stuck-bus injection, and unplug/replug recovery;
- shared-bus contention with the actual external owner; and
- High-speed/Sleep electrical behavior on a supporting variant.

## Final conclusion

Every original software finding is either resolved by the current code and
direct verification or, for the diagnostic CLI's intentional blocking input,
accurately bounded and documented. All additional reproducible in-scope gaps
found by the independent review were fixed with localized changes and covered
by regression tests. No confirmed software defect from the original audit
scope remains open. Hardware qualification remains pending only where the
necessary physical fixture or fault injection was unavailable.
