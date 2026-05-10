# Document Inventory

These compact notes cover the MB85RC I2C FeRAM variants present in this repository. They summarize device behavior from PDFs in `docs/` and raw Markdown in `docs/pdf-extracted-md`; raw extraction dumps are not repeated here.

| Source PDF | Raw extract | Pages used | Notes |
|---|---|---:|---|
| `docs/MB85RC04V-DS5v1-E.pdf` | `docs/pdf-extracted-md/MB85RC04V-DS5v1-E.md` | 1-14 | 4-Kbit device, pinout, addressing, reads/writes. |
| `docs/MB85RC16V-DS11v0-E.pdf` | `docs/pdf-extracted-md/MB85RC16V-DS11v0-E.md` | 1-15 | 16-Kbit device, pinout, addressing, reads/writes. |
| `docs/MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf` | `docs/pdf-extracted-md/MB85RC256V-Data-Sheet-DS501-00017-11v2-E.md` | 1-39 | 256-Kbit device, main repository target. |
| `docs/MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.pdf` | `docs/pdf-extracted-md/MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.md` | 1-2 | Short feature cross-check for MB85RC256V. |
| `docs/MB85RC64TA-DS5v1-E.pdf` | `docs/pdf-extracted-md/MB85RC64TA-DS5v1-E.md` | 1-21 | 64-Kbit, high-speed capable. |
| `docs/MB85RC512T-DS6v1-E.pdf` | `docs/pdf-extracted-md/MB85RC512T-DS6v1-E.md` | 1-21 | 512-Kbit, high-speed capable. |
| `docs/MB85RC1MT-DS5v1-E.pdf` | `docs/pdf-extracted-md/MB85RC1MT-DS5v1-E.md` | 1-21 | 1-Mbit, high-speed capable. |

Compact note set:

| File | Purpose |
|---|---|
| `01_chip_overview.md` | Family capabilities and variant matrix. |
| `02_pinout_and_signals.md` | 8-pin I2C signal and write-protect notes. |
| `03_electrical_and_timing.md` | Voltage, bus speed, endurance, retention, and timing. |
| `04_protocol_commands_and_transactions.md` | I2C memory read/write transaction patterns. |
| `05_register_map.md` | Address space and addressing-width notes. |
| `06_modes_interrupts_status_and_faults.md` | WP behavior, high-speed mode, ACK/fault handling. |
| `07_initialization_reset_and_operational_notes.md` | Bring-up and operational guidance. |
| `08_variant_differences_and_open_questions.md` | Differences and unresolved repository policy choices. |
