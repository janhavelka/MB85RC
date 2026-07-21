# MB85RC Documentation

This directory contains maintained project documentation and vendor reference
PDFs. Historical audit reports and generated PDF-to-Markdown dumps are not kept
as source documentation; their durable conclusions are folded into the README,
the public API comments, and the files listed here.

## Maintained Docs

| File | Purpose |
| --- | --- |
| [DEVICE_REFERENCE.md](DEVICE_REFERENCE.md) | MB85RC-family variant, addressing, protocol, timing, WP, Device ID, High-speed, and Sleep notes used by the driver. |
| [IDF_PORT.md](IDF_PORT.md) | Native ESP-IDF example boundary, command contract, and validation notes. |
| [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) | Release verification checklist before tagging or publishing. |
| [reports/HIL_SUMMARY.md](reports/HIL_SUMMARY.md) | Historical HIL ledger with explicit fixture, revision, result, and evidence-grade limits. |

Project-level maintained documents are [README.md](../README.md),
[CHANGELOG.md](../CHANGELOG.md), [CONTRIBUTING.md](../CONTRIBUTING.md), and
[SECURITY.md](../SECURITY.md). Public API documentation is generated from the
headers under `include/MB85RC/` with [Doxyfile](../Doxyfile).

## Vendor References

Vendor PDFs live under [reference-pdfs/](reference-pdfs/). They are retained as
source evidence for device behavior and variant differences.

| PDF | Notes |
| --- | --- |
| `MB85RC04V-DS5v1-E.pdf` | 4-Kbit variant, one-byte memory address with A8 in the I2C address word. |
| `MB85RC16V-DS11v0-E.pdf` | 16-Kbit no-Device-ID variant with A10:A8 in the I2C address word. |
| `MB85RC64TA-DS5v1-E.pdf` | 64-Kbit High-speed/Sleep-capable variant. |
| `MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf` | 256-Kbit two-byte-address variant and original library target. |
| `MB85RC512T-DS6v1-E.pdf` | 512-Kbit High-speed/Sleep-capable variant. |
| `MB85RC1MT-DS5v1-E.pdf` | 1-Mbit variant with A16 in the I2C address word. |
| `MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.pdf` | Short MB85RC256V feature cross-check. |
