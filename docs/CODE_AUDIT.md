# Remaining audit notes

## Additional datasheet details

- MB85RC64TA, MB85RC512T, and MB85RC1MT enter Sleep after acknowledging
  command byte `86h`; the transport must check that ACK.
- The high memory-address byte masks are `0x1F` for MB85RC64TA, `0x7F` for
  MB85RC256V, and `0xFF` for MB85RC512T and MB85RC1MT. The latter's A16 is
  carried separately in the slave address.

## Remaining work

- Print formatting and the diagnostic demo suites remain separate in the two
  CLIs. Sharing those is a later refactor beyond the pure-helper extraction;
  differences in scratch ranges and stress limits still need explicit review.
- Further documentation consolidation can reduce repeated operation-budget
  explanations in README and repeated integration contracts across references.
- Board-specific High-speed timing, Sleep current, and electrical recovery
  behavior require physical qualification for each applicable device/fixture.
