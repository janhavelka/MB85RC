# MB85RC Device Reference

This file is the maintained device-behavior reference for the supported
MB85RC-family driver surface. It summarizes the vendor PDFs kept in
`docs/reference-pdfs/` and replaces the older extracted Markdown dumps.

## Variant Matrix

| Variant | Capacity | Last address | Address model | Device ID | Bus speed | Supply | Notes |
| --- | ---: | ---: | --- | --- | --- | --- | --- |
| MB85RC04V | 512 B | `0x01FF` | One address byte; A8 is encoded in the I2C address word with A2:A1 as device-select bits. | Manufacturer `0x00A`, Product `0x010` | 1 MHz | 3.0 V to 5.5 V | No local High-speed or Sleep command support. |
| MB85RC16V | 2 KiB | `0x07FF` | One address byte; A10:A8 are encoded in the I2C address word. | None in local datasheet | 1 MHz | 3.0 V to 5.5 V | Must be selected explicitly; `AUTO` cannot discover it. |
| MB85RC64TA | 8 KiB | `0x1FFF` | Two address bytes; A2:A1:A0 select the device. | Manufacturer `0x00A`, Product `0x358` | 1 MHz normal, 3.4 MHz HS | 1.8 V to 3.6 V | High-speed and Sleep documented. |
| MB85RC256V | 32 KiB | `0x7FFF` | Two address bytes; A2:A1:A0 select the device. High address MSB must be 0. | Manufacturer `0x00A`, Product `0x510` | 1 MHz | 2.7 V to 5.5 V | Original target part. |
| MB85RC512T | 64 KiB | `0xFFFF` | Two address bytes; A2:A1:A0 select the device. | Manufacturer `0x00A`, Product `0x658` | 1 MHz normal, 3.4 MHz HS | 1.7 V to 3.6 V | High-speed and Sleep documented. |
| MB85RC1MT | 128 KiB | `0x1FFFF` | Two address bytes; A16 is encoded in the I2C address word with A2:A1 as device-select bits. | Manufacturer `0x00A`, Product `0x758` | 1 MHz normal, 3.4 MHz HS | 1.8 V to 3.6 V | High-speed and Sleep documented. |

Endurance, retention, and operating temperature are variant-specific. The older
extracted notes recorded these page-1 retention summaries from the local PDFs:

- `MB85RC04V` and `MB85RC256V`: `10^12` writes/byte; 10 years at +85 degC,
  95 years at +55 degC, and over 200 years at +35 degC.
- `MB85RC16V`: `10^12` writes/byte with the same family retention statement in
  the local extraction.
- `MB85RC64TA`: `10^13` writes/byte; 19.1 years at +105 degC and 70.4 years at
  +85 degC.
- `MB85RC512T`: `10^13` writes/byte; 10 years at +85 degC and 95 years at
  +55 degC.
- `MB85RC1MT`: `10^13` writes/byte; 10 years at +85 degC, 95 years at +55 degC,
  and over 200 years at +35 degC.

The standard operating ambient temperature range recorded for
`MB85RC04V`, `MB85RC16V`, `MB85RC256V`, `MB85RC512T`, and `MB85RC1MT` is
-40 degC to +85 degC; the `MB85RC64TA` feature summary includes retention data
at +105 degC. Use the exact BOM datasheet for final endurance, retention,
voltage, temperature, and package decisions.

## Common Electrical Notes

- `SDA` is open drain and requires an external pull-up.
- `SCL` clocks input data on rising edges and output data on falling edges.
- `WP` high disables writes to the whole array; reads remain enabled.
- `WP` low or open enables writes on documented variants because the pin is
  internally pulled down.
- Address pins must be strapped to `VDD` or `VSS`; documented open pins read as
  low through internal pull-downs.
- Address-pin availability changes by variant: `MB85RC04V` and `MB85RC1MT`
  support up to four devices on one bus, while `MB85RC64TA`, `MB85RC256V`, and
  `MB85RC512T` support up to eight. `MB85RC16V` uses address bits in the device
  address rather than external device-select pins.
- Do not change address pins or `WP` during an I2C transaction.

## Electrical And AC Timing

The bus-speed values in the variant matrix are maximum device capabilities, not
a complete board timing proof. Production boards must also satisfy the exact
BOM datasheet's AC/DC timing tables: pull-up sizing, bus capacitance, rise and
fall times, setup/hold times, valid voltage range, temperature range, input
thresholds, and High-speed-mode footnotes at the selected `VDD`. The core
driver only reports variant bus limits; it does not validate board electrical
timing.

## Memory Model

The MB85RC parts are linear byte-addressable FRAM memories, not register-file
peripherals. There are no software block-protect registers, status registers,
or configuration registers in the supported local datasheets.

FRAM writes are immediate. Do not add EEPROM-style ACK polling or post-write
programming delays. A successful I2C write means the transport accepted the bus
transaction; it is not proof that data persisted if hardware `WP` is asserted.
Critical data paths should verify by readback or use application-level
journaling.

Out-of-spec power sequencing, brownout, or violating read/write cycle
conditions is an application storage-integrity problem. The datasheets do not
guarantee memory contents when those conditions are violated; production systems
should validate the board power profile and use verify/journaling for critical
records.

Sequential read/write transactions auto-increment the internal address and the
physical device can roll over at the end of its array. The public driver API
intentionally rejects cross-capacity ranges unless an explicit wrap API is
added and tested.

Current-address reads are conservative: the internal pointer is undefined after
power-on and can be disturbed by failed or diagnostic transactions. Use explicit
addressed reads for deterministic production paths.

## I2C Transactions

| Operation | Transaction shape |
| --- | --- |
| Byte/sequential write | START, device address write, memory address bytes, one or more data bytes, STOP. |
| Random read | START, device address write, memory address bytes, repeated START, device address read, data bytes, NACK final byte, STOP. |
| Sequential read | Random-read setup followed by additional ACKed bytes; final byte is NACKed before STOP. |
| Current-address read | START, device address read, data byte(s), NACK final byte, STOP. Use only after a known pointer-setting transaction. |
| Device ID read | Reserved write address `0xF8`, active device address word, repeated START, reserved read address `0xF9`, three ID bytes, NACK final byte, STOP. ACK after byte 3 may repeat the ID stream. |
| High-speed transfer | HS-capable variants use `START, 0000 1XXX, expected NACK, repeated START, normal memory command`; STOP exits HS state. |
| Sleep entry | HS/Sleep-capable variants use `F8h`, active device address word, repeated START, `86h`, STOP. |
| Sleep wake | Send a START plus active device address word; normal access resumes after the datasheet recovery time. |

The expected NACK on the High-speed master-code byte belongs only inside the
transport's special HS prefix handling. Generic NACKs from normal memory,
Device ID, or Sleep transactions remain transport failures.

For Device ID and Sleep command address words, the R/W bit is documented as
don't-care in the local PDFs. For `MB85RC1MT`, A16 is also don't-care in those
command forms; do not over-interpret those bits in custom special transports.

For Sleep-capable variants, wake recovery starts on the ninth wake clock in the
local extracted notes and requires `tREC` before normal access. The driver
records this state contract but does not insert a hidden delay.

## Operational Checklist From The Extracted Notes

- Select the exact variant profile for density, supply range, address-pin
  layout, memory-address format, supported maximum bus speed, and HS/Sleep
  capability.
- Configure the I2C target address from the board strap and the active
  variant's address table.
- Keep `WP` low/open for write tests, or treat write success as transport
  acceptance only when `WP` may be high externally.
- Use random explicit-address reads for public production reads instead of
  relying on current-address state.
- Keep bus-level retries, clock switching, Sleep wake delay, and bus recovery
  in the application-owned transport or bus manager.
- Datasheet bus recovery/software reset, when needed, is transport-owned. The
  extracted notes describe nine repetitions of `START` plus one data `1` before
  read/write retry, and warn not to force SDA high while a slave may be holding
  it low.

## Driver Policy Derived From The Datasheets

- The core owns no I2C controller, pins, bus locks, retries, clock changes, or
  framework time sources.
- `Config::expectedVariant = DeviceVariant::AUTO` works only for variants with
  a Device ID command. Fixed-BOM products should set the exact expected variant.
- `MB85RC16V` has no Device ID command in the local datasheet set and must be
  selected explicitly.
- High-speed and Sleep support are variant-gated and require the optional
  special transport callback. The application still owns the actual bus clock
  and wake-delay policy.
- Public bulk APIs and staged transfer jobs are bounded by active capacity and
  do not rely on datasheet rollover.
