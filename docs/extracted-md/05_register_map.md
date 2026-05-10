# Register Map

These parts are linear memories, not register-file peripherals. The relevant map is the valid byte-address range for the selected variant and the number of address bits/bytes used by that variant.

| Variant | Addressable bytes | Last byte address | Device-address capacity | Source |
|---|---:|---:|---:|---|
| MB85RC04V | 512 | `0x01FF` | Up to 4 devices | 04V datasheet, pp. 1-2 |
| MB85RC16V | 2,048 | `0x07FF` | Addressing described in device-address section | 16V datasheet, pp. 1-2 |
| MB85RC256V | 32,768 | `0x7FFF` | Up to 8 devices | 256V datasheet, pp. 1-2 |
| MB85RC64TA | 8,192 | `0x1FFF` | Up to 8 devices | 64TA datasheet, pp. 1-2 |
| MB85RC512T | 65,536 | `0xFFFF` | Up to 8 devices | 512T datasheet, pp. 1-2 |
| MB85RC1MT | 131,072 | `0x1FFFF` | Up to 4 devices | 1MT datasheet, pp. 1-2 |

Addressing cautions:

- Small-density devices may encode high address bits in the I2C device address rather than sending the same address-byte count as larger parts. The selected variant's device-address table determines the transaction formatter.
- Reads that reach the end of address space wrap according to the sequential-read description in the datasheets. Source: variant datasheets, read-operation sections.
- WP is a pin-level protection for the whole memory array; it is not a register bit. Source: 256V datasheet, p. 6.
