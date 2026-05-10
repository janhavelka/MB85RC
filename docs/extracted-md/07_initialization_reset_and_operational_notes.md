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
- High-speed capable variants need the documented entry command before 3.4 MHz operation; keep default operation at a conservative supported bus speed unless the user selects high-speed mode. Source: 64TA/512T/1MT datasheets, high-speed sections.
- Repository constraint: core code remains transport-agnostic and uses injected I2C read/write callbacks.
