# NP501-00019-2v0-E

- Source PDF: `MB85RC256V-Fact-Sheet-NP501-00019-2v0-E.pdf`
- Extraction tool: pdfplumber
- Page count: 2
- SHA256: `721075c230e024e7c87f0af1f24e68af2048d54ab0d5a1d6def4cdd4bdb34aa7`

## Page 1

FUJITSU SEMICONDUCTOR
FACT SHEET
NP501-00019-2v0-E
FRAM
MB85RC256V
MB85RC256V is a 256K-bits FRAM with serial interface (I2C), using the ferroelectric
process and CMOS process technologies for forming the nonvolatile memory cells.
Because FRAM is able to write high-speed even though a nonvolatile memory,
it is suitable for the log management and the storage of the resume data, etc.
■ FEATURES
- Bit configuration :32,768 words × 8 bits
- Two-wire serial interface :Fully controllable by two ports through serial clock (SCL) and serial data (SDA).
- Operating frequency :1 MHz (Max.)
- Read/write endurance :1012 times / byte
- Data retention :10 years ( + 85 °C), 95 years ( + 55 °C), over 200 years ( + 35 °C)
- Operating power supply voltage :2.7V to 5.5V
- Low power consumption :Operating current 200μA (Max. @1MHz),
:Standby current 27μA (Typ.)
- Operation ambient temperature range:− 40 °C to + 85 °C
- Package :8-pin plastic SOP (FPT-8P-M02)
:8-pin plastic SOP (FPT-8P-M08)
RoHS compliant
■ ORDERING INFORMATION
Product name Package Shipping form
MB85RC256VPNF-G-JNE1 8-pin plastic SOP Tube
(FPT-8P-M02)
MB85RC256VPNF-G-JNERE1 3.90mm×5.05mm,1.27mm pitch Embossed Carrier tape
MB85RC256VPF-G-JNE2 8-pin plastic SOP Tube
(FPT-8P-M08)
MB85RC256VPF-G-JNERE2 5.30mm×5.24mm,1.27mm pitch Embossed Carrier tape
■ PACKAGE EXAMPLE
8-pin plastic SOP 8-pin plastic SOP
(FPT-8P-M02) (FPT-8P-M08)
2013.5 1/2
Copyright©2012-2013 FUJITSU SEMICONDUCTOR LIMITED All rights reserved

## Page 2

MB85RC256V
■PIN ASSIGNMENT
(TOP VIEW)
Pin No. Pin name Description
Device Address pins
MB85RC256V can be connected to the same data bus up to 8 devices. Device addresses are
used in order to identify each of these devices. Connect these pins to VDD pin or VSS pin
1 to 3 A0 to A2
externally. Only if the combination of VDD and VSS pin matches a Device Address Code
inputted from the SDA pin, the device operates. In the open pin state, A0, A1 and A2 are
internally pulled-down and recognized as the "L" level.
4 VSS Ground pin
Serial Data I/O pin
5 SDA
This is an I/O pin which performs bidirectional communication for both memory address and
(FPT-8P-M02) writing/reading data. It is possible to connect multiple devices. It is an open drain output, so a
pull-up resistor is required to be connected to the external circuit.
Serial Clock pin
6 SCL This is a clock input pin for input/output serial data. Data is sampled on the rising edge of the
clock and output on the falling edge.
Write Protect pin
When Write Protect pin is the "H" level, writing operation is disabled. When Write Protect pin
is the "L" level, the entire memory region can be overwritten. Reading operation is always
7 WP
enabled regardless of Write Protect pin input level. The Write Protect pin is internally pulled
down to VSS pin, and that is recognized as the "L" level (write enabled) when the pin is the
open state.
(FPT-8P-M08)
8 VDD Supply Voltage pin
■BLOCK DIAGRAM
■I2C
The MB85RC256V has the two-wire serial interface; the I2C bus, and operates as a slave device.
The I2C bus defines communication roles of “master” and “slave” devices, with the master side
holding the authority to initiate control. Furthermore, the I2C bus connection is possible
where a single master device is connected to multiple slave devices in a party-line configuration.
In this case, it is necessary to assign a unique device address to the slave device, the master side
starts communication after specifying the slave to communicate by addresses.
NP501-00019-2v0-E
2013.5 2/2
Copyright©2012-2013 FUJITSU SEMICONDUCTOR LIMITED All rights reserved
