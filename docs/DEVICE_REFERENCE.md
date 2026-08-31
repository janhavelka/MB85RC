# MB85RC Device Reference

Maintained device-behavior reference for the supported MB85RC-family driver
surface, summarizing the vendor PDFs kept in `docs/reference-pdfs/`.

## Variant Matrix

| Variant | Capacity | Last address | Address model | Device ID | Bus speed | Supply | Notes |
| --- | ---: | ---: | --- | --- | --- | --- | --- |
| MB85RC04V | 512 B | `0x01FF` | One address byte; A8 is encoded in the I2C address word with A2:A1 as device-select bits. | Manufacturer `0x00A`, Product `0x010` | 400 kHz; 1 MHz only at VDD 4.5-5.5 V | 3.0 V to 5.5 V | Two device pins (A2, A1); each part occupies two consecutive 7-bit addresses. No High-speed or Sleep support. |
| MB85RC16V | 2 KiB | `0x07FF` | One address byte; A10:A8 are encoded in the I2C address word. | None in local datasheet | 400 kHz; 1 MHz only at VDD 4.5-5.5 V | 3.0 V to 5.5 V | No device pins: one part occupies all of `0x50`-`0x57`, so only one can share a bus. Must be selected explicitly; `AUTO` cannot discover it. |
| MB85RC64TA | 8 KiB | `0x1FFF` | Two address bytes; A2:A1:A0 select the device. | Manufacturer `0x00A`, Product `0x358` | 1 MHz normal, 3.4 MHz HS | 1.8 V to 3.6 V | High-speed and Sleep documented. |
| MB85RC256V | 32 KiB | `0x7FFF` | Two address bytes; A2:A1:A0 select the device. High address MSB must be 0. | Manufacturer `0x00A`, Product `0x510` | 1 MHz | 2.7 V to 5.5 V | Original target part. |
| MB85RC512T | 64 KiB | `0xFFFF` | Two address bytes; A2:A1:A0 select the device. | Manufacturer `0x00A`, Product `0x658` | 1 MHz normal, 3.4 MHz HS | 1.7 V to 3.6 V | High-speed and Sleep documented. |
| MB85RC1MT | 128 KiB | `0x1FFFF` | Two address bytes; A16 is encoded in the I2C address word with A2:A1 as device-select bits. | Manufacturer `0x00A`, Product `0x758` | 1 MHz normal, 3.4 MHz HS | 1.8 V to 3.6 V | High-speed and Sleep documented. |

Endurance, retention, and operating temperature are variant-specific. The local
vendor PDFs give these page-1 endurance and retention summaries:

- `MB85RC04V` and `MB85RC256V`: `10^12` writes/byte; 10 years at +85 degC,
  95 years at +55 degC, and over 200 years at +35 degC.
- `MB85RC16V`: `10^12` writes/byte with the same family retention statement in
  the local datasheet.
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
- Device-select pin counts differ: `MB85RC64TA`, `MB85RC256V`, and `MB85RC512T`
  have three (A2:A0); `MB85RC04V` and `MB85RC1MT` have two (A2, A1) because the
  lowest slave-address bit carries memory address A8 or A16; `MB85RC16V` has
  none, because its lowest three slave-address bits carry memory address
  A10:A8.
- Devices per bus: `MB85RC64TA`, `MB85RC256V`, and `MB85RC512T` allow eight;
  `MB85RC04V` and `MB85RC1MT` allow four, each consuming two consecutive 7-bit
  addresses; `MB85RC16V` allows exactly one, because a single part answers to
  all eight addresses `0x50`-`0x57`.
- Driver configuration uses the board strap base address, not a memory-bank
  encoded transaction address. `MB85RC64TA`, `MB85RC256V`, and `MB85RC512T`
  accept `0x50`-`0x57`; `MB85RC04V` and `MB85RC1MT` accept only even bases
  `0x50`, `0x52`, `0x54`, and `0x56`; `MB85RC16V` accepts only `0x50`.
- Do not change address pins or `WP` during an I2C transaction.

## Electrical And AC Timing

The bus-speed values in the variant matrix are the rates guaranteed across each
variant's full documented supply range, not a complete board timing proof.
`MB85RC04V` and `MB85RC16V` specify Fast Mode Plus (1 MHz) only for VDD 4.5 V to
5.5 V; below 4.5 V their guaranteed ceiling is Fast Mode (400 kHz). The other
four variants specify 1 MHz over their whole supply range. `variantInfo()` and
`maxNormalBusHz()` therefore report 400 kHz for `MB85RC04V` and `MB85RC16V`;
raise the controller clock only after confirming the board's supply voltage
against the BOM datasheet. Production boards must also satisfy the exact
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
physical device rolls over to address 0 at the end of its array, including
across the address bits carried in the slave byte. A burst crossing a 256-byte
boundary therefore needs no new slave address. The public driver API
intentionally rejects cross-capacity ranges unless an explicit wrap API is
added and tested.

For variants that carry memory address bits in the slave byte (`MB85RC04V` A8,
`MB85RC16V` A10:A8, `MB85RC1MT` A16), two datasheet rules constrain the driver:

- Random read must send the same upper address bits in both the write-phase and
  the read-phase device address words.
- Current-address read composes its target as `n + 1`, where `n` combines the
  upper address bits taken from the *slave byte of that transaction* with the
  low address bits held in the device's internal buffer. The upper bits must
  therefore describe the last accessed byte, not the byte about to be read.

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
local datasheets and requires `tREC` before normal access. The driver
records this state contract but does not insert a hidden delay.

## Datasheet Operational Checklist

- Select the exact variant profile for density, supply range, address-pin
  layout, memory-address format, supported maximum bus speed, and HS/Sleep
  capability.
- Configure the I2C target address from the board strap base and the active
  variant's address table. Let the driver derive per-transaction addresses for
  variants that encode upper memory bits in the I2C address.
- Keep `WP` low/open for write tests, or treat write success as transport
  acceptance only when `WP` may be high externally.
- Use random explicit-address reads for public production reads instead of
  relying on current-address state.
- Keep bus-level retries, clock switching, Sleep wake delay, and bus recovery
  in the application-owned transport or bus manager.
- Route the reserved Device ID transaction through
  `I2cSpecialOp::READ_DEVICE_ID`; do not make `0x7C` a normal scanned device
  address.
- Datasheet bus recovery/software reset, when needed, is transport-owned. The
  local datasheets describe nine repetitions of `START` plus one data `1` before
  read/write retry, and warn not to force SDA high while a slave may be holding
  it low.

## Driver Policy Derived From The Datasheets

- The core owns no I2C controller, pins, bus locks, retries, clock changes, or
  framework time sources.
- Each injected callback is one terminal physical attempt with complete-length
  reporting. A failed memory write is indeterminate unless the transport can
  prove that no requested data was accepted.
- `Config::expectedVariant = DeviceVariant::AUTO` works only for variants with
  a Device ID command. Fixed-BOM products should set the exact expected variant.
- `MB85RC16V` has no Device ID command in the local datasheet set and must be
  selected explicitly.
- High-speed and Sleep support are variant-gated and require the optional
  special transport callback. The application still owns the actual bus clock
  and wake-delay policy.
- Public bulk APIs and staged transfer jobs are bounded by active capacity and
  do not rely on datasheet rollover.
