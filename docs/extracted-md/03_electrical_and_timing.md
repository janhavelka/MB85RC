# Electrical And Timing

The variant matrix gives the legal supply and bus-rate limits; the covered MB85RC parts do not all share one maximum bus speed or voltage range.

| Variant | Max normal I2C speed | High-speed mode | Supply | Source |
|---|---:|---:|---:|---|
| MB85RC04V | 1 MHz | Not listed | 3.0 V to 5.5 V | datasheet, p. 1 |
| MB85RC16V | 1 MHz | Not listed | 3.0 V to 5.5 V | datasheet, p. 1 |
| MB85RC256V | 1 MHz | Not listed | 2.7 V to 5.5 V | datasheet, p. 1 |
| MB85RC64TA | 1 MHz normal, 3.4 MHz high-speed | Entry command required | 1.8 V to 3.6 V | datasheet, pp. 1, 9 |
| MB85RC512T | 1 MHz normal, 3.4 MHz high-speed | Entry command required | 1.7 V to 3.6 V | datasheet, pp. 1, 9 |
| MB85RC1MT | 1 MHz normal, 3.4 MHz high-speed | Entry command required | 1.8 V to 3.6 V | datasheet, pp. 1, 9 |

Electrical notes:

- SDA is open-drain and requires an external pull-up. Source: 256V datasheet, p. 2.
- WP is internally pulled down; leaving it open enables writes. Source: 256V datasheet, p. 2.
- The standard operating ambient temperature range for 04V/16V/256V/512T/1MT is -40 degC to +85 degC; MB85RC64TA retention data is specified up to +105 degC in the feature summary. Source: variant datasheets, p. 1.
- FeRAM endurance is much higher than EEPROM and there is no post-write busy polling sequence in the feature description. Source: variant datasheets, p. 1.
