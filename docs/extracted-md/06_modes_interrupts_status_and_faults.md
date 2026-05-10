# Modes, Interrupts, Status, And Faults

The MB85RC memories do not have interrupt pins or status registers. Software-visible failure state is limited to I2C ACK/NACK, bus errors, selected variant limits, and the WP pin's external state when known.

| Condition | Expected behavior | Source |
|---|---|---|
| WP high | Writes disabled across the entire memory; reads still enabled. | 256V datasheet, pp. 2, 6 |
| WP low or open | Writes enabled because WP is internally pulled down on documented variants. | 256V datasheet, pp. 2, 6 |
| Address mismatch | Device does not participate in the transaction. | device-address sections |
| End of sequential read | Read address wraps at the end of address space. | read-operation sections |
| High-speed mode unsupported | 04V/16V/256V feature summaries list 1 MHz max and do not document 3.4 MHz entry. | variant datasheets, p. 1 |
| High-speed mode supported | 64TA/512T/1MT support 3.4 MHz after high-speed entry. | 64TA, 512T, 1MT datasheets, high-speed sections |

Expose failures as transport errors or invalid-argument errors rather than inventing device status bits. If the application needs to know whether WP is asserted, that must come from board configuration or an external GPIO, not from the FeRAM over I2C.
