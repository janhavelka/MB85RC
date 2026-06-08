# Protocol Commands And Transactions

The MB85RC family uses normal I2C memory transactions: start, slave address, one or more memory-address bytes, data, ACK/NACK, and stop/repeated-start. Source: variant datasheets, I2C and command sections.

Common transaction shapes:

| Operation | Bus sequence | Source |
|---|---|---|
| Byte write / sequential write | START, slave address with write bit, memory address, one or more data bytes, STOP. | 256V datasheet, pp. 6-7 |
| Current-address read | START, slave address with read bit, read current pointer byte(s), NACK final byte, STOP. | 256V datasheet, p. 7 |
| Random read | Dummy write sets memory address, repeated START, slave address with read bit, read data. | 256V datasheet, p. 7 |
| Sequential read | Random/current read followed by additional ACKed bytes; final byte is NACKed before STOP. | 256V datasheet, p. 8 |
| High-speed-prefixed transfer | High-speed capable T/TA/1MT parts use `START, 0000 1XXX, expected NACK, repeated START, normal memory command`; STOP exits HS state. | 64TA datasheet, p. 9; 512T datasheet, p. 9; 1MT datasheet, p. 9 |
| Sleep entry | `START, F8h, ACK, active device address word, ACK, repeated START, 86h, ACK, STOP` for HS/Sleep-capable variants. | 64TA datasheet, p. 10; 512T datasheet, p. 10; 1MT datasheet, p. 10 |
| Sleep wake | Send a START plus active device address word; recovery starts on the 9th clock, and normal access resumes after tREC. | 64TA datasheet, p. 10; 512T datasheet, p. 10; 1MT datasheet, p. 10 |

Driver notes:

- Prefer explicit random reads for public `read(address, length)` APIs so behavior does not depend on the previous internal address pointer.
- Support repeated-start reads because the datasheets describe random read through an address-setting write followed by a read phase. Source: 256V datasheet, p. 7.
- Do not add EEPROM-style ACK polling after writes unless a specific adapter needs bus-level retry for unrelated I2C errors; the devices advertise no polling sequence after writing. Source: variant datasheets, p. 1.
- Treat the HS master-code NACK as expected only inside the special HS prefix. Generic NACKs from normal memory, Device ID, or Sleep commands remain transport failures.
- The core driver requests HS/Sleep special operations through injected transport and does not change the MCU I2C clock or block for Sleep tREC.
