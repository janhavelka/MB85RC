# MB85RC256V — FRAM (FeRAM) I2C Memory — Comprehensive Extraction

---

## 1. Source documents and code inputs

| # | Filename | Description | Pages / lines |
|---|----------|-------------|---------------|
| 1 | `datasheet_MB85RC256V.pdf` | Fujitsu Semiconductor fact sheet (summary), document NP501-00019-2v0-E, dated 2013.5 | 2 |
| 2 | `datasheet_MB85RC256V_v2.pdf` | Full datasheet by RAMXEED (formerly Fujitsu Semiconductor Memory Solutions), document DS501-00017-11v2-E | 39 |
| 3 | `MB85_FRAM.h` | Arduino library header for the MB85 family, with supported-part list, API declarations, inline template implementations, constants, license, and changelog | 241 lines |
| 4 | `MB85_FRAM.cpp` | Arduino library implementation for probing devices, computing capacity, and issuing I2C requests | 169 lines |
| 5 | `MB85RC512T-DS6v1-E.pdf` | Official RAMXEED datasheet for MB85RC512T, used to compare 64KB-family protocol, Device ID, High Speed mode, and Sleep mode | 33 |
| 6 | `MB85RC64TA-DS5v1-E.pdf` | Official RAMXEED datasheet for MB85RC64TA, used to compare 8KB wide-voltage protocol, Device ID, High Speed mode, and Sleep mode | 41 |
| 7 | `MB85RC1MT-DS5v1-E.pdf` | Official RAMXEED datasheet for MB85RC1MT, used to compare 1Mbit addressing, A16 handling, Device ID, High Speed mode, and Sleep mode | 33 |
| 8 | `MB85RC16V-DS11v0-E.pdf` | Official RAMXEED datasheet for MB85RC16V, used to confirm the one-address-byte / upper-address-in-device-word variant | 36 |
| 9 | `MB85RC04V-DS5v1-E.pdf` | Official RAMXEED datasheet for MB85RC04V, used to confirm the 9-bit-address / mixed device-address-word variant and Device ID | 31 |
| 10 | `ramxeed_i1_wide_voltage_i2c_family.html` | Official RAMXEED family page for MB85RC1MT / MB85RC512T / MB85RC64TA | web |
| 11 | `ramxeed_i3_i2c_family.html` | Official RAMXEED family page for MB85RC256V / MB85RC256VN / MB85RC128A / MB85RC64A / MB85RC64V / MB85RC16 / MB85RC16V / MB85RC04 / MB85RC04V | web |

Note: RAMXEED Limited (formerly Fujitsu Semiconductor Memory Solutions Limited) continues to offer and support existing products with Fujitsu part numbers unchanged. [datasheet_MB85RC256V_v2.pdf, p1]

---

## 2. Device identity and variants

- **Part number**: MB85RC256V [datasheet_MB85RC256V_v2.pdf, p1]
- **Manufacturer**: RAMXEED Limited (formerly Fujitsu Semiconductor Memory Solutions Limited) [datasheet_MB85RC256V_v2.pdf, p1]
- **Technology**: FeRAM (Ferroelectric Random Access Memory) — ferroelectric process + silicon gate CMOS [datasheet_MB85RC256V_v2.pdf, p1]
- **Configuration**: 32,768 words × 8 bits = 256 Kbit (32 KB) [datasheet_MB85RC256V_v2.pdf, p1]

### Ordering variants

| Part Number | Package | Shipping Form |
|-------------|---------|---------------|
| MB85RC256VPNF-G-JNE1 | 8-pin SOP 150mil (FPT-8P-M02), 3.90mm × 5.05mm, 1.27mm pitch | Tube |
| MB85RC256VPNF-G-JNERE1 | 8-pin SOP 150mil | Embossed Carrier tape (1500 pcs/reel) |
| MB85RC256VPNF-G-AME2 | 8-pin SOP 150mil | Tray |
| MB85RC256VPNF-G-AMERE2 | 8-pin SOP 150mil | Embossed Carrier tape (1500 pcs/reel) |
| MB85RC256VPF-G-BCE1 | 8-pin SOP 208mil (FPT-8P-M08), 5.30mm × 5.65mm, 1.27mm pitch | Tube |
| MB85RC256VPF-G-BCERE1 | 8-pin SOP 208mil | Embossed Carrier tape (500 pcs/reel) |

[datasheet_MB85RC256V_v2.pdf, p16]

### Addendum: Fact sheet ordering variants (older suffixes)

The original fact sheet (2013) lists different part numbers for the 208 mil package compared to the current v2 datasheet:

| Part Number (Fact Sheet) | Package | Shipping Form |
|--------------------------|---------|---------------|
| MB85RC256VPF-G-JNE2 | 8-pin SOP 208mil (FPT-8P-M08), 5.30mm × 5.24mm, 1.27mm pitch | Tube |
| MB85RC256VPF-G-JNERE2 | 8-pin SOP 208mil | Embossed Carrier tape |

Note: The fact sheet lists 208 mil package length as 5.24mm, while the v2 datasheet specifies 5.65mm. The v2 datasheet variants (BCE1/BCERE1) supersede the fact sheet variants (JNE2/JNERE2). [datasheet_MB85RC256V.pdf, p1]

### Device ID (readable via I2C)

| Field | Value | Bits |
|-------|-------|------|
| Manufacturer ID | 0x00A | 12 bits |
| Product ID | 0x510 | 12 bits |
| Density code | 0x5 | 4 bits (within Product ID) |

Device ID is 3 bytes total, read-only, composed of Manufacturer ID and Product ID. [datasheet_MB85RC256V_v2.pdf, p9]

---

## 3. High-level functional summary

The MB85RC256V is a 256-Kbit ferroelectric RAM (FRAM/FeRAM) with an I2C serial interface. It stores data nonvolatilely using ferroelectric memory cells — unlike SRAM, it retains data without a battery. [datasheet_MB85RC256V_v2.pdf, p1]

**Key differentiators from EEPROM/Flash:**
- **No write delay**: The device does NOT need a polling sequence after writing. Data is written immediately as each byte is clocked in. No programming wait time (tWC) is required after the Stop Condition. [datasheet_MB85RC256V_v2.pdf, p4, p6, p7]
- **Endurance**: 10^12 read/write cycles per byte (vs ~10^5–10^6 for EEPROM). Total number of reading AND writing counts toward endurance because FeRAM uses destructive readout. [datasheet_MB85RC256V_v2.pdf, p14]
- **Data retention**: 10 years at +85°C, 95 years at +55°C, >200 years at +35°C [datasheet_MB85RC256V_v2.pdf, p14]

The device is suitable for log management, resume data storage, and high-frequency write applications. [datasheet_MB85RC256V.pdf, p1]

---

## 4. Interface summary

- **Interface type**: I2C (two-wire serial: SCL + SDA) [datasheet_MB85RC256V_v2.pdf, p1]
- **Role**: Slave device only [datasheet_MB85RC256V_v2.pdf, p3]
- **Maximum clock frequency**: 1 MHz (Fast Mode Plus) [datasheet_MB85RC256V_v2.pdf, p1, p12]
- **Supported I2C modes**: Standard Mode (100 kHz), Fast Mode (400 kHz), Fast Mode Plus (1 MHz) [datasheet_MB85RC256V_v2.pdf, p12]
- **Max devices on bus**: 8 (using 3-bit address pins A0, A1, A2) [datasheet_MB85RC256V_v2.pdf, p2]
- **SDA output type**: Open drain (requires external pull-up resistor) [datasheet_MB85RC256V_v2.pdf, p2]

### I2C Slave Address (Device Address Word)

The 7-bit slave address is formed as:

```
Bit:  7    6    5    4    3    2    1    0
      1    0    1    0    A2   A1   A0   R/W
      ├── Device Type ──┤├─ Addr Code ─┤
```

- **Device type code** (upper 4 bits): Fixed `1010` (0xA) [datasheet_MB85RC256V_v2.pdf, p5]
- **Device address code** (3 bits): Determined by hardware pins A2, A1, A0 [datasheet_MB85RC256V_v2.pdf, p5]
- **R/W bit**: 0 = write, 1 = read [datasheet_MB85RC256V_v2.pdf, p5]
- **7-bit I2C address range**: 0x50–0x57 (depending on A2:A0 pin configuration)

If the device code is not `1010` or the address code does not match pins A2/A1/A0, the device enters standby. [datasheet_MB85RC256V_v2.pdf, p5]

### Memory Addressing

- **Memory address size**: 15 bits (sent as 2 bytes) [datasheet_MB85RC256V_v2.pdf, p7]
- **Address range**: 0x0000 to 0x7FFF (32,768 bytes)
- **MSB of high address byte**: Must be set to `0` (address is only 15 bits) [datasheet_MB85RC256V_v2.pdf, p7]
- **Address format**: `[0][A14:A8]` `[A7:A0]` (high byte then low byte)

---

## 5. Electrical and timing constraints relevant to software

### DC Characteristics (within recommended operating conditions)

| Parameter | Symbol | Condition | Min | Typ | Max | Unit |
|-----------|--------|-----------|-----|-----|-----|------|
| Operating supply voltage | VDD | — | 2.7 | — | 5.5 | V |
| Operating current | IDD | SCL = 400 kHz | — | 75 | 130 | µA |
| Operating current | IDD | SCL = 1000 kHz | — | 140 | 200 | µA |
| Standby current | ISB | Under Stop Condition, TA ≤ 25°C | — | 27 | — | µA |
| Standby current | ISB | Under Stop Condition, TA ≤ 85°C | — | — | 56 | µA |
| VIH (input high) | VIH | VDD 2.7V–5.5V | VDD×0.8 | — | 5.5 | V |
| VIL (input low) | VIL | VDD 2.7V–5.5V | VSS | — | VDD×0.2 | V |
| VOL (output low) | VOL | IOL = 3 mA | — | — | 0.4 | V |
| Input leakage (SCL, SDA) | \|ILI\| | VIN 0V to VDD | — | — | 1 | µA |
| Output leakage (SDA) | \|ILO\| | VOUT 0V to VDD | — | — | 1 | µA |
| WP/A0-A2 pull-down resistance | RIN | VIN ≤ VIL(max) | 50 | — | — | kΩ |
| WP/A0-A2 pull-up resistance | RIN | VIN ≥ VIH(min) | 1 | — | — | MΩ |

[datasheet_MB85RC256V_v2.pdf, p12]

### AC Timing Characteristics

| Parameter | Symbol | Standard Mode | Fast Mode | Fast Mode Plus | Unit |
|-----------|--------|:---:|:---:|:---:|------|
| SCL clock frequency | FSCL | 0–100 | 0–400 | 0–1000 | kHz |
| Clock high time | THIGH | min 4000 | min 600 | min 400 | ns |
| Clock low time | TLOW | min 4700 | min 1300 | min 600 | ns |
| SCL/SDA rise time | Tr | max 1000 | max 300 | max 300 | ns |
| SCL/SDA fall time | Tf | max 300 | max 300 | max 100 | ns |
| Start condition hold | THD:STA | min 4000 | min 600 | min 250 | ns |
| Start condition setup | TSU:STA | min 4700 | min 600 | min 250 | ns |
| SDA input hold | THD:DAT | min 0 | min 0 | min 0 | ns |
| SDA input setup | TSU:DAT | min 250 | min 100 | min 100 | ns |
| SDA output hold | TDH:DAT | min 0 | min 0 | min 0 | ns |
| Stop condition setup | TSU:STO | min 4000 | min 600 | min 250 | ns |
| SDA output access after SCL falling | TAA | max 3000 | max 900 | max 550 | ns |
| Bus free time (between Stop and Start) | TBUF | min 4700 | min 1300 | min 500 | ns |
| Noise suppression time (SCL, SDA) | TSP | max 50 | max 50 | max 50 | ns |

[datasheet_MB85RC256V_v2.pdf, p12]

### Pin Capacitance

| Parameter | Symbol | Max | Unit |
|-----------|--------|-----|------|
| I/O capacitance (SDA) | CI/O | 15 | pF |
| Input capacitance (SCL) | CIN | 15 | pF |

Measured at VDD = 5.0V, f = 1 MHz, TA = +25°C. [datasheet_MB85RC256V_v2.pdf, p13]

### Gap Fill: Absolute Maximum Ratings

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Power supply voltage | VDD | −0.5 | +6.0 | V |
| Input voltage | VIN | −0.5 | VDD + 0.5 (≤ 6.0) | V |
| Output voltage | VOUT | −0.5 | VDD + 0.5 (≤ 6.0) | V |
| Operation ambient temperature | TA | −40 | +85 | °C |
| Storage temperature | Tstg | −55 | +125 | °C |

All parameters based on VSS = 0 V. Semiconductor devices can be permanently damaged by application of stress in excess of absolute maximum ratings. [datasheet_MB85RC256V_v2.pdf, p11]

### Gap Fill: Recommended Operating Conditions (formal)

| Parameter | Symbol | Min | Typ | Max | Unit |
|-----------|--------|-----|-----|-----|------|
| Power supply voltage | VDD | 2.7 | — | 5.5 | V |
| Operation ambient temperature | TA | −40 | — | +85 | °C |

Based on VSS = 0 V. TA is ambient temperature when only this device is working (approximately equal to package surface temperature). Operation outside recommended ranges may adversely affect reliability and could result in device failure. [datasheet_MB85RC256V_v2.pdf, p11]

### Gap Fill: Standby current measurement conditions detail

The ISB (standby current) values in the DC table above are measured with: SCL and SDA = VDD, A0/A1/A2/WP = 0 V or VDD or Open, under Stop Condition. [datasheet_MB85RC256V_v2.pdf, p12]

### Gap Fill: AC measurement conditions

AC characteristics were measured under the following conditions: [datasheet_MB85RC256V_v2.pdf, p12]

| Parameter | Value |
|-----------|-------|
| Power supply voltage | 2.7 V to 5.5 V |
| Operation ambient temperature | −40°C to +85°C |
| Input voltage magnitude | VDD × 0.2 to VDD × 0.8 |
| Input rising time | 5 ns |
| Input falling time | 5 ns |
| Input judge level | VDD/2 |
| Output judge level | VDD/2 |

### Gap Fill: AC test load circuit

The AC timing output specs (particularly TAA — SDA output access time after SCL falling) are measured with a test load of **100 pF** capacitance and **1.8 kΩ** pull-up resistance to VDD. [datasheet_MB85RC256V_v2.pdf, p13]

### Gap Fill: TBUF naming note

The TBUF parameter is labeled "Pre-charge time" in the datasheet (not "Bus free time" as in the I2C specification). It represents the minimum time between a Stop condition and the next Start condition. [datasheet_MB85RC256V_v2.pdf, p12]

### Critical software-relevant note

**No write cycle time (tWC)**: Unlike EEPROM, FRAM writes are completed in real-time as each byte is clocked in. No polling or wait time is needed after a Stop Condition. [datasheet_MB85RC256V_v2.pdf, p4, p6, p7]

---

## 6. Power, reset, enable, and startup behavior

### Power-On Sequence

| Parameter | Symbol | Min | Max | Unit | Condition |
|-----------|--------|-----|-----|------|-----------|
| SDA/SCL hold during power-down | tpd | 85 | — | ns | — |
| SDA/SCL hold during power-up (5V) | tpu | 85 | — | ns | VDD = 5.0V ± 0.5V |
| SDA/SCL hold during power-up (3.3V) | tpu | 0.5 | — | ms | VDD = 3.3V ± 0.3V |
| Power supply rise time (5V) | tr | 0.5 | 50 | ms | VDD = 5.0V ± 0.5V |
| Power supply rise time (3.3V) | tr | 0.005 | 50 | ms | VDD = 3.3V ± 0.3V |
| Power supply fall time | tf | 0.5 | 50 | ms | — |

[datasheet_MB85RC256V_v2.pdf, p14]

**Power-up signal requirements:**
- During power-up, SDA and SCL must be held above VDD × 0.8 until VDD reaches VIH(min) = 2.7V [datasheet_MB85RC256V_v2.pdf, p14]
- SDA and SCL max must remain below VDD + 0.5V during power-up [datasheet_MB85RC256V_v2.pdf, p14]
- During power-down, SDA and SCL must be held above VDD × 0.8 before VDD falls below VIH(min) [datasheet_MB85RC256V_v2.pdf, p14]

**WARNING**: If the device does not operate within the specified conditions of read cycle, write cycle, or power on/off sequence, memory data cannot be guaranteed. [datasheet_MB85RC256V_v2.pdf, p14]

### Current Address After Power-On

The current address in the memory address buffer is **undefined** immediately after the power is turned on. A Random Read (which sets the address explicitly) or Write operation should be performed before using Current Address Read. [datasheet_MB85RC256V_v2.pdf, p8]

### Software Reset Sequence

If malfunction occurs after power-on, or communication is interrupted, use one of: [datasheet_MB85RC256V_v2.pdf, p10]

**(1) Software Reset Sequence** (before each command):
- Send 9 sets of "Start Condition + one '1' data bit" on the I2C bus
- This unblocks a slave that may be holding SDA low
- No additional hardware required
- Caution: Do NOT force SDA high if slave may be driving it low (to prevent bus conflict)

**(2) Command Retry** (after failure):
- Simply retry the failed command after receiving a failure response

[datasheet_MB85RC256V_v2.pdf, p10]

---

## 7. Pin behavior relevant to firmware

| Pin # | Name | Direction | Description |
|-------|------|-----------|-------------|
| 1 | A0 | Input | Device address bit 0. Connect to VDD or VSS. Internally pulled-down (open = "L" = 0). |
| 2 | A1 | Input | Device address bit 1. Connect to VDD or VSS. Internally pulled-down (open = "L" = 0). |
| 3 | A2 | Input | Device address bit 2. Connect to VDD or VSS. Internally pulled-down (open = "L" = 0). |
| 4 | VSS | Power | Ground |
| 5 | SDA | I/O | Serial data (bidirectional). Open-drain output — requires external pull-up. |
| 6 | SCL | Input | Serial clock. Data sampled on rising edge, output on falling edge. |
| 7 | WP | Input | Write Protect. H = write disabled (entire array). L = write enabled. Internally pulled-down (open = "L" = write enabled). |
| 8 | VDD | Power | Supply voltage (2.7V to 5.5V) |

[datasheet_MB85RC256V_v2.pdf, p2; datasheet_MB85RC256V.pdf, p2]

### Address Pin Behavior

- A0, A1, A2 internally pulled-down — if left open, recognized as "L" (0) [datasheet_MB85RC256V_v2.pdf, p2]
- Default I2C address with all pins open: 0x50 (7-bit)
- Pull-down resistance: min 50 kΩ (at VIL). Pull-up resistance: min 1 MΩ (at VIH). [datasheet_MB85RC256V_v2.pdf, p12]

### Write Protect Pin Behavior

- Internally pulled-down to VSS — open pin defaults to "L" (write enabled) [datasheet_MB85RC256V_v2.pdf, p2, p6]
- WP = "H": **entire** memory array is write-protected. Write operations are disabled. [datasheet_MB85RC256V_v2.pdf, p6]
- WP = "L": Entire memory array is writable [datasheet_MB85RC256V_v2.pdf, p6]
- Read operations always succeed regardless of WP state [datasheet_MB85RC256V_v2.pdf, p6]
- **IMPORTANT**: During the access period from Start to Stop condition, WP, A0, A1, and A2 pin levels must remain stable at "H" or "L". Do not change them mid-transaction. [datasheet_MB85RC256V_v2.pdf, p15]

---

## 8. Register map overview

Not applicable to this device.

The MB85RC256V is a flat-addressable FRAM memory array with no internal registers. Memory is accessed directly by specifying a 15-bit address (0x0000–0x7FFF). There are no configuration registers, status registers, or control registers.

Cross-check against official MB85RC512T, MB85RC64TA, MB85RC1MT, MB85RC16V, and MB85RC04V sources did not reveal a conventional configuration/status register block on those sibling parts either. Across the checked family, protocol differences are expressed through the device-address word, the number of memory-address bits/bytes, the optional Device-ID command, and on some parts optional High Speed / Sleep entry sequences. [MB85RC512T-DS6v1-E.pdf, pp4-10; MB85RC64TA-DS5v1-E.pdf, pp4-11; MB85RC1MT-DS5v1-E.pdf, pp4-10; MB85RC16V-DS11v0-E.pdf, pp4-8; MB85RC04V-DS5v1-E.pdf, pp4-9]

---

## 9. Detailed register and bitfield breakdown

Not applicable to this device. The MB85RC256V has no registers — only a 32,768-byte memory array and a 3-byte read-only Device ID.

### Device ID Fields

The 3-byte Device ID is encoded as follows: [datasheet_MB85RC256V_v2.pdf, p9]

```
Data Byte 1st:  [11:8] of Manufacturer ID  = 0x0
Data Byte 2nd:  [7:0]  of Manufacturer ID  = 0x0A    → Full Manufacturer ID = 0x00A
Data Byte 3rd:  [11:8] of Product ID (bits 11:8)  = 0x5 (Density code)
                [7:4]  Proprietary use             = 0x1
                [3:0]  Proprietary use             = 0x0  → Full Product ID = 0x510
```

Byte breakdown:
| Byte | Bits [11:0] | Value |
|------|-------------|-------|
| 1st | Manufacturer ID [11:4] | 0x00 |
| 2nd | Manufacturer ID [3:0] + Product ID [11:8] | 0xA5 |
| 3rd | Product ID [7:0] | 0x10 |

**Note**: The exact bit packing across the 3 bytes is shown in the datasheet diagram. Manufacturer ID = 0x00A (12 bits), Product ID = 0x510 (12 bits). Density = 0x5 (within Product ID upper nibble). [datasheet_MB85RC256V_v2.pdf, p9]

---

## 10. Commands and transaction-level behaviors

### Byte Write

Writes a single byte to a specified address. [datasheet_MB85RC256V_v2.pdf, p7]

```
[S] [1010 A2 A1 A0 0] [ACK] [Addr High: 0+7bits] [ACK] [Addr Low: 8bits] [ACK] [Data: 8bits] [ACK] [P]
```

1. Master sends Start
2. Master sends device address word with R/W = 0 (write)
3. Slave ACKs
4. Master sends high address byte (MSB must be 0, then A14:A8)
5. Slave ACKs
6. Master sends low address byte (A7:A0)
7. Slave ACKs
8. Master sends data byte
9. Slave ACKs
10. Master sends Stop

**No write cycle delay required after Stop.** [datasheet_MB85RC256V_v2.pdf, p7]

### Page Write (Sequential Write)

Writes multiple consecutive bytes starting from a specified address. [datasheet_MB85RC256V_v2.pdf, p7]

```
[S] [1010 A2 A1 A0 0] [ACK] [Addr High] [ACK] [Addr Low] [ACK] [Data0] [ACK] [Data1] [ACK] ... [DataN] [ACK] [P]
```

- After Byte Write setup, continue sending data bytes (each ACK'd by slave)
- **Address auto-increments** after each byte
- Address **rolls over** from 0x7FFF to 0x0000 at end of memory [datasheet_MB85RC256V_v2.pdf, p7]
- If more than 32K bytes sent, earlier data is overwritten starting from the initial address
- **No page boundary limitation** — unlike EEPROM, the address simply auto-increments through the entire 32KB array. There is no page buffer.
- Data is written to FRAM immediately after each ACK response (not buffered and written on Stop). [datasheet_MB85RC256V_v2.pdf, p7]
- **No write cycle time needed after Stop.** [datasheet_MB85RC256V_v2.pdf, p7]

### Current Address Read

Reads one byte from address N+1, where N was the last accessed address. [datasheet_MB85RC256V_v2.pdf, p8]

```
[S] [1010 A2 A1 A0 1] [ACK] [Data (N+1)] [NACK] [P]
```

- After a previous successful read or write that ended with Stop, the internal address counter holds N+1
- If last address was 0x7FFF, rolls over to 0x0000 [datasheet_MB85RC256V_v2.pdf, p8]
- **Current address is UNDEFINED after power-on** — do not use Current Address Read as the first operation after power-on [datasheet_MB85RC256V_v2.pdf, p8]
- The NACK is issued by the **master** (receiver) to signal end of read [datasheet_MB85RC256V_v2.pdf, p8]

### Random Read

Reads one byte from a specific address. [datasheet_MB85RC256V_v2.pdf, p8]

```
[S] [1010 A2 A1 A0 0] [ACK] [Addr High] [ACK] [Addr Low] [ACK]
[Sr] [1010 A2 A1 A0 1] [ACK] [Data] [NACK] [P]
```

1. Master sends Start + device address (write mode) + 2-byte address — this sets the address pointer
2. Master sends Repeated Start (NOT Stop)
3. Master sends device address (read mode)
4. Slave sends data byte from specified address
5. Master sends NACK then Stop

### Sequential Read

Reads multiple consecutive bytes starting from a specified address. [datasheet_MB85RC256V_v2.pdf, p9]

```
[S] [1010 A2 A1 A0 0] [ACK] [Addr High] [ACK] [Addr Low] [ACK]
[Sr] [1010 A2 A1 A0 1] [ACK] [Data0] [ACK] [Data1] [ACK] ... [DataN] [NACK] [P]
```

- After Random Read setup, master ACKs each received byte to continue reading
- Address auto-increments after each byte
- Address rolls over from 0x7FFF to 0x0000 [datasheet_MB85RC256V_v2.pdf, p9]
- Master sends NACK on the last byte to terminate, then Stop

### Device ID Read

Reads the 3-byte read-only Device ID. [datasheet_MB85RC256V_v2.pdf, p9]

```
[S] [0xF8] [ACK] [1010 A2 A1 A0 R/W*] [ACK]
[Sr] [0xF9] [ACK] [Byte1] [ACK] [Byte2] [ACK] [Byte3] [NACK] [P]
```

Sequence:
1. Master sends Start
2. Master sends Reserved Slave ID **0xF8** (= 1111 1000)
3. Slave ACKs
4. Master sends device address word (R/W bit is **don't care**)
5. Slave ACKs
6. Master sends Repeated Start
7. Master sends Reserved Slave ID **0xF9** (= 1111 1001)
8. Slave ACKs
9. Slave sends 3 bytes of Device ID (1st, 2nd, 3rd) — master ACKs first two
10. Master sends NACK after 3rd byte, then Stop

**Special behavior**: If master sends ACK (instead of NACK) after the 3rd byte, the device re-sends the Device ID from byte 1 again (looping). [datasheet_MB85RC256V_v2.pdf, p9]

---

## 11. Initialization and configuration sequences

### Minimum initialization after power-on

1. **Wait for power-up**: Ensure VDD is stable and SDA/SCL levels meet power-up requirements (SDA, SCL > VDD×0.8 during ramp) [datasheet_MB85RC256V_v2.pdf, p14]
2. **Optional: Software Reset Sequence**: Send 9× (Start + one "1" bit) to clear any slave that is stuck holding SDA low [datasheet_MB85RC256V_v2.pdf, p10]
3. **Optional: Read Device ID**: Verify device identity by reading the 3-byte Device ID [datasheet_MB85RC256V_v2.pdf, p9]
4. **Set address pointer**: Perform a write or Random Read to establish the internal address counter (undefined after power-on) [datasheet_MB85RC256V_v2.pdf, p8]

There is no register configuration required. The device is ready to read/write immediately after power-up stabilization.

---

## 12. Operating modes and state machine behavior

The MB85RC256V has two externally meaningful states:

1. **Active**: Processing an I2C transaction (between Start and Stop conditions)
2. **Standby**: Idle, waiting for a Start condition (entered after Stop condition, or when addressed with non-matching device code)

[datasheet_MB85RC256V_v2.pdf, p4, p5]

### Standby entry conditions:
- After a Stop condition is received [datasheet_MB85RC256V_v2.pdf, p4]
- If the received device type code is not `1010` [datasheet_MB85RC256V_v2.pdf, p5]
- If the received device address code does not match hardware pins A2/A1/A0 [datasheet_MB85RC256V_v2.pdf, p5]
- If Stop condition is received before ACK during a transaction [datasheet_MB85RC256V_v2.pdf, p4]

### ACK/NACK behavior:
- Slave sends ACK ("L" on SDA at 9th clock) after successfully receiving each 8-bit field [datasheet_MB85RC256V_v2.pdf, p4]
- If slave receives Stop before sending/receiving ACK, slave stops operation and enters standby [datasheet_MB85RC256V_v2.pdf, p4]
- After sending/receiving NACK ("H" on SDA at 9th clock), the slave releases the bus. Master must then generate Stop or Start. [datasheet_MB85RC256V_v2.pdf, p4]
- During the 9th clock, the transmitter releases SDA to Hi-Z to allow the receiver to drive ACK/NACK [datasheet_MB85RC256V_v2.pdf, p4]

---

## 13. Measurement / data path behavior

Not applicable to this device.

The MB85RC256V is a memory device with no measurement, ADC, DAC, or signal processing capabilities.

---

## 14. Interrupts, alerts, status, and faults

Not applicable to this device.

The MB85RC256V has no interrupt output, alert pin, status register, or fault detection mechanism. Communication errors must be detected at the I2C protocol level (NACK detection, bus arbitration).

---

## 15. Nonvolatile memory / OTP / EEPROM behavior

### Memory Organization

- **Size**: 32,768 bytes (256 Kbit) [datasheet_MB85RC256V_v2.pdf, p1]
- **Address space**: 0x0000 to 0x7FFF (15-bit addressing) [datasheet_MB85RC256V_v2.pdf, p7]
- **Word size**: 8 bits [datasheet_MB85RC256V_v2.pdf, p1]
- **Type**: FeRAM (Ferroelectric RAM) — nonvolatile [datasheet_MB85RC256V_v2.pdf, p1]

### Write Behavior — CRITICAL DIFFERENCE FROM EEPROM

- **No write delay**: Data is committed to nonvolatile memory immediately as each byte is clocked in and ACKed. There is NO internal write cycle, NO page buffer, and NO write completion polling needed. [datasheet_MB85RC256V_v2.pdf, p4, p6, p7]
- **No page size limitation**: Sequential writes auto-increment through the entire 32KB address space without page boundaries. Address wraps from 0x7FFF to 0x0000. [datasheet_MB85RC256V_v2.pdf, p7]
- **No ACK polling needed**: The "FeRAM ACKNOWLEDGE -- POLLING NOT REQUIRED" section explicitly states that no waiting time for ACK polling occurs, unlike E2PROM which requires Acknowledge Polling to check write completion. [datasheet_MB85RC256V_v2.pdf, p6]
- **Immediate write**: "Because FeRAM performs the high-speed write operations, the data will be written to FeRAM right after the ACK response finished." [datasheet_MB85RC256V_v2.pdf, p7]
- **No buffer flush on Stop**: "It is not necessary to take a period for internal write operation cycles from the buffer to the memory after the stop condition is generated." [datasheet_MB85RC256V_v2.pdf, p7]

### Endurance

- **Read/write endurance**: Minimum 10^12 cycles per byte [datasheet_MB85RC256V_v2.pdf, p14]
- **Destructive readout**: FeRAM reads are destructive — the data is restored automatically, but both reads and writes count toward the endurance limit. [datasheet_MB85RC256V_v2.pdf, p14]

### Data Retention

| Temperature | Retention |
|-------------|-----------|
| ≤ +85°C | ≥ 10 years |
| ≤ +55°C | ≥ 95 years |
| ≤ +35°C | ≥ 200 years |

Minimum values define retention time of data written right after shipment, calculated from qualification results. [datasheet_MB85RC256V_v2.pdf, p14]

### Write Protection

- Hardware WP pin: "H" = entire array protected, "L" = entire array writable [datasheet_MB85RC256V_v2.pdf, p6]
- No software-controlled write protection or block protection
- No permanent lock or OTP regions
- WP pin open = write enabled (internal pull-down) [datasheet_MB85RC256V_v2.pdf, p6]

### Reflow Note

"We recommend programming of the device after reflow. Data written before reflow cannot be guaranteed." [datasheet_MB85RC256V_v2.pdf, p15]

---

## 16. Special behaviors, caveats, and footnotes

1. **Address MSB must be 0**: The 15-bit address is sent as 2 bytes. The most significant bit of the high byte MUST be `0`. [datasheet_MB85RC256V_v2.pdf, p7]

2. **Address rollover**: Address auto-increments and wraps from 0x7FFF to 0x0000 for both reads and writes. [datasheet_MB85RC256V_v2.pdf, p7, p8, p9]

3. **Undefined current address at power-on**: The internal address counter is undefined after power-on. Do not use Current Address Read as the first memory operation. [datasheet_MB85RC256V_v2.pdf, p8]

4. **Destructive readout endurance**: Both reads and writes count toward the 10^12 endurance limit. This is an inherent property of FeRAM. [datasheet_MB85RC256V_v2.pdf, p14]

5. **Pin stability during transactions**: WP, A0, A1, A2 pin levels must remain stable (H or L) for the entire access period from Start to Stop. Do not toggle them during a transaction. [datasheet_MB85RC256V_v2.pdf, p15]

6. **Reflow damages pre-programmed data**: Data written before reflow soldering cannot be guaranteed. Program after reflow. [datasheet_MB85RC256V_v2.pdf, p15]

7. **Device ID re-read behavior**: If master sends ACK after the 3rd Device ID byte (instead of NACK), the device loops back and re-sends the ID from byte 1. [datasheet_MB85RC256V_v2.pdf, p9]

8. **Reserved Slave IDs for Device ID**: The Device ID read sequence uses I2C reserved addresses 0xF8 (write) and 0xF9 (read). These are standard I2C Device ID addresses. [datasheet_MB85RC256V_v2.pdf, p9]

9. **SDA level during software reset**: When performing the 9× Start + "1" reset sequence, do not force SDA high if the slave may be driving it low — this prevents bus conflict. Rely on pull-up resistors for Hi-Z state. [datasheet_MB85RC256V_v2.pdf, p10]

10. **No write delay confirmation**: The datasheet explicitly states multiple times that no programming wait time exists after Stop. This is a fundamental FeRAM advantage over EEPROM. [datasheet_MB85RC256V_v2.pdf, p4, p6, p7]

11. **Data sampling**: SDA is sampled on the rising edge of SCL and output on the falling edge of SCL. [datasheet_MB85RC256V_v2.pdf, p2]

12. **ESD protection**: HBM ≥ ±2000V (JESD22-A114 compliant), CDM per JESD22-C101 compliant, Latch-up C-V ≥ ±200V (proprietary method). All variants tested. [datasheet_MB85RC256V_v2.pdf, p15]

13. **Moisture Sensitivity Level**: MSL 3 per IPC/JEDEC J-STD-020E. Relevant for reflow soldering floor life. [datasheet_MB85RC256V_v2.pdf, p15]

14. **Regulatory compliance**: Product complies with REACH Regulations, EU RoHS Directive, and China RoHS. [datasheet_MB85RC256V_v2.pdf, p15]

15. **Storage temperature range**: −55°C to +125°C (Tstg). Distinct from operating range of −40°C to +85°C. [datasheet_MB85RC256V_v2.pdf, p11]

16. **Tray baking note**: Tube and tape/reel packaging have no heat resistance — packages should NOT be baked using tube or tape/reel. Tray packaging is heat-proof up to 150°C max. [datasheet_MB85RC256V_v2.pdf, p21, p24, p28]

---

## 17. Recommended polling and control strategy hints from the docs

1. **Do NOT poll for write completion**: Unlike EEPROM, the MB85RC256V requires no ACK polling or delay after writes. Immediately proceed with the next operation after Stop. [datasheet_MB85RC256V_v2.pdf, p6]

2. **Use Software Reset Sequence** before each command if bus stability is uncertain: Send 9× (Start + one "1" bit) to recover from any hung slave state. [datasheet_MB85RC256V_v2.pdf, p10]

3. **Use Command Retry** after failed commands: If an I2C communication fails (NACK received unexpectedly), simply retry the command. [datasheet_MB85RC256V_v2.pdf, p10]

4. **Verify device identity on startup**: Use the Device ID read sequence to confirm the correct device is present (Manufacturer ID = 0x00A, Product ID = 0x510). [datasheet_MB85RC256V_v2.pdf, p9]

5. **Maximum throughput**: At 1 MHz Fast Mode Plus, continuous sequential writes or reads can achieve near the full bus bandwidth since there is no write cycle delay.

---

## 18. Ambiguities, conflicts, and missing information

1. **Device ID byte encoding**: The datasheet provides the 3-byte Device ID in a diagram with 12-bit Manufacturer ID (0x00A) and 12-bit Product ID (0x510), but the exact bit-packing across the 3 bytes (24 bits = two 12-bit fields) requires careful reading of the bit-position diagram. The raw hex bytes are not explicitly listed as 0x00, 0xA5, 0x10 — this is inferred from the bit layout. [datasheet_MB85RC256V_v2.pdf, p9]

2. **Behavior on write to address with MSB=1**: The datasheet says "input '0' to the most significant bit of the higher address byte" but does not state whether the device ignores bit 15, masks it, or NACKs. Behavior is unspecified. [datasheet_MB85RC256V_v2.pdf, p7]

3. **WP pin timing**: The datasheet says WP must remain stable from Start to Stop, but does not specify the exact setup/hold time relative to Start/Stop conditions. [datasheet_MB85RC256V_v2.pdf, p15]

4. **Clock stretching**: Not stated in reviewed documentation. It is not mentioned whether the MB85RC256V performs clock stretching.

5. **Multi-master support**: Not stated in reviewed documentation. The datasheet only describes single-master multi-slave configurations.

6. **General Call address (0x00) response**: Not stated in reviewed documentation.

7. **10-bit addressing**: Not stated in reviewed documentation. Only 7-bit addressing is described.

8. **Behavior during power brownout**: Not stated in reviewed documentation beyond the power-on/off sequence constraints.

9. **Standby current conditions**: The standby current spec at 25°C lists only a typical value (27 µA) with no max. The max (56 µA) is only given at 85°C. [datasheet_MB85RC256V_v2.pdf, p12]

10. **Address pins internal pull-down value**: The pull-down resistance for A0-A2 and WP is specified indirectly (min 50 kΩ at VIL), but the exact pull-down resistance is not given as a typical value. [datasheet_MB85RC256V_v2.pdf, p12]

---

## 19. Raw implementation checklist

- [ ] Configure I2C peripheral for up to 1 MHz (Fast Mode Plus), or lower as needed
- [ ] Set 7-bit slave address to `0x50 | (A2 << 2) | (A1 << 1) | A0` based on hardware pin configuration
- [ ] Implement power-up delay: wait for VDD stable, ensure SDA/SCL > VDD×0.8 during ramp
- [ ] On startup, send Software Reset Sequence (9× Start + "1" bit) if bus state is uncertain
- [ ] On startup, optionally verify Device ID: send 0xF8, device addr, Repeated Start, 0xF9, read 3 bytes — confirm Manufacturer ID = 0x00A, Product ID = 0x510
- [ ] Implement Byte Write: Start, slave addr (W), addr high (MSB=0), addr low, data, Stop — **no post-write delay**
- [ ] Implement Sequential Write (Page Write): same as byte write but send multiple data bytes — address auto-increments, wraps at 0x7FFF→0x0000, **no page boundary, no post-write delay**
- [ ] Implement Random Read: Start, slave addr (W), addr high, addr low, Repeated Start, slave addr (R), read data byte, NACK, Stop
- [ ] Implement Sequential Read: same as Random Read but ACK each byte to continue, NACK on last byte, Stop — address wraps at 0x7FFF→0x0000
- [ ] Implement Current Address Read: Start, slave addr (R), read data byte, NACK, Stop — only use after at least one write or random read has set the address
- [ ] Never use Current Address Read as the first operation after power-on (address undefined)
- [ ] Ensure MSB of high address byte is always 0 (15-bit address space)
- [ ] Handle WP pin: set low (or leave open) for write-enabled, set high for write-protected
- [ ] Do NOT toggle A0, A1, A2, or WP pins during an I2C transaction (Start to Stop)
- [ ] Implement command retry on NACK/failure
- [ ] Do NOT implement ACK polling or write cycle delay — this is FRAM, not EEPROM
- [ ] Be aware that reads also count toward the 10^12 endurance limit (destructive readout)
- [ ] If programming during manufacturing, program AFTER reflow soldering

---

## 20. Source citation appendix

| Fact | Source | Page / lines |
|------|--------|--------------|
| Device configuration 32,768 × 8 bits | datasheet_MB85RC256V_v2.pdf | 1 |
| Operating frequency 1 MHz max | datasheet_MB85RC256V_v2.pdf | 1 |
| VDD range 2.7V to 5.5V | datasheet_MB85RC256V_v2.pdf | 1 |
| Endurance 10^12 cycles/byte | datasheet_MB85RC256V_v2.pdf | 1, 14 |
| Data retention 10yr/95yr/200yr | datasheet_MB85RC256V_v2.pdf | 14 |
| Operating current 200 µA max at 1 MHz | datasheet_MB85RC256V_v2.pdf | 1, 12 |
| Standby current 27 µA typ | datasheet_MB85RC256V_v2.pdf | 1, 12 |
| Pin assignments and descriptions | datasheet_MB85RC256V_v2.pdf | 2 |
| A0-A2 internal pull-down, open = "L" | datasheet_MB85RC256V_v2.pdf | 2 |
| WP internal pull-down, open = write enabled | datasheet_MB85RC256V_v2.pdf | 2, 6 |
| SDA open-drain output, needs pull-up | datasheet_MB85RC256V_v2.pdf | 2 |
| Data sampled on SCL rising, output on falling | datasheet_MB85RC256V_v2.pdf | 2 |
| I2C slave device | datasheet_MB85RC256V_v2.pdf | 3 |
| Up to 8 devices on same bus | datasheet_MB85RC256V_v2.pdf | 2, 3 |
| Start/Stop condition definitions | datasheet_MB85RC256V_v2.pdf | 4 |
| No write delay after Stop condition | datasheet_MB85RC256V_v2.pdf | 4 |
| ACK/NACK protocol | datasheet_MB85RC256V_v2.pdf | 4 |
| Device type code 1010 | datasheet_MB85RC256V_v2.pdf | 5 |
| Device address word format | datasheet_MB85RC256V_v2.pdf | 5 |
| R/W bit: 0=write, 1=read | datasheet_MB85RC256V_v2.pdf | 5 |
| Standby on address mismatch | datasheet_MB85RC256V_v2.pdf | 5 |
| 15-bit memory addressing, 2-byte address | datasheet_MB85RC256V_v2.pdf | 6 |
| ACK polling not required (FeRAM) | datasheet_MB85RC256V_v2.pdf | 6 |
| Write protection: WP H=protected, L=writable | datasheet_MB85RC256V_v2.pdf | 6 |
| Byte Write command | datasheet_MB85RC256V_v2.pdf | 7 |
| Page Write (sequential write), no page boundary | datasheet_MB85RC256V_v2.pdf | 7 |
| Address MSB must be 0 | datasheet_MB85RC256V_v2.pdf | 7 |
| Address rollover 0x7FFF→0x0000 | datasheet_MB85RC256V_v2.pdf | 7, 8, 9 |
| Data written right after ACK | datasheet_MB85RC256V_v2.pdf | 7 |
| Current Address Read | datasheet_MB85RC256V_v2.pdf | 8 |
| Current address undefined after power-on | datasheet_MB85RC256V_v2.pdf | 8 |
| Random Read | datasheet_MB85RC256V_v2.pdf | 8 |
| Sequential Read | datasheet_MB85RC256V_v2.pdf | 9 |
| Device ID (Manufacturer 0x00A, Product 0x510) | datasheet_MB85RC256V_v2.pdf | 9 |
| Device ID read sequence (0xF8, 0xF9) | datasheet_MB85RC256V_v2.pdf | 9 |
| Device ID re-read on ACK after 3rd byte | datasheet_MB85RC256V_v2.pdf | 9 |
| Software Reset Sequence (9× Start + "1") | datasheet_MB85RC256V_v2.pdf | 10 |
| Command Retry on failure | datasheet_MB85RC256V_v2.pdf | 10 |
| Absolute maximum ratings | datasheet_MB85RC256V_v2.pdf | 11 |
| DC characteristics table | datasheet_MB85RC256V_v2.pdf | 12 |
| AC timing: Standard/Fast/Fast Mode Plus | datasheet_MB85RC256V_v2.pdf | 12 |
| Pin capacitance 15 pF max | datasheet_MB85RC256V_v2.pdf | 13 |
| Power-on/off sequence timing | datasheet_MB85RC256V_v2.pdf | 14 |
| Destructive readout (reads count toward endurance) | datasheet_MB85RC256V_v2.pdf | 14 |
| Pin stability during transaction (Start to Stop) | datasheet_MB85RC256V_v2.pdf | 15 |
| Program after reflow, not before | datasheet_MB85RC256V_v2.pdf | 15 |
| ESD HBM ≥ ±2000V | datasheet_MB85RC256V_v2.pdf | 15 |
| Ordering information and part numbers | datasheet_MB85RC256V_v2.pdf | 16 |
| Package: 8-pin SOP 150mil and 208mil | datasheet_MB85RC256V.pdf | 1 |
| RAMXEED name change from Fujitsu | datasheet_MB85RC256V_v2.pdf | 1 |
| Library supported/unsupported MB85 family list | MB85_FRAM.h | 7-28 |
| Library metadata: author, license, Doxygen/clang-format notes, changelog | MB85_FRAM.h | 30-74 |
| Library constants: BUFFER_LENGTH default, I2C speed constants, min address, max devices | MB85_FRAM.h | 86-99 |
| Public API declarations and templated read/write/fillMemory implementations | MB85_FRAM.h | 110-239 |
| Exact function signatures, member initializers, and absence of a global instance | MB85_FRAM.h / MB85_FRAM.cpp | 110-239 / 7-20 |
| `begin()` scan and wraparound-based size detection | MB85_FRAM.cpp | 28-112 |
| Discovery transaction sequences used by `begin()` | MB85_FRAM.cpp | 42-105 |
| `totalBytes()`, `memSize()`, `getDevice()`, and `requestI2C()` behavior | MB85_FRAM.cpp | 21-27, 113-169 |
| Slot-to-address mapping and direct `_I2C[]` indexing behavior | MB85_FRAM.cpp | 44-49, 96-99, 113-169 |
| Read/write chunking, request sizing, and transmission-status handling | MB85_FRAM.h / MB85_FRAM.cpp | 131-239 / 146-169 |
| Multi-device boundary caveats in `getDevice()` / `read()` / `write()` | MB85_FRAM.h | 135-205 |
| RAMXEED `i1` family grouping and spec split (MB85RC1MT / MB85RC512T / MB85RC64TA, 3.4 MHz, 10^13 cycles) | ramxeed_i1_wide_voltage_i2c_family.html | 11-45 |
| RAMXEED `i3` family grouping and spec split (MB85RC256V / 128A / 64A / 64V / 16 / 16V / 04 / 04V, 1 MHz, 10^12 cycles) | ramxeed_i3_i2c_family.html | 11-45 |
| MB85RC512T device-address word uses A2/A1/A0 and 2 memory-address bytes | MB85RC512T-DS6v1-E.pdf | 4-5 |
| MB85RC512T High Speed mode, Sleep mode, Device ID 00AH / 658H, tREC 400 us | MB85RC512T-DS6v1-E.pdf | 8-10, 16 |
| MB85RC64TA device-address word uses A2/A1/A0 and 2 memory-address bytes | MB85RC64TA-DS5v1-E.pdf | 4-5 |
| MB85RC64TA High Speed mode, Sleep mode, Device ID 00AH / 358H, tREC 400 us | MB85RC64TA-DS5v1-E.pdf | 9-11, 17 |
| MB85RC1MT device-address word uses A2/A1 plus A16, with 17-bit current address | MB85RC1MT-DS5v1-E.pdf | 4, 7 |
| MB85RC1MT High Speed mode, Sleep mode, Device ID 00AH / 758H, tREC 400 us | MB85RC1MT-DS5v1-E.pdf | 8-10, 16 |
| MB85RC16V device-address word carries upper address bits; one lower address byte follows; current address is 11 bits | MB85RC16V-DS11v0-E.pdf | 4-7 |
| MB85RC04V device-address word mixes device-select and A8; current address is 9 bits; Device ID 00AH / 010H | MB85RC04V-DS5v1-E.pdf | 4, 8 |
| MB85RC16V software reset / command retry sequence | MB85RC16V-DS11v0-E.pdf | 8 |
| MB85RC04V software reset / command retry sequence | MB85RC04V-DS5v1-E.pdf | 9 |
| MB85RC512T / 64TA / 1MT High Speed entry command `0000 1XXX` and NACK behavior | MB85RC512T-DS6v1-E.pdf / MB85RC64TA-DS5v1-E.pdf / MB85RC1MT-DS5v1-E.pdf | 8 / 9 / 8 |
| MB85RC512T / 64TA / 1MT Sleep-mode transition uses `F8H`, device address word, then `86H` | MB85RC512T-DS6v1-E.pdf / MB85RC64TA-DS5v1-E.pdf / MB85RC1MT-DS5v1-E.pdf | 9 / 10 / 9 |

---

## 21. Arduino library extraction (`MB85_FRAM.h` / `MB85_FRAM.cpp`)

### 21.1 Library metadata from the header/source

- The header comment identifies the project as an Arduino library for the "MB85 Family of SRAM Memories"; the `.cpp` file repeats the same wording and directs readers back to the header for details. [MB85_FRAM.h:3-9; MB85_FRAM.cpp:1-5]
- The library includes `Wire.h` and `Arduino.h`, so it is written against the Arduino `Wire` I2C API. [MB85_FRAM.h:77-79]
- The stated author is Arnd `<Arnd@Zanduino.Com>` / `SV-Zanshin`. [MB85_FRAM.h:56-58]
- The declared license is GNU General Public License v3.0 or later. [MB85_FRAM.h:47-54]
- The header documents project CI/documentation conventions: Doxygen configuration comes from the Zanduino `Common` repository, with five named environment variables, and CI formatting uses the `Common` `.clang-format` file. [MB85_FRAM.h:30-45]

#### Library changelog carried in the header

| Version | Date | Developer | Comment |
|---------|------|-----------|---------|
| 1.0.6 | 2022-10-02 | LMFLox | Issue #7 - corrected computation of chip type |
| 1.0.5 | 2019-01-26 | SV-Zanshin | Issue #4 - converted documentation to doxygen |
| 1.0.4 | 2018-07-22 | SV-Zanshin | Corrected I2C Datatypes |
| 1.0.4 | 2018-07-08 | SV-Zanshin | Corrected and cleaned up c++ code formatting |
| 1.0.4 | 2018-07-02 | SV-Zanshin | Added guard code against multiple I2C Speed definitions |
| 1.0.4 | 2018-06-29 | SV-Zanshin | Issue #3 added support for faster I2C bus speeds |
| 1.0.2a | 2017-09-06 | SV-Zanshin | Added fillMemory() function as a template |
| 1.0.1 | 2017-09-06 | SV-Zanshin | Completed testing for large structures |
| 1.0.1b | 2017-09-06 | SV-Zanshin | Allow structures > 32 bytes, optimized memory use |
| 1.0.0b | 2017-09-04 | SV-Zanshin | Prepared for release, final testing |
| 1.0.0a | 2017-08-27 | SV-Zanshin | Started coding |

[MB85_FRAM.h:60-74]

### 21.2 MB85 family coverage declared by the library

The header comment says the library covers these MB85 parts: [MB85_FRAM.h:7-23]

| Part | Density / organization | ID information in header | Note from header |
|------|------------------------|--------------------------|------------------|
| MB85RC512T | 512 Kbit (64K x 8) | Manufacturer ID `0x00A`, Product ID `0x658`, Density `0x6` | Supported list |
| MB85RC256V | 256 Kbit (32K x 8) | Manufacturer ID `0x00A`, Product ID `0x510`, Density `0x5` | Supported list |
| MB85RC128A | 128 Kbit (16K x 8) | No Manufacturer ID / Product ID / Density values | Supported list |
| MB85RC64TA | 64 Kbit (8K x 8) | No Manufacturer ID / Product ID / Density values | `1.8 to 3.6V` |
| MB85RC64A | 64 Kbit (8K x 8) | No Manufacturer ID / Product ID / Density values | `2.7 to 3.6V` |
| MB85RC64V | 64 Kbit (8K x 8) | No Manufacturer ID / Product ID / Density values | `3.0 to 5.5V` |

The same header explicitly marks these parts as unsupported: [MB85_FRAM.h:18-23]

| Part | Density / organization | ID information in header | Unsupported note |
|------|------------------------|--------------------------|------------------|
| MB85RC1MT | 1 Mbit (128K x 8) | Manufacturer ID `0x00A`, Product ID `0x758`, Density `0x7` | `(unsp)` |
| MB85RC16 | 16 Kbit (2K x 8) | No Manufacturer ID / Product ID / Density values | `1 Address byte (unsp)` |
| MB85RC16V | 16 Kbit (2K x 8) | No Manufacturer ID / Product ID / Density values | `1 Address byte (unsp)` |
| MB85RC04V | 4 Kbit (512 x 8) | No Manufacturer ID / Product ID / Density values | `1 Address byte (unsp)` |

- The header says there is "no direct means of identifying the various chips" except the top three entries in its list, so the library uses a software method based on address wraparound. [MB85_FRAM.h:25-28]

### 21.3 Constants, limits, and class surface

The header defines these library-level constants and limits: [MB85_FRAM.h:86-99]

| Symbol | Value | Meaning in source |
|--------|-------|-------------------|
| `BUFFER_LENGTH` | `32` if not already defined | Default I2C buffer length |
| `I2C_STANDARD_MODE` | `100000` | Default normal I2C speed |
| `I2C_FAST_MODE` | `400000` | Fast mode |
| `I2C_FAST_MODE_PLUS_MODE` | `1000000` | "Really fast mode" |
| `I2C_HIGH_SPEED_MODE` | `3400000` | "Turbo mode" |
| `MB85_MIN_ADDRESS` | `0x50` | Minimum FRAM I2C address |
| `MB85_MAX_DEVICES` | `8` | Maximum number of FRAM devices |

Public API declared by the class: [MB85_FRAM.h:104-114, 130-235]

| Function | Declared behavior from comments and code |
|----------|------------------------------------------|
| `MB85_FRAM_Class()` | Constructor, currently empty and unused in `.cpp` |
| `~MB85_FRAM_Class()` | Destructor, currently empty and unused in `.cpp` |
| `begin(const uint32_t i2cSpeed = I2C_STANDARD_MODE)` | Starts I2C communications, scans devices, and auto-determines memory size |
| `totalBytes()` | Returns total memory available across all detected MB85 memories |
| `memSize(const uint8_t memNumber)` | Returns a selected memory size in bytes |
| `read(const uint32_t addr, T &value)` | Templated read for arbitrary `T`, including arrays and structures |
| `write(const uint32_t addr, const T &value)` | Templated write for arbitrary `T`, including arrays and structures |
| `fillMemory(const T &value)` | Templated helper that repeatedly writes copies of `value` until memory is filled |

#### Exact function signatures and object state

| Source signature | Notes from source |
|------------------|-------------------|
| `MB85_FRAM_Class();` | Constructor body is empty in `.cpp` |
| `~MB85_FRAM_Class();` | Destructor body is empty in `.cpp` |
| `uint8_t begin(const uint32_t i2cSpeed = I2C_STANDARD_MODE);` | Returns detected-device count |
| `uint32_t totalBytes();` | Returns `_TotalMemory` |
| `uint32_t memSize(const uint8_t memNumber);` | Returns `_I2C[memNumber] * 1024` or `0` |
| `template <typename T> uint8_t &read(const uint32_t addr, T &value)` | Returns a reference type, not a plain value |
| `template <typename T> uint8_t &write(const uint32_t addr, const T &value)` | Returns a reference type, not a plain value |
| `template <typename T> uint32_t &fillMemory(const T &value)` | Returns a reference type, not a plain value |
| `uint8_t getDevice(uint32_t &memAddress, uint32_t &endAddress);` | Private helper, mutates both reference arguments |
| `int8_t requestI2C(const uint8_t device, const uint32_t memAddress, const uint16_t dataSize, const bool endTrans);` | Private helper for addressed read/write setup |

[MB85_FRAM.h:110-114, 130-235]

- Object state is initialized by in-class member initializers: `_DeviceCount = 0`, `_TotalMemory = 0`, `_I2C[8] = {0}`, `_TransmissionStatus = false`. The constructor itself does not add any extra logic. [MB85_FRAM.h:236-239; MB85_FRAM.cpp:9-20]
- No global/singleton instance is declared in these two files; they only declare and define the class. [MB85_FRAM.h:104-240; MB85_FRAM.cpp:7-169]
- No register constants, register map definitions, bitfields, WP-control helpers, or Device-ID helper functions appear in these two files. [MB85_FRAM.h:77-240; MB85_FRAM.cpp:7-169]

### 21.4 Device discovery and initialization logic implemented by `begin()`

The `.cpp` implementation does the following when `begin()` is called: [MB85_FRAM.cpp:28-112]

1. Calls `Wire.begin()`.
2. Calls `Wire.setClock(i2cSpeed)`.
3. Scans all 7-bit addresses from `0x50` through `0x57`.
4. Treats `Wire.endTransmission() == 0` as "device present".
5. For each responding address, probes candidate capacities `8192`, `16384`, and `32768` bytes; the loop variable is `uint16_t`, so the next doubling overflows to `0` and terminates the loop. [MB85_FRAM.cpp:49]
6. For each candidate capacity, reads address `0x0000` into `minimumByte`.
7. Writes `0xFF` to address `0x0000`.
8. Reads the byte at the candidate address `memSize` into `maximumByte`.
9. Writes `0x00` to the candidate address `memSize`.
10. Reads address `0x0000` again into `newMinimumByte`.
11. Restores `maximumByte` back to the candidate address `memSize`.
12. If `newMinimumByte != 0xFF`, records the discovered size as `_I2C[i - MB85_MIN_ADDRESS] = memSize / 1024`, adds `memSize` to `_TotalMemory`, restores `minimumByte` to address `0x0000`, and stops probing larger sizes for that device.
13. After the probe loop, increments `_DeviceCount`.
14. Returns `_DeviceCount`.

- The implementation therefore uses live read/write transactions against the actual FRAM contents during discovery, not a Device-ID read command. [MB85_FRAM.cpp:46-111]
- The implemented loop body therefore reaches 8 KB, 16 KB, and 32 KB probe points. The header still lists MB85RC512T (64K x 8) as supported. [MB85_FRAM.h:12-23; MB85_FRAM.cpp:49-50]

#### Library-generated I2C transaction forms used by `begin()`

The code emits these transaction shapes during discovery: [MB85_FRAM.cpp:42-111]

| Purpose | Wire call sequence in source | Bytes / addressing detail |
|---------|------------------------------|---------------------------|
| Presence probe | `beginTransmission(i)` -> `endTransmission()` | No address bytes, just slave-address probe on `0x50`..`0x57` |
| Read address `0x0000` | `beginTransmission(i)` -> `write(0)` -> `write(0)` -> `endTransmission()` -> `requestFrom(i, 1)` -> `read()` | Two-byte address then 1-byte read |
| Write `0xFF` to `0x0000` | `beginTransmission(i)` -> `write(0)` -> `write(0)` -> `write(0xFF)` -> `endTransmission()` | Two-byte address plus one data byte |
| Read candidate address `memSize` | `beginTransmission(i)` -> `write(memSize >> 8)` -> `write((uint8_t)memSize)` -> `endTransmission()` -> `requestFrom(i, 1)` -> `read()` | For `32768`, this sends address bytes `0x80 0x00` |
| Write `0x00` to candidate address `memSize` | `beginTransmission(i)` -> `write(memSize >> 8)` -> `write((uint8_t)memSize)` -> `write(0x00)` -> `endTransmission()` | Two-byte address plus one data byte |
| Restore candidate-address byte | `beginTransmission(i)` -> `write(memSize >> 8)` -> `write((uint8_t)memSize)` -> `write(maximumByte)` -> `endTransmission()` | Restores the pre-probe byte at the candidate address |
| Restore address `0x0000` on detected size | `beginTransmission(i)` -> `write(0)` -> `write(0)` -> `write(minimumByte)` -> `endTransmission()` | Only executed in the detection branch |

- None of these reads use `Wire.endTransmission(false)`, so the source never explicitly requests a repeated-start sequence during probing. [MB85_FRAM.cpp:52-57, 67-72, 82-87]
- For each candidate size that is tested, the code performs 3 one-byte reads and 3 writes, plus a seventh write only when the detection branch is taken and address `0x0000` is restored. [MB85_FRAM.cpp:51-105]

### 21.5 Addressing, segmentation, and template access behavior

The header comments state that multiple memories are treated as one contiguous logical memory and that reads/writes wrap from the end of memory back to the beginning. The template implementations match that description. [MB85_FRAM.h:119-125, 167-173]

#### Physical slot mapping and logical-memory ordering

- The library does not store physical slave addresses directly. Instead, slot `0` corresponds to I2C address `0x50`, slot `1` to `0x51`, and so on up to slot `7` -> `0x57`, because `requestI2C()` always addresses `device + MB85_MIN_ADDRESS`. [MB85_FRAM.h:98-99, 233-235; MB85_FRAM.cpp:160-165]
- During detection, `_I2C[i - MB85_MIN_ADDRESS]` stores the discovered device capacity in kB for the responding slave address. A zero entry means no detected device in that slot. [MB85_FRAM.cpp:44-49, 96-99; MB85_FRAM.h:238]
- Logical memory concatenation is therefore keyed by ascending slot order `0`..`7`, which is the same as ascending I2C address order `0x50`..`0x57`. [MB85_FRAM.cpp:44-49, 122-130, 160-165]
- `memSize(memNumber)` indexes `_I2C[]` directly, so the `memNumber` parameter behaves like an address slot index, not a compact "nth detected device" index. [MB85_FRAM.cpp:98, 134-145]

#### `read()`

- `read()` stores a byte pointer to the destination object and uses `sizeof(T)` as `structSize`. [MB85_FRAM.h:131-133]
- The starting address is normalized with `addr % _TotalMemory`. [MB85_FRAM.h:134]
- `getDevice(memAddress, endAddress)` maps the logical address into a physical device index plus the last address in that device. [MB85_FRAM.h:135-136; MB85_FRAM.cpp:113-133]
- A new I2C read request is issued whenever `i % (BUFFER_LENGTH - 2) == 0`; with the default `BUFFER_LENGTH` of 32, this chunk boundary is 30 bytes. [MB85_FRAM.h:89-91, 137-140]
- On each chunk boundary the code calls `requestI2C(device, memAddress, structSize, true)`. [MB85_FRAM.h:138-139]
- Each byte is consumed with `Wire.read()` and copied into the destination object. [MB85_FRAM.h:141]
- If the current device end address is reached, the code walks forward through up to `MB85_MAX_DEVICES` entries, wraps the device index back to `0` when needed, selects the next populated `_I2C[device]`, issues `requestI2C(device, 0, structSize, true)`, and continues reading from address `0` of that device. [MB85_FRAM.h:142-156]
- The function returns `structSize`. [MB85_FRAM.h:158]
- `requestI2C(..., true)` always performs `beginTransmission` -> two `write()` calls for the memory address -> `endTransmission()` -> `requestFrom()`. No repeated-start form is explicitly requested by the library. [MB85_FRAM.cpp:160-166]
- The amount requested from `Wire.requestFrom()` is `structSize`, not the remaining byte count and not `BUFFER_LENGTH - 2`. [MB85_FRAM.h:138-139; MB85_FRAM.cpp:165]
- The return value from `requestI2C()` is ignored by `read()`, and `read()` does not check `Wire.available()` before each `Wire.read()`. [MB85_FRAM.h:138-141; MB85_FRAM.cpp:165-166]

#### `write()`

- `write()` also normalizes the starting address with `addr % _TotalMemory` and maps it through `getDevice()`. [MB85_FRAM.h:178-182]
- A new write-positioning transaction is started whenever `i % (BUFFER_LENGTH - 2) == 0`. [MB85_FRAM.h:183-189]
- At nonzero chunk boundaries, the current I2C transmission is explicitly closed with `Wire.endTransmission()`. [MB85_FRAM.h:184-187]
- `requestI2C(device, memAddress, structSize, false)` sends the 2-byte memory address and leaves the transmission open so payload bytes can follow. [MB85_FRAM.h:188-190; MB85_FRAM.cpp:160-168]
- Payload bytes are written one by one with `Wire.write(*bytePtr++)`. [MB85_FRAM.h:190]
- When the end of the current device is reached, the code ends the transmission, advances to the next populated device with wraparound, repositions to address `0`, and continues writing. [MB85_FRAM.h:191-205]
- A final `Wire.endTransmission()` is always issued at the end of the function. [MB85_FRAM.h:208]
- The function returns `structSize`. [MB85_FRAM.h:209]
- On the write path, the `dataSize` argument to `requestI2C()` is not used for I2C transfer sizing; it is only returned by the helper when `endTrans` is `false`. [MB85_FRAM.cpp:146-168]

#### `fillMemory()`

- `fillMemory()` computes `structSize = sizeof(T)` and loops `i` from `0` to `_TotalMemory / structSize - 1`. [MB85_FRAM.h:224-227]
- Each iteration writes one copy of `value` at `i * structSize`. [MB85_FRAM.h:226-227]
- The header comment says any leftover bytes are left untouched if total memory is not divisible by the data length. [MB85_FRAM.h:215-223]
- The function returns `i` after the loop. [MB85_FRAM.h:225-229]

### 21.6 Internal state and helper functions

Private state stored by the class: [MB85_FRAM.h:232-239]

| Member | Type | Meaning in source |
|--------|------|-------------------|
| `_DeviceCount` | `uint8_t` | Number of memories found |
| `_TotalMemory` | `uint32_t` | Total bytes across detected memories |
| `_I2C[MB85_MAX_DEVICES]` | `uint8_t[8]` | List of per-device capacities in kB |
| `_TransmissionStatus` | `bool` | I2C communication status |

Helper behavior implemented in `.cpp`:

- `totalBytes()` returns `_TotalMemory` directly. [MB85_FRAM.cpp:21-27]
- `getDevice(uint32_t &memAddress, uint32_t &endAddress)` starts `endAddress` at `UINT32_MAX`, then walks populated `_I2C[]` entries, accumulating each device size in bytes until the target logical address falls within the current device; `memAddress` is reduced by the sizes of any earlier devices. [MB85_FRAM.cpp:113-133]
- `memSize(memNumber)` returns `_I2C[memNumber] * 1024` when `memNumber <= _DeviceCount`, otherwise `0`. [MB85_FRAM.cpp:134-145]
- `requestI2C(device, memAddr, dataSize, endTrans)` always sends the 2-byte memory address first. If `endTrans` is `true`, it closes the transmission, calls `Wire.requestFrom(device + MB85_MIN_ADDRESS, dataSize)`, and returns `Wire.available()`. If `endTrans` is `false`, it leaves the transmission open and returns `dataSize`. [MB85_FRAM.cpp:146-169]

#### Features present vs not implemented in these files

Present in source:

- Auto-scan of slave addresses `0x50`..`0x57` with capacity probing. [MB85_FRAM.cpp:42-111]
- Multi-device logical-memory concept via `_I2C[]`, `_TotalMemory`, and `getDevice()`. [MB85_FRAM.h:236-239; MB85_FRAM.cpp:113-133]
- Generic templated read, write, and full-memory fill helpers. [MB85_FRAM.h:130-230]
- User-selectable I2C clock input to `begin()`, plus named speed constants in the header. [MB85_FRAM.h:93-96, 112; MB85_FRAM.cpp:42-43]

Not implemented in these two files:

- No Device-ID read helper despite the header listing Manufacturer ID / Product ID values for some parts. [MB85_FRAM.h:12-23, 110-239; MB85_FRAM.cpp:28-169]
- No explicit Current Address Read helper. [MB85_FRAM.h:110-239; MB85_FRAM.cpp:21-169]
- No explicit Sequential Read / Page Write API with caller-supplied byte count; all public transfers are mediated through templated `T`. [MB85_FRAM.h:110-239]
- No write-protect control API, address-pin helper API, or power/reset recovery helper API. [MB85_FRAM.h:110-239; MB85_FRAM.cpp:21-169]
- No public getter for `_DeviceCount`, `_TransmissionStatus`, or the detected slave-address list. [MB85_FRAM.h:232-239]

### 21.7 Direct code-level caveats and mismatches captured from the source

These are direct observations from the shipped source, plus a short set of explicit source-vs-official-documentation mismatches where sibling-part datasheets expose a gap:

1. `begin()` does not clear `_DeviceCount`, `_TotalMemory`, or `_I2C[]` before rescanning, so repeated calls reuse existing state and then add more results on top of it. [MB85_FRAM.h:236-239; MB85_FRAM.cpp:42-111]
2. `_DeviceCount` is incremented for every responding I2C address after the probe loop, even if no candidate capacity was recorded into `_I2C[]`. [MB85_FRAM.cpp:46-49, 96-108]
3. The `begin()` comment describes the probe differently from the implementation: the comment says address `0` is overwritten with `0x00` and wraparound is identified if address `0` becomes `0xFF`, but the actual code writes `0xFF` to address `0`, writes `0x00` to the candidate address, and detects a match when `newMinimumByte != 0xFF`. [MB85_FRAM.cpp:31-38, 59-79, 81-106]
4. For probe sizes that do not trigger detection, the code does not restore address `0x0000` before trying the next candidate size; the restore of `minimumByte` to address `0x0000` only occurs inside the detection branch. [MB85_FRAM.cpp:51-57, 59-64, 81-87, 96-105]
5. `read()` and `write()` store `sizeof(T)` in `static uint8_t structSize`, and `fillMemory()` stores `sizeof(T)` in `uint8_t structSize`; the size-carrying variables in these templates are therefore 8-bit. [MB85_FRAM.h:133, 179, 224]
6. `read()` and `write()` return `uint8_t &`, while `fillMemory()` returns `uint32_t &`; in `fillMemory()`, the returned expression is the local variable `i`. [MB85_FRAM.h:131, 162, 213, 225-229]
7. `read()` and `write()` both compute `addr % _TotalMemory` with no guard for `_TotalMemory == 0`. [MB85_FRAM.h:134, 180]
8. `memSize()` uses `if (memNumber <= _DeviceCount)` rather than `< _DeviceCount` for its range check. [MB85_FRAM.cpp:140-141]
9. `requestI2C()` returns `int8_t`, even though its return values are `Wire.available()` or `dataSize`. [MB85_FRAM.cpp:146-168]
10. The header exposes `I2C_HIGH_SPEED_MODE = 3400000`, while the MB85RC256V datasheet processed elsewhere in this extraction only specifies Standard, Fast, and Fast Mode Plus operation up to 1 MHz. [MB85_FRAM.h:93-96; datasheet_MB85RC256V_v2.pdf, p12]
11. When the probe loop tests `memSize = 32768`, it sends address `0x8000`; for MB85RC256V, the datasheet section earlier in this extraction states that the most significant bit of the high address byte is supposed to be `0`, so this probe relies on behavior outside the documented normal address format. [MB85_FRAM.cpp:67-78; datasheet_MB85RC256V_v2.pdf, p7]
12. In the `read()` and `write()` chip-to-chip wrap logic, `if (device++ == MB85_MAX_DEVICES) device = 0;` increments `device` before `_I2C[device]` is checked. If the current device index is `7`, the next `_I2C[device]` access is `_I2C[8]`. [MB85_FRAM.h:144-151, 193-201]
13. `getDevice()` subtracts earlier device sizes from `memAddress`, making `memAddress` chip-local, but `endAddress` remains an accumulated total across slots. Its output therefore does not match the comment "end of memory chip" for devices beyond the first slot. [MB85_FRAM.cpp:115-129]
14. After `read()` or `write()` switches to the next device at a boundary, the code sets `memAddress = 0` but does not recompute `endAddress` for the newly selected device. Subsequent boundary detection therefore keeps using the old `endAddress` value. [MB85_FRAM.h:142-156, 191-205]
15. The combination of item 13 and item 14 means multi-chip wrap behavior is only correct in special cases where the stale `endAddress` value happens to match the local end of the active device. This is an inference from the implementation flow above. [MB85_FRAM.h:135-156, 180-205; MB85_FRAM.cpp:113-133]
16. In `read()` and `write()`, `structSize` is stored in `uint8_t`, so `sizeof(T)` is truncated modulo 256 before the loop count and request size are used. [MB85_FRAM.h:133, 179]
17. The header comment claims the library "will automatically split bigger reads into multiple calls," but `read()` passes the full truncated `structSize` to `requestI2C()` each time rather than the remaining/chunk size. [MB85_FRAM.h:122-124, 138-139; MB85_FRAM.cpp:165]
18. `_TransmissionStatus` is assigned in `begin()`, `write()`, and `requestI2C()`, but there is no public method to retrieve it and most call sites do not branch on its value. [MB85_FRAM.h:186, 208, 239; MB85_FRAM.cpp:55, 64, 70, 79, 85, 94, 104, 164]
19. The header's MB85RC64TA row says there are no Manufacturer ID / Product ID values, but the official MB85RC64TA datasheet defines the Device-ID command and lists Manufacture ID `00AH`, Product ID `358H`. [MB85_FRAM.h:15; MB85RC64TA-DS5v1-E.pdf, p11]
20. The header's MB85RC04V row says there are no Manufacturer ID / Product ID values, but the official MB85RC04V datasheet defines the Device-ID command and lists Manufacture ID `00AH`, Product ID `010H`. [MB85_FRAM.h:22-23; MB85RC04V-DS5v1-E.pdf, p8]
21. Exposing `I2C_HIGH_SPEED_MODE = 3400000` is not by itself a complete High Speed mode implementation for the HS-capable sibling parts. Their official datasheets require an entry command `0000 1XXX`, expect NACK on that entry command, and exit High Speed mode on Stop; the library contains no code that emits this sequence. [MB85_FRAM.h:93-96; MB85_FRAM.cpp:42-43; MB85RC512T-DS6v1-E.pdf, p8; MB85RC64TA-DS5v1-E.pdf, p9; MB85RC1MT-DS5v1-E.pdf, p8]
22. The library's fixed transport model is narrower than the official MB85RC family. `requestI2C()` always sends two memory-address bytes and the scan assumes `0x50`..`0x57` are eight distinct devices, but official sibling datasheets show valid variants where those bits represent A16 or upper memory address bits instead of pure device-select pins. This makes the current helper logic incompatible with the documented MB85RC1MT / MB85RC16V / MB85RC04V addressing models. [MB85_FRAM.cpp:42-49, 146-168; MB85RC1MT-DS5v1-E.pdf, pp4-7; MB85RC16V-DS11v0-E.pdf, pp4-7; MB85RC04V-DS5v1-E.pdf, pp4-8]

---

## 22. Family-derived gap fills from other official MB85RC sources

This section extends the MB85RC256V-centric extraction with official RAMXEED sibling-part sources only. It is intended to fill family-level gaps in the current manual and in the assumptions visible in `MB85_FRAM.h` / `MB85_FRAM.cpp`, not to redefine MB85RC256V behavior. [MB85RC512T-DS6v1-E.pdf; MB85RC64TA-DS5v1-E.pdf; MB85RC1MT-DS5v1-E.pdf; MB85RC16V-DS11v0-E.pdf; MB85RC04V-DS5v1-E.pdf; ramxeed_i1_wide_voltage_i2c_family.html; ramxeed_i3_i2c_family.html]

### 22.1 Cross-family protocol pattern

- Across the checked MB85RC512T, MB85RC64TA, MB85RC1MT, MB85RC16V, and MB85RC04V datasheets, the interface remains transaction-oriented FeRAM access rather than a programmable register file. The checked sources describe device-address words, memory-address fields, data transfers, Device-ID reads where present, software-reset / command-retry sequences, and on some parts High Speed / Sleep commands, but no writable configuration/status register block. [MB85RC512T-DS6v1-E.pdf, pp4-10; MB85RC64TA-DS5v1-E.pdf, pp4-12; MB85RC1MT-DS5v1-E.pdf, pp4-10; MB85RC16V-DS11v0-E.pdf, pp4-8; MB85RC04V-DS5v1-E.pdf, pp4-9]
- The official family pages also show a meaningful split that is not captured by the local library surface: the `i3` family page groups MB85RC256V / MB85RC256VN / MB85RC128A / MB85RC64A / MB85RC64V / MB85RC16 / MB85RC16V / MB85RC04 / MB85RC04V as up-to-1-MHz parts with `10^12` cycle endurance, while the wide-voltage `i1` family page groups MB85RC1MT / MB85RC512T / MB85RC64TA as 1-MHz-plus-3.4-MHz parts with `10^13` cycle endurance. [ramxeed_i3_i2c_family.html, lines 11-45; ramxeed_i1_wide_voltage_i2c_family.html, lines 11-45]

### 22.2 Addressing-model comparison relevant to the local library

| Official part | Addressing structure from vendor docs | Library-compatibility implication |
|---------------|---------------------------------------|-----------------------------------|
| MB85RC256V | `1010 + A2/A1/A0 + R/W`, then 2 memory-address bytes; classic 8-device / 2-byte-address member of the family. [datasheet_MB85RC256V_v2.pdf, pp5-8] | This is the model the current library is hard-wired around. |
| MB85RC512T | `1010 + A2/A1/A0 + R/W`, then 2 memory-address bytes; up to 8 physical devices. [MB85RC512T-DS6v1-E.pdf, pp4-8] | Transaction format matches `requestI2C()`, but the probe loop still cannot identify a 64 KB part because it only tests 8 KB, 16 KB, and 32 KB candidates. [MB85_FRAM.cpp:49-50] |
| MB85RC64TA | `1010 + A2/A1/A0 + R/W`, then 2 memory-address bytes; up to 8 physical devices. [MB85RC64TA-DS5v1-E.pdf, pp4-5] | This part is address-compatible with the library's transport helper, but the header metadata understates its identification support because official Device-ID data exists. [MB85_FRAM.h:15; MB85RC64TA-DS5v1-E.pdf, p11] |
| MB85RC1MT | `1010 + A2/A1 + A16 + R/W`, then 2 memory-address bytes; current-address buffer is 17 bits. [MB85RC1MT-DS5v1-E.pdf, pp4, 7] | Not compatible with the library's fixed `0x50`..`0x57` scan and 2-byte helper alone, because the most-significant memory bit A16 must be carried in the device-address word. |
| MB85RC16V | `1010 + A2/A1/A0 + R/W`, but those three bits are memory upper address bits, not slave-select pins; one lower address byte follows; current-address buffer is 11 bits. [MB85RC16V-DS11v0-E.pdf, pp4-7] | Not compatible with the library's assumption that `0x50`..`0x57` are eight distinct devices and that every request sends two address bytes. |
| MB85RC04V | `1010 + A2/A1 + A8 + R/W`; A2/A1 are device-select pins, A8 is the upper memory bit; one lower address byte follows; current-address buffer is 9 bits. [MB85RC04V-DS5v1-E.pdf, p4] | Not compatible with the library's eight-slot model because part of the scanned address space is memory selection, not physical-device selection. |

- Inference from the documented device-address-word layouts above: MB85RC256V / MB85RC512T / MB85RC64TA use the normal 7-bit slave-address span `0x50`..`0x57` as true physical-device selection; MB85RC1MT still occupies numeric addresses in that same span, but for a fixed A2/A1 pin setting one physical chip should ACK two of them because A16 is encoded in the address word. [datasheet_MB85RC256V_v2.pdf, p5; MB85RC512T-DS6v1-E.pdf, p4; MB85RC64TA-DS5v1-E.pdf, p4; MB85RC1MT-DS5v1-E.pdf, p4]
- Inference from the official MB85RC16V address-word definition: one physical MB85RC16V can legitimately ACK the full `0x50`..`0x57` address span, because those three address bits are documented as memory upper address bits rather than external chip-select pins. [MB85RC16V-DS11v0-E.pdf, pp4-7]
- Inference from the official MB85RC04V address-word definition: one physical MB85RC04V can ACK two different 7-bit slave addresses for the same A2/A1 pin setting, because A8 is carried in the device-address word. [MB85RC04V-DS5v1-E.pdf, p4]

### 22.3 Device-ID and optional-command coverage that the local library leaves unused

- Official Device-ID coverage across the checked family is broader than the library header comment suggests. The checked sources explicitly document Device ID for MB85RC512T (`00AH` / `658H`), MB85RC256V (`00AH` / `510H`), MB85RC64TA (`00AH` / `358H`), MB85RC1MT (`00AH` / `758H`), and MB85RC04V (`00AH` / `010H`). [MB85RC512T-DS6v1-E.pdf, p10; datasheet_MB85RC256V_v2.pdf, p9; MB85RC64TA-DS5v1-E.pdf, p11; MB85RC1MT-DS5v1-E.pdf, p10; MB85RC04V-DS5v1-E.pdf, p8]
- MB85RC512T, MB85RC64TA, and MB85RC1MT officially support High Speed mode up to 3.4 MHz, but their datasheets describe this as a protocol state entered by the master-code sequence `0000 1XXX`, followed by an expected NACK, with Stop returning the bus to normal mode. Merely calling `Wire.setClock(3400000)` does not implement that documented entry procedure. [MB85RC512T-DS6v1-E.pdf, p8; MB85RC64TA-DS5v1-E.pdf, p9; MB85RC1MT-DS5v1-E.pdf, p8; MB85_FRAM.cpp:42-43]
- MB85RC512T, MB85RC64TA, and MB85RC1MT also add an official Sleep mode that is entered with `START + F8H`, device address word, repeated `START + 86H`, and exited by another device address followed by a recovery interval `tREC = 400 us`. No helper for Sleep entry/exit or `tREC` handling exists in the local library. [MB85RC512T-DS6v1-E.pdf, pp9, 16; MB85RC64TA-DS5v1-E.pdf, pp10, 17; MB85RC1MT-DS5v1-E.pdf, pp9, 16; MB85_FRAM.h:110-239; MB85_FRAM.cpp:21-169]

### 22.4 Gap-fill conclusions for this manual

- The current manual was already correct to say "no register map" for MB85RC256V. The additional sourcing shows that the missing family-level detail was not hidden registers, but rather address-word variants, Device-ID coverage, and optional wide-voltage-family commands.
- The current library is best understood as a partially generic front-end for the subset of MB85RC parts that look like MB85RC256V/MB85RC64TA/MB85RC512T at the transaction level. Even within that subset, the current wraparound-based probe logic still does not scale up to the 64 KB MB85RC512T. [MB85_FRAM.cpp:49-50]
- Any future "whole MB85RC family" implementation would need variant-aware transport formatting: external device-select bits vs upper-memory bits, one-byte vs two-byte memory addresses, Device-ID helpers, and optional High Speed / Sleep sequences where the official datasheets provide them. This is an implementation conclusion derived from the sourced comparison above.

### 22.5 Implementation-facing code and field map

This subsection collapses the checked family data into explicit bus-level codes and fields. These are protocol fields and reserved command bytes, not internal device registers. [datasheet_MB85RC256V_v2.pdf, pp5, 9-10; MB85RC512T-DS6v1-E.pdf, pp4, 8-11; MB85RC64TA-DS5v1-E.pdf, pp4, 9-12; MB85RC1MT-DS5v1-E.pdf, pp4, 8-11; MB85RC16V-DS11v0-E.pdf, pp4, 8; MB85RC04V-DS5v1-E.pdf, pp4, 8-9]

| Item | Code / field | Where documented in checked family | Meaning for implementation |
|------|--------------|------------------------------------|----------------------------|
| Device type code | `1010` | MB85RC256V / MB85RC512T / MB85RC64TA / MB85RC1MT / MB85RC16V / MB85RC04V [datasheet_MB85RC256V_v2.pdf, p5; MB85RC512T-DS6v1-E.pdf, p4; MB85RC64TA-DS5v1-E.pdf, p4; MB85RC1MT-DS5v1-E.pdf, p4; MB85RC16V-DS11v0-E.pdf, p4; MB85RC04V-DS5v1-E.pdf, p4] | Upper four bits of the normal device address word across all checked parts. |
| External device-select bits | `A2:A1:A0` | MB85RC256V / MB85RC512T / MB85RC64TA [datasheet_MB85RC256V_v2.pdf, p5; MB85RC512T-DS6v1-E.pdf, p4; MB85RC64TA-DS5v1-E.pdf, p4] | True physical-device addressing; supports up to 8 devices on the bus. |
| Device-select plus memory-upper mix | `A2:A1 + A8` | MB85RC04V [MB85RC04V-DS5v1-E.pdf, p4] | Only `A2:A1` are physical chip-select bits; `A8` is part of the memory address. |
| Memory-upper bits in device word | `A2:A1:A0` | MB85RC16V [MB85RC16V-DS11v0-E.pdf, p4] | These three bits are not slave-select pins; they are memory address bits A10:A8. |
| Most-significant memory bit in device word | `A16` | MB85RC1MT [MB85RC1MT-DS5v1-E.pdf, pp4, 7] | 1Mbit variant requires variant-aware device-word composition for accesses and current-address reads. |
| Normal read/write selector | `R/W` | All checked parts [datasheet_MB85RC256V_v2.pdf, p5; MB85RC512T-DS6v1-E.pdf, p4; MB85RC64TA-DS5v1-E.pdf, p4; MB85RC1MT-DS5v1-E.pdf, p4; MB85RC16V-DS11v0-E.pdf, p4; MB85RC04V-DS5v1-E.pdf, p4] | `0 = write`, `1 = read`. |
| Device-ID reserved slave ID, write phase | `F8H` | MB85RC256V / MB85RC512T / MB85RC64TA / MB85RC1MT / MB85RC04V [datasheet_MB85RC256V_v2.pdf, p9; MB85RC512T-DS6v1-E.pdf, p10; MB85RC64TA-DS5v1-E.pdf, p11; MB85RC1MT-DS5v1-E.pdf, p10; MB85RC04V-DS5v1-E.pdf, p8] | First reserved address used to start the Device-ID sequence where supported. |
| Device-ID reserved slave ID, read phase | `F9H` | MB85RC256V / MB85RC512T / MB85RC64TA / MB85RC1MT / MB85RC04V [datasheet_MB85RC256V_v2.pdf, p9; MB85RC512T-DS6v1-E.pdf, p10; MB85RC64TA-DS5v1-E.pdf, p11; MB85RC1MT-DS5v1-E.pdf, p10; MB85RC04V-DS5v1-E.pdf, p8] | Second reserved address used to clock out the 3-byte Device ID. |
| High Speed entry command | `0000 1XXX` | MB85RC512T / MB85RC64TA / MB85RC1MT [MB85RC512T-DS6v1-E.pdf, p8; MB85RC64TA-DS5v1-E.pdf, p9; MB85RC1MT-DS5v1-E.pdf, p8] | Must be sent after Start to enter 3.4 MHz mode; datasheets state the slave side keeps NACKing this entry command. |
| Sleep transition start | `F8H` | MB85RC512T / MB85RC64TA / MB85RC1MT [MB85RC512T-DS6v1-E.pdf, p9; MB85RC64TA-DS5v1-E.pdf, p10; MB85RC1MT-DS5v1-E.pdf, p9] | First byte in the Sleep-entry sequence. |
| Sleep transition second command | `86H` | MB85RC512T / MB85RC64TA / MB85RC1MT [MB85RC512T-DS6v1-E.pdf, p9; MB85RC64TA-DS5v1-E.pdf, p10; MB85RC1MT-DS5v1-E.pdf, p9] | Sent after repeated Start to place the part into Sleep mode. |
| Software reset / recovery pattern | `9 × (Start + data "1")` | MB85RC256V / MB85RC512T / MB85RC64TA / MB85RC1MT / MB85RC16V / MB85RC04V [datasheet_MB85RC256V_v2.pdf, p10; MB85RC512T-DS6v1-E.pdf, p11; MB85RC64TA-DS5v1-E.pdf, p12; MB85RC1MT-DS5v1-E.pdf, p11; MB85RC16V-DS11v0-E.pdf, p8; MB85RC04V-DS5v1-E.pdf, p9] | Official bus-recovery sequence; no dedicated register or opcode is defined for it. |

### 22.6 Per-part command/support matrix from checked sources

| Part | Byte/Page write | Current/Random/Sequential read | Device ID | High Speed mode | Sleep mode | Software reset / command retry | Address bytes after device word |
|------|-----------------|--------------------------------|-----------|-----------------|------------|--------------------------------|---------------------------------|
| MB85RC256V | Yes [datasheet_MB85RC256V_v2.pdf, p7] | Yes [datasheet_MB85RC256V_v2.pdf, pp8-9] | Yes [datasheet_MB85RC256V_v2.pdf, p9] | No checked support beyond 1 MHz/Fm+ [datasheet_MB85RC256V_v2.pdf, p12] | Not documented in checked 256V source | Yes [datasheet_MB85RC256V_v2.pdf, p10] | 2 |
| MB85RC512T | Yes [MB85RC512T-DS6v1-E.pdf, pp6-7] | Yes [MB85RC512T-DS6v1-E.pdf, pp7-8] | Yes [MB85RC512T-DS6v1-E.pdf, p10] | Yes [MB85RC512T-DS6v1-E.pdf, p8] | Yes [MB85RC512T-DS6v1-E.pdf, p9] | Yes [MB85RC512T-DS6v1-E.pdf, p11] | 2 |
| MB85RC64TA | Yes [MB85RC64TA-DS5v1-E.pdf, pp6-7] | Yes [MB85RC64TA-DS5v1-E.pdf, pp7-9] | Yes [MB85RC64TA-DS5v1-E.pdf, p11] | Yes [MB85RC64TA-DS5v1-E.pdf, p9] | Yes [MB85RC64TA-DS5v1-E.pdf, p10] | Yes [MB85RC64TA-DS5v1-E.pdf, p12] | 2 |
| MB85RC1MT | Yes [MB85RC1MT-DS5v1-E.pdf, pp6-7] | Yes [MB85RC1MT-DS5v1-E.pdf, pp7-8] | Yes [MB85RC1MT-DS5v1-E.pdf, p10] | Yes [MB85RC1MT-DS5v1-E.pdf, p8] | Yes [MB85RC1MT-DS5v1-E.pdf, p9] | Yes [MB85RC1MT-DS5v1-E.pdf, p11] | 2, plus A16 in device word |
| MB85RC16V | Yes [MB85RC16V-DS11v0-E.pdf, p6] | Yes [MB85RC16V-DS11v0-E.pdf, pp7-8] | Not found in checked 16V datasheet | Not found in checked 16V datasheet | Not found in checked 16V datasheet | Yes [MB85RC16V-DS11v0-E.pdf, p8] | 1, plus A10:A8 in device word |
| MB85RC04V | Yes [MB85RC04V-DS5v1-E.pdf, p6] | Yes [MB85RC04V-DS5v1-E.pdf, pp7-8] | Yes [MB85RC04V-DS5v1-E.pdf, p8] | Not found in checked 04V datasheet | Not found in checked 04V datasheet | Yes [MB85RC04V-DS5v1-E.pdf, p9] | 1, plus A8 in device word |

Cells phrased as "Not found in checked ... datasheet" are negative results from this manual's official-source audit set; they are not claims about every historical revision or third-party summary.
