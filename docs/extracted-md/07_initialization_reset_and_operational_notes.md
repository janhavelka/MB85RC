# Initialization, Reset, And Operational Notes

Basic MB85RC bring-up:

1. Select the exact variant profile: density, supply range, address pins, address-byte format, and supported maximum bus speed.
2. Configure the I2C target address from the board's A pins and the variant's address table.
3. Keep WP low for write tests or treat writes as fallible if WP may be high externally.
4. For a presence check, perform a short read or write-safe read transaction within the valid memory range.
5. For public reads, use random-read transactions rather than relying on current-address state.

Sources: variant datasheets, pin/function and read/write-operation sections.

Operational notes:

- FeRAM permits frequent writes; operations that cross the last valid address will encounter the datasheet's wrap behavior unless the repository API rejects them earlier.
- The datasheets' feature sections explicitly contrast these devices with Flash/EEPROM by saying no polling sequence is needed after writes. Source: variant datasheets, p. 1.
- High-speed capable variants need the documented entry command before 3.4 MHz operation. The driver can request HS-prefixed transfers through the optional special transport callback, but the application bus manager still owns the MCU I2C clock and must validate 3.4 MHz hardware operation. Source: 64TA/512T/1MT datasheets, high-speed sections.
- Sleep-capable variants need the documented `F8h` + device-address + repeated-start `86h` entry sequence. Wake recovery starts on the 9th wake clock and requires tREC 400 us before normal access; the core records the state contract and does not insert a hidden delay. Source: 64TA/512T/1MT datasheets, Sleep sections.
- Repository constraint: core code remains transport-agnostic and uses injected I2C read/write callbacks.
