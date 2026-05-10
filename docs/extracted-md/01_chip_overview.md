# Chip Overview

MB85RC devices are nonvolatile FeRAM memories with a two-wire I2C interface. They retain data without a backup battery and do not need EEPROM-style write polling after memory writes. Source: MB85RC04V datasheet, p. 1; MB85RC256V datasheet, p. 1.

| Variant | Organization | Max bus rate | Supply range | Endurance | Retention statement | Source |
|---|---:|---:|---:|---:|---|---|
| MB85RC04V | 512 x 8 | 1 MHz | 3.0 V to 5.5 V | 10^12 writes/byte | 10 years at +85 degC, 95 years at +55 degC, over 200 years at +35 degC | 04V datasheet, p. 1 |
| MB85RC16V | 2,048 x 8 | 1 MHz | 3.0 V to 5.5 V | 10^12 writes/byte | Same family retention statement | 16V datasheet, p. 1 |
| MB85RC256V | 32,768 x 8 | 1 MHz | 2.7 V to 5.5 V | 10^12 writes/byte | 10 years at +85 degC, 95 years at +55 degC, over 200 years at +35 degC | 256V datasheet, p. 1 |
| MB85RC64TA | 8,192 x 8 | 3.4 MHz high-speed mode | 1.8 V to 3.6 V | 10^13 writes/byte | 19.1 years at +105 degC, 70.4 years at +85 degC | 64TA datasheet, p. 1 |
| MB85RC512T | 65,536 x 8 | 3.4 MHz high-speed mode | 1.7 V to 3.6 V | 10^13 writes/byte | 10 years at +85 degC, 95 years at +55 degC | 512T datasheet, p. 1 |
| MB85RC1MT | 131,072 x 8 | 3.4 MHz high-speed mode | 1.8 V to 3.6 V | 10^13 writes/byte | 10 years at +85 degC, 95 years at +55 degC, over 200 years at +35 degC | 1MT datasheet, p. 1 |

Software-visible behavior:

- Memory behaves like byte-addressable nonvolatile RAM over I2C.
- There is no page-write delay to poll in the normal EEPROM sense; writes complete through the I2C transaction flow. Source: variant datasheets, p. 1.
- Variant differences mostly affect memory size, address-byte handling, legal I2C speed, supply range, and address-pin count.
