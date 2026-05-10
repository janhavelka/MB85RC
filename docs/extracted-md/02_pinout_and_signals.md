# Pinout And Signals

The variants use the same basic 8-pin memory shape: supply, ground, SDA, SCL, write protect, and address-selection pins or NC pins depending on density. Source: variant datasheets, p. 2.

| Pin/signal | Role | Driver or board note | Source |
|---|---|---|---|
| `VDD` | Supply | Observe per-variant voltage range. | variant datasheets, p. 1 |
| `VSS` | Ground | Common reference. | variant datasheets, p. 2 |
| `SDA` | I2C serial data | Bidirectional open-drain data; external pull-up required. | 256V datasheet, p. 2 |
| `SCL` | I2C serial clock | Input clock; data sampled on rising edge and output on falling edge. | 256V datasheet, p. 2 |
| `WP` | Write protect | High disables writes to the entire memory array; low enables writes. Internally pulled down, so open means write-enabled on listed variants. Reads remain enabled. | 256V datasheet, pp. 2, 6 |
| `A0-A2` / `A1-A2` | Device address pins | Tie to VDD or VSS. Open address pins are internally pulled down and read as low on documented variants. | 04V datasheet, p. 2; 256V datasheet, p. 2 |
| `NC` | No connect | Some small-density variants use NC where larger variants use an address pin. | 04V datasheet, p. 2 |

Address-pin availability changes by variant. MB85RC04V exposes A1/A2 and supports up to four devices on one bus; MB85RC256V, MB85RC512T, and MB85RC64TA expose A0/A1/A2 and support up to eight devices. Source: 04V datasheet, p. 2; 256V datasheet, p. 2; 512T datasheet, p. 2; 64TA datasheet, p. 2.
