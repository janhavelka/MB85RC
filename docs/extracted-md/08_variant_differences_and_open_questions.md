# Variants And Open Questions

Variant differences that affect code:

| Area | Difference | Source |
|---|---|---|
| Density | 512 bytes through 131,072 bytes across covered variants. | variant datasheets, p. 1 |
| Address pins | 04V supports up to 4 devices; 256V/64TA/512T support up to 8; 1MT supports up to 4. | variant datasheets, p. 2 |
| Speed | 04V/16V/256V list 1 MHz max; 64TA/512T/1MT list 3.4 MHz high-speed mode. | variant datasheets, pp. 1, high-speed sections |
| Voltage | Ranges span 1.7 V to 5.5 V depending on variant. | variant datasheets, p. 1 |
| Endurance | 10^12 writes/byte on older/lower-speed variants; 10^13 writes/byte on T/TA/1MT variants. | variant datasheets, p. 1 |

Not documented in PDFs / repository policy choices:

- The PDFs do not define which variants this repository must treat as first-class API targets.
- The PDFs define per-variant address behavior, but do not prescribe a software table format.
- The PDFs document high-speed mode only for 64TA/512T/1MT among the covered sources; repository exposure policy is not specified by the PDFs.
- The PDFs document wrap behavior at the memory boundary, but do not define whether a public API should reject operations crossing the last valid address.

Raw extraction remains in `docs/pdf-extracted-md` for page-level verification.
