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

Endurance and retention are variant-specific. The local PDFs state
`10^12` writes/byte for the older/lower-speed parts and `10^13` writes/byte for
the T/TA/1MT parts. Use the exact BOM datasheet for final endurance, retention,
voltage, temperature, and package decisions.

## Common Electrical Notes

- `SDA` is open drain and requires an external pull-up.
- `SCL` clocks input data on rising edges and output data on falling edges.
- `WP` high disables writes to the whole array; reads remain enabled.
- `WP` low or open enables writes on documented variants because the pin is
  internally pulled down.
- Address pins must be strapped to `VDD` or `VSS`; documented open pins read as
  low through internal pull-downs.
- Do not change address pins or `WP` during an I2C transaction.

## Memory Model

The MB85RC parts are linear byte-addressable FRAM memories, not register-file
peripherals. There are no software block-protect registers, status registers,
or configuration registers in the supported local datasheets.

FRAM writes are immediate. Do not add EEPROM-style ACK polling or post-write
programming delays. A successful I2C write means the transport accepted the bus
transaction; it is not proof that data persisted if hardware `WP` is asserted.
Critical data paths should verify by readback or use application-level
journaling.

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
| Device ID read | Reserved write address `0xF8`, active device address word, repeated START, reserved read address `0xF9`, three ID bytes. |
| High-speed transfer | HS-capable variants use `START, 0000 1XXX, expected NACK, repeated START, normal memory command`; STOP exits HS state. |
| Sleep entry | HS/Sleep-capable variants use `F8h`, active device address word, repeated START, `86h`, STOP. |
| Sleep wake | Send a START plus active device address word; normal access resumes after the datasheet recovery time. |

The expected NACK on the High-speed master-code byte belongs only inside the
transport's special HS prefix handling. Generic NACKs from normal memory,
Device ID, or Sleep transactions remain transport failures.

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
