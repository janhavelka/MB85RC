# MB85RC256V-DS11v2-E.pdf

- Source PDF: `MB85RC256V-Data-Sheet-DS501-00017-11v2-E.pdf`
- Extraction tool: pdfplumber
- Page count: 39
- SHA256: `cf559b4702e2785bb6d50452ea463505ef40e542025e14a3d4c606af0e350831`

## Page 1

DS501-00017-11v2-E
Memory FeRAM
256 K (32 K x 8) Bit I2C
MB85RC256V
 DESCRIPTION
The MB85RC256V is an FeRAM (Ferroelectric Random Access Memory) chip in a configuration of 32,768
words x 8 bits, using the ferroelectric process and silicon gate CMOS process technologies for forming the
nonvolatile memory cells.
Unlike SRAM, the MB85RC256V is able to retain data without using a data backup battery.
The read/write endurance of the nonvolatile memory cells used for the MB85RC256V has improved to be
at least 1012 cycles, significantly outperforming other nonvolatile memory products in the number.
The MB85RC256V does not need a polling sequence after writing to the memory such as the case of Flash
memory or E2PROM.
 FEATURES
• Bit configuration : 32,768 words x 8 bits
• Two-wire serial interface : Fully controllable by two ports: serial clock (SCL) and serial data (SDA).
• Operating frequency : 1 MHz (Max)
• Read/write endurance : 1012 times / byte
• Data retention : 10 years ( + 85 degC), 95 years ( + 55 degC), over 200 years ( + 35 degC)
• Operating power supply voltage: 2.7 V to 5.5 V
• Low-power consumption : Operating power supply current 200 uA (Max @1 MHz)
Standby current 27 uA (Typ)
• Operation ambient temperature range
: - 40 degC to + 85 degC
• Package : 8-pin plastic SOP (150mil)
8-pin plastic SOP (208mil)
Both are RoHS compliant
Fujitsu Semiconductor Memory Solutions Limited has changed its name to RAMXEED Limited.
RAMXEED Limited will continue to offer and support existing products while maintaining Fujitsu's
part number unchanged.
Copyright 2024 RAMXEED LIMITED

## Page 2

MB85RC256V
 PIN ASSIGNMENT
(TOP VIEW) (TOP VIEW)
A0 1 8 VDD A0 1 8 VDD
A1 2 7 WP A1 2 7 WP
A2 3 6 SCL A2 3 6 SCL
VSS 4 5 SDA VSS 4 5 SDA
(8-pin plastic SOP,150mil) (8-pin plastic SOP, 208mil)
 PIN FUNCTIONAL DESCRIPTIONS
Pin
Pin Name Functional Description
Number
Device Address pins
The MB85RC256V can be connected to the same data bus up to 8 devices.
Device addresses are used in order to identify each of these devices. Connect
1 to 3 A0 to A2 these pins to VDD pin or VSS pin externally. Only if the combination of VDD and
VSS pins matches Device Address Code inputted from the SDA pin, the device
operates. In the open pin state, A0, A1 and A2 pins are internally pulled-down
and recognized as the "L" level.
4 VSS Ground pin
Serial Data I/O pin
This is an I/O pin which performs bidirectional communication for both memory
5 SDA address and writing/reading data. It is possible to connect multiple devices. It is
an open drain output, so a pull-up resistor is required to be connected to the ex-
ternal circuit.
Serial Clock pin
6 SCL This is a clock input pin for input/output serial data. Data is sampled on the ris-
ing edge of the clock and output on the falling edge.
Write Protect pin
When the Write Protect pin is the “H” level, the writing operation is disabled.
When the Write Protect pin is the “L” level, the entire memory region can be
7 WP overwritten. The reading operation is always enabled regardless of the Write
Protect pin input level. The Write Protect pin is internally pulled down to VSS
pin, and that is recognized as the “L” level (write enabled) when the pin is the
open state.
8 VDD Supply Voltage pin
2 DS501-00017-11v2-E

## Page 3

MB85RC256V
 BLOCK DIAGRAM
SDA
SCL
WP
A0, A1, A2
 I2C (Inter-Integrated Circuit)
The MB85RC256V has the two-wire serial interface; the I2C bus, and operates as a slave device.
The I2C bus defines communication roles of “master” and “slave” devices, with the master side holding the
authority to initiate control. Furthermore, the I2C bus connection is possible where a single master device is
connected to multiple slave devices in a party-line configuration. In this case, it is necessary to assign a
unique device address to the slave device, the master side starts communication after specifying the slave
to communicate by addresses.
- I2C Interface System Configuration Example
DS501-00017-11v2-E 3
retnuoC
sserddA
redoceD
woR
Serial/Parallel Converter
FeRAM Array
32,768 x 8
Column Decoder/Sense Amp/
Write Amp
tiucriC
lortnoC
VDD
Pull-up
Resistors
SCL
SDA
...
I2C Bus I2C Bus I2C Bus I2C Bus
Master MB85RC256V MB85RC256V MB85RC256V
A2 A1 A0 A2 A1 A0 A2 A1 A0
0 0 0 0 0 1 0 1 0
Device address

## Page 4

MB85RC256V
 I2C COMMUNICATION PROTOCOL
The I2C bus is a two wire serial interface that uses a bidirectional data bus (SDA) and serial clock (SCL). A
data transfer can only be initiated by the master, which will also provide the serial clock for synchronization.
The SDA signal should change while the SCL is the “L” level. However, as an exception, when starting and
stopping communication sequence, the SDA is allowed to change while the SCL is the “H” level.
• Start Condition
To start read or write operations by the I2C bus, change the SDA input from the “H” level to the “L” level while
the SCL input is in the “H” level.
• Stop Condition
To stop the I2C bus communication, change the SDA input from the “L” level to the “H” level while the SCL
input is in the “H” level. In the reading operation, inputting the stop condition finishes reading and enters the
standby state. In the writing operation, inputting the stop condition finishes inputting the rewrite data and
enters the standby state.
- Start Condition, Stop Condition
SCL
SDA
H or L
Start Stop
Note : At the write operation, the FeRAM device does not need the programming wait time (tWC) after issuing
the Stop Condition.
 ACKNOWLEDGE (ACK)
In the I2C bus, serial data including memory address or memory information is sent and received in units of
8 bits. The acknowledge signal indicates that every 8 bits of the data is successfully sent and received. The
receiver side usually outputs the “L” level every time on the 9th SCL clock after each 8 bits are successfully
transmitted and received. On the transmitter side, the bus is temporarily released to Hi-Z every time on this
9th clock to allow the acknowledge signal to be received and checked. During this Hi-Z released period, the
receiver side pulls the SDA line down to indicate the “L” level that the previous 8 bits communication is
successfully received.
In case the slave side receives Stop condition before sending or receiving the ACK “L” level, the slave side
stops the operation and enters to the standby state. On the other hand, the slave side releases the bus state
after sending or receiving the NACK “H” level. The master side generates Stop condition or Start condition
in this released bus state.
- Acknowledge timing overview diagram
SCL 1 2 3 8 9
SDA ACK
The transmitter side should always release SDA on the 9th bit.
At this time, the receiver side outputs a pull-down if the previ-
Start ous 8 bits data are received correctly (ACK response).
4 DS501-00017-11v2-E

## Page 5

MB85RC256V
 DEVICE ADDRESS WORD (Slave address)
Following the start condition, the master inputs the 8 bits device address word to start I2C communication.
The device address word (8 bits) consists of a device Type code (4 bits), device address code (3 bits), and
a read/write code (1 bit).
• Device Type Code (4 bits)
The upper 4 bits of the device address word are a device type code that identifies the device type, and are
fixed at “1010” for the MB85RC256V.
• Device Address Code (3 bits)
Following the device type code, the 3 bits of the device address code are input in order of A2, A1 and A0.
The device address code identifies one device from up to eight devices connected to the bus.
Each MB85RC256V is given a unique 3 bits code on the device address pin (external hardware pin A2, A1
and A0). The slave only responds if the received device address code is equal to this unique 3 bits code.
• Read/Write Code (1 bit)
The 8th bit of the device address word is the R/W (read/write) code. When the R/W code is “0”, a write
operation is enabled, and the R/W code is “1”, a read operation is enabled for the MB85RC256V.
It turns to a stand-by state if the device code is not “1010” or device address code does not equal to pins
A2, A1 and A0.
- Device Address Word
Start 1 2 3 4 5 6 7 8 9 1 2
. .
SCL
SDA ACK
. .
S 1 0 1 0 A2 A1 A0 R/W A
Device Code Device address Read/Write Code
Code
Access from master
Access from slave
S Start Condition
A ACK (SDA is the "L" level)
DS501-00017-11v2-E 5

## Page 6

MB85RC256V
 DATA STRUCTURE
In the I2C bus, the acknowledge “L” level is output on the 9th bit by a slave, after the 8 bits of the device
address word following the start condition are input by a master. After confirming the acknowledge response
by the master, the master outputs 8 bits x 2 memory address to the slave. When the each memory address
input ends, the slave again outputs the acknowledge “L” level. After this operation, the I/O data follows in
units of 8 bits, with the acknowledge “L” level output after every 8 bits.
It is determined by the R/W code whether the data line is driven by the master or the slave. However, the
clock line shall be driven by the master. For a write operation, the slave will accept 8 bits from the master,
then send an acknowledge. If the master detects the acknowledge, the master will transfer the next 8 bits.
For a read operation, the slave will place 8 bits on the data line, then wait for an acknowledge from the master.
 FeRAM ACKNOWLEDGE -- POLLING NOT REQUIRED
The MB85RC256V performs the high speed write operations, so any waiting time for an ACK polling* does
not occur.
*: In E2PROM, the Acknowledge Polling is performed as a progress check whether rewriting is executed or
not. It is normal to judge by the 9th bit of Acknowledge whether rewriting is performed or not after inputting
the start condition and then the device address word (8 bits) during rewriting.
 WRITE PROTECT (WP)
The entire memory array can be write protected using the Write Protect pin. When the Write Protect pin is
set to the “H” level, the entire memory array will be write protected. When the Write Protect pin is the “L”
level, the entire memory array will be rewritten. Reading is allowed regardless of the WP pin's “H” level or
“L” level.
Note : The Write Protect pin is pulled down internally to the VSS pin, therefore if the Write Protect pin is open,
the pin status is detected as the “L” level (write enabled).
6 DS501-00017-11v2-E

## Page 7

MB85RC256V
 COMMAND
• Byte Write
If the device address word (R/W “0” input) is sent following the start condition, the slave responds with an
ACK. After this ACK, write addresses and data are sent in the same way, and the write ends by generating
a stop condition at the end.
Address Address Write
S 1 0 1 0 A2 A1 A0 0 A A A A P
High 8bits Low 8bits Data 8bits
0 X X X X X X X X X X X X X X X
Access from master
MSB LSB
Access from slave
S Start Condition
P Stop Condition
A ACK (SDA is the "L" level)
Note : In the MB85RC256V, input “0” to the most significant bit of the higher address byte because the address
is expressed with 15 bits.
• Page Write
If additional 8 bits are continuously sent after the same command (except stop condition) as Byte Write, a
page write is performed. The memory address rolls over to first memory address (0000 ) at the end of the
H
address. Therefore, if more than 32 Kbytes are sent, the data is overwritten in order starting from the start
of the memory address that was written first. Because FeRAM performs the high-speed write operations,
the data will be written to FeRAM right after the ACK response finished.
Address Address Write Write ...
S 1 0 1 0 A2 A1 A0 0 A A A A A P
High 8bits Low 8bits Data 8bits Data
Access from master
Access from slave
S Start Condition
P Stop Condition
A ACK (SDA is the "L" level)
Note: It is not necessary to take a period for internal write operation cycles from the buffer to the memory after
the stop condition is generated.
DS501-00017-11v2-E 7

## Page 8

MB85RC256V
• Current Address Read
When the previous write or read operation finishes successfully up to the stop condition and assumes the
last accessed address is “n”, then the address at “n+1” is read by sending the following command unless
turning the power off. If the memory address is last address, the address counter will roll over to 0000 . The
H
current address in memory address buffer is undefined immediately after the power is turned on.
Access from master
Access from slave
(n+1) address
Read S Start Condition
S 1 0 1 0 A2 A1 A0 1 A N P
Data 8bits
P Stop Condition
A ACK (SDA is the "L" level)
N NACK (SDA is the "H" level)
• Random Read
The one byte of data from the memory address saved in the memory address buffer can be read out
synchronously to SCL by specifying the address in the same way as for a write, and then issuing another
start condition and sending the Device Address Word (R/W “1” input).
The final NACK (SDA is the "H" level) is issued by the receiver that receives the data. In this case, this bit
is issued by the master side.
Address Address Read
S 1 0 1 0 A2 A1 A0 0 A A A S 1 0 1 0 A2 A1 A0 1 A N P
High 8bits Low 8bits Data 8bits
Access from master
Access from slave
S Start Condition
P Stop Condition
A ACK (SDA is the "L" level)
N NACK (SDA is the "H" level)
8 DS501-00017-11v2-E

## Page 9

MB85RC256V
• Sequential Read
Data can be received continuously following the Device address word (R/W “1” input) after specifying the
address in the same way as for Random Read. If the read reaches the end of address, the internal read
address automatically rolls over to first memory address 0000 and keeps reading.
H
... Read Read ... Read
A A A N P
Data 8bits Data Data 8bits
Access from master
Access from slave
P Stop Condition
A ACK (SDA is the "L" level)
N NACK (SDA is the "H" level)
• Device ID
The Device ID command reads fixed Device ID. The size of Device ID is 3 bytes and consists of manufacturer
ID and product ID. The Device ID is read-only and can be read out by following sequences.
a) The master sends the Reserved Slave ID F8H after the START condition.
b) The master sends the device address word after the ACK response from the slave.
In this device address word, R/W code are “Don't care” value.
c) The master re-sends the START condition followed by the Reserved Slave ID F9H after the ACK response
from the slave.
d) The master read out the Device ID succeedingly in order of Data Byte 1st / 2nd / 3rd after the ACK
response from the slave.
e) The master responds the NACK (SDA is the “H” level) after reading 3 bytes of the Device ID.
In case the master respond the ACK after reading 3 bytes of the Device ID, the master re-reading the
Device ID from the 1st byte.
Reserved Reserved
R
Data Byte Data Byte Data Byte
S Slave ID A 1 0 1 0 A2 A1 A0 / A S Slave ID A A A N P
W 1st 2nd 3rd
(F8 ) (F9 )
H H
Access from master
Access from slave
S Start Condition
P Stop Condition
A ACK (SDA is the "L" level)
N NACK (SDA is the "H" level)
Data Byte 1st Data Byte 2nd Data Byte 3rd
Manufacture ID = 00A Product ID = 510
H H
11 10 9 8 7 6 5 4 3 2 1 0 11 10 9 8 7 6 5 4 3 2 1 0
Density = 5 Proprietary use
H
0 0 0 0 0 0 0 0 1 0 1 0 0 1 0 1 0 0 0 1 0 0 0 0
DS501-00017-11v2-E 9

## Page 10

MB85RC256V
 SOFTWARE RESET SEQUENCE OR COMMAND RETRY
In case the malfunction has occurred after power on, the master side stopped the I2C communication during
processing, or unexpected malfunction has occurred, execute the following (1) software recovery sequence
just before each command, or (2) retry command just after failure of each command.
(1) Software Reset Sequence
Since the slave side may be outputting “L” level, do not force to drive “H” level, when the master side drives
the SDA port. This is for preventing a bus conflict. The additional hardware is not necessary for this software
reset sequence.
9 set of “Start Conditions and one “1” data”
SCL
SDA
Hi-Z state by pull up Resistor
Send “Start Condition and one data “1”” .
Repeat these 9 times just before Write or Read command.
(2) Command Retry
Command retry is useful to recover from failure response during I2C communication.
10 DS501-00017-11v2-E

## Page 11

MB85RC256V
 ABSOLUTE MAXIMUM RATINGS
Rating
Parameter Symbol Unit
Min Max
Power supply voltage* VDD - 0.5 +6.0 V
Input voltage* VIN - 0.5 VDD + 0.5 ( <= 6.0) V
Output voltage* VOUT - 0.5 VDD + 0.5 ( <= 6.0) V
Operation ambient temperature TA - 40 + 85 degC
Storage temperature Tstg - 55 + 125 degC
*: These parameters are based on the condition that VSS is 0 V.
WARNING: Semiconductor devices can be permanently damaged by application of stress (voltage, current,
temperature, etc.) in excess of absolute maximum ratings. Do not exceed these ratings.
 RECOMMENDED OPERATING CONDITIONS
Value
Parameter Symbol Unit
Min Typ Max
Power supply voltage*1 VDD 2.7  5.5 V
Operation ambient temperature*2 TA - 40  + 85 degC
*1: These parameters are based on the condition that VSS is 0 V.
*2: Ambient temperature when only this device is working. Please consider it to be the almost same as the
package surface temperature.
WARNING: The recommended operating conditions are required in order to ensure the normal operation of
the semiconductor device. All of the device's electrical characteristics are warranted when the
device is operated within these ranges.
Always use semiconductor devices within their recommended operating condition ranges.
Operation outside these ranges may adversely affect reliability and could result in device failure.
No warranty is made with respect to uses, operating conditions, or combinations not represented
on the data sheet. Users considering application outside the listed conditions are advised to contact
their representatives beforehand.
DS501-00017-11v2-E 11

## Page 12

MB85RC256V
 ELECTRICAL CHARACTERISTICS
1. DC Characteristics
(within recommended operating conditions)
Value
Parameter Symbol Condition Unit
Min Typ Max
Input leakage current*1 |ILI| VIN  0 V to VDD   1 uA
Output leakage current*2 |ILO| VOUT  0 V to VDD   1 uA
Operating power supply SCL  400 kHz  75 130 uA
IDD
current SCL  1000 kHz  140 200 uA
SCL, SDA  VDD
A0,A1,A2,WP  0 V or 27 56
Standby current ISB
VDD or Open

TA  + 25 degC TA  + 85 degC
uA
Under Stop Condition
“H” level input voltage VIH VDD  2.7 V to 5.5 V VDD x 0.8  5.5 V
“L” level input voltage VIL VDD  2.7 V to 5.5 V VSS  VDD x 0.2 V
“L” level output voltage VOL IOL  3 mA   0.4 V
Input resistance for VIN  VIL (Max) 50   k
RIN
WP, A0, A1, and A2 pins VIN  VIH (Min) 1   M
*1: Applicable pin: SCL,SDA *2: Applicable pin: SDA
2. AC Characteristics
Value
STANDARD FAST MODE
Parameter Symbol FAST MODE Unit
MODE PLUS
Min Max Min Max Min Max
SCL clock frequency FSCL 0 100 0 400 0 1000 kHz
Clock high time THIGH 4000  600  400  ns
Clock low time TLOW 4700  1300  600  ns
SCL/SDA rising time Tr  1000  300  300 ns
SCL/SDA falling time Tf  300  300  100 ns
Start condition hold THD:STA 4000  600  250  ns
Start condition setup TSU:STA 4700  600  250  ns
SDA input hold THD:DAT 0  0  0  ns
SDA input setup TSU:DAT 250  100  100  ns
SDA output hold TDH:DAT 0  0  0  ns
Stop condition setup TSU:STO 4000  600  250  ns
SDA output access after SCL falling TAA  3000  900  550 ns
Pre-charge time TBUF 4700  1300  500  ns
Noise suppression time
TSP  50  50  50 ns
(SCL and SDA)
AC characteristics were measured under the following measurement conditions.
Power supply voltage : 2.7 V to 5.5 V
Operation ambient temperature : - 40 degC to + 85 degC
Input voltage magnitude : VDD x 0.2 to VDD x 0.8
Input rising time : 5 ns, Input judge level : VDD/2
Input falling time : 5 ns, Output judge level : VDD/2
12 DS501-00017-11v2-E

## Page 13

MB85RC256V
3. AC Timing Definitions
TSU:DAT THD:DAT
SCL
VIH VIH VIH VIH VIH
VIL Start VIL VIL VIL VIL Stop
VIH VIH VIH VIH
SDA
VIL VIL VIL VIL
TSU:STA THD:STA T
SU:STO
Tr Tf
THIGH TLOW
SCL
VIH VIH VIH VIH
Stop Start
VIL VIL VIL VIL
VIH VIH VIH VIH
SDA
VIL VIL VIL VIL
TBUF
TA
T
A
r T
DH:DAT
Tf Tsp
VIH
SCL
VIL VIL
VIH VIH
SDA Valid
VIL VIL VIL
1/FSCL
4. Pin Capacitance
Value
Parameter Symbol Conditions Unit
Min Typ Max
I/O capacitance CI/O VDD  5.0V,   15 pF
VIN  VOUT  0 V ~ VDD,
Input capacitance CIN
f  1 MHz, TA  + 25 degC
  15 pF
5. AC Test Load Circuit
VDD
1.8 kΩ
Output
100 pF
DS501-00017-11v2-E 13

## Page 14

MB85RC256V
 POWER ON/OFF SEQUENCE
tpd tf tr tpu
VDD VDD
2.7 V 2.7 V
VIH (Min) VIH (Min)
1.0 V 1.0 V
VIL (Max) VIL (Max)
0 V 0 V
SDA, SCL SDA, SCL > VDD × 0.8 * SDA, SCL : Don't care SDA, SCL > VDD × 0.8 * SDA, SCL
* : SDA, SCL (Max) < VDD + 0.5 V
Value
Parameter Symbol Unit Condition
Min Max
SDA, SCL level hold time during
tpd 85  ns 
power down
SDA, SCL level hold time during 85  ns VDD  5.0V  0.5V Operation
tpu
power up 0.5  ms VDD  3.3V  0.3V Operation
0.5 50 VDD  5.0V  0.5V Operation
Power supply rising time tr ms
0.005 50 VDD  3.3V  0.3V Operation
Power supply falling time tf 0.5 50 ms 
If the device does not operate within the specified conditions of read cycle, write cycle or power on/off
sequence, memory data can not be guaranteed.
 FeRAM CHARACTERISTICS
Item Min Max Unit Parameter
Read/Write Endurance*1 1012  Times/byte Operation Ambient Temperature TA  + 85 degC
10  Operation Ambient Temperature TA  + 85 degC
Data Retention*2 95  Years Operation Ambient Temperature TA  + 55 degC
>= 200  Operation Ambient Temperature TA  + 35 degC
*1 : Total number of reading and writing defines the minimum value of endurance, as an FeRAM memory
operates with destructive readout mechanism.
*2 : Minimum values define retention time of the first reading/writing data right after shipment, and these values
are calculated by qualification results.
14 DS501-00017-11v2-E

## Page 15

MB85RC256V
 NOTE ON USE
• We recommend programming of the device after reflow. Data written before reflow cannot be guaranteed.
• During the access period from the start condition to the stop condition, keep the level of WP, A0, A1, and
A2 pins to the “H” level or the “L” level.
 ESD AND LATCH-UP
Test DUT Value
ESD HBM (Human Body Model) MB85RC256VPNF-G-JNE1
>= |2000 V|
JESD22-A114 compliant MB85RC256VPNF-G-JNERE1
MB85RC256VPNF-G-AME2
ESD CDM (Charged Device Model)
MB85RC256VPNF-G-AMERE2 
JESD22-C101 compliant
MB85RC256VPF-G-BCE1
Latch-Up (C-V Method) MB85RC256VPF-G-BCERE1
>= |200 V|
Proprietary method
• C-V method of Latch-Up Resistance Test
Protection Resistor
A
Test
1 2 terminal VDD
SW DUT VDD
+ (Max.Rating)
VIN V
C
- 200pF VSS
Reference
terminal
Note : Charge voltage alternately switching 1 and 2 approximately 2 sec interval. This switching process is
considered as one cycle.
Repeat this process 5 times. However, if the latch-up condition occurs before completing 5times, this
test must be stopped immediately.
 REFLOW CONDITIONS AND FLOOR LIFE
[ JEDEC MSL ] : Moisture Sensitivity Level 3 (IPC/JEDEC J-STD-020E)
 CURRENT STATUS ON CONTAINED RESTRICTED SUBSTANCES
This product complies with the regulations of REACH Regulations, EU RoHS Directive and China RoHS.
DS501-00017-11v2-E 15

## Page 16

MB85RC256V
 ORDERING INFORMATION
Minimum shipping
Part number Package Shipping form
quantity
8-pin, plastic SOP
MB85RC256VPNF-G-JNE1 Tube *
(150mil)
8-pin, plastic SOP Embossed Carrier
MB85RC256VPNF-G-JNERE1 1500
(150mil) tape
8-pin, plastic SOP
MB85RC256VPNF-G-AME2 Tray *
(150mil)
8-pin, plastic SOP Embossed Carrier
MB85RC256VPNF-G-AMERE2 1500
(150mil) tape
8-pin, plastic SOP
MB85RC256VPF-G-BCE1 Tube *
(208mil)
8-pin, plastic SOP Embossed Carrier
MB85RC256VPF-G-BCERE1 500
(208mil) tape
*: Please contact our sales office about minimum shipping quantity.
16 DS501-00017-11v2-E

## Page 17

MB85RC256V
8-pin plastic SOP(150mil) Lead pitch 1.27mm
Lead shape Gullwing
Sealing method Plastic mold
Mounting height 1.75mm MAX.
8-pin plastic SOP
Dime(cid:15928)nsion in mm
DS501-00017-11v2-E 17
)04.1(
(cid:23)(cid:17)(cid:28)(cid:19)(cid:3)(cid:115)(cid:3)(cid:19)(cid:17)(cid:20)(cid:19)*
Details of “F”part
.XAM
57.1
Package width x
3.90mm x 4.90mm
Package length
Note *: These dimension do not include resin protrution.
Pins width do not include tie bar cutting remaindar.
“F”
S
0.10 S
.NIM
50.0
0.40 MIN.
(cid:24)(cid:21)(cid:17)(cid:19)(cid:15996)(cid:24)(cid:20)(cid:17)(cid:19)
8pin 5pin
1pin 4pin
*(cid:19)(cid:20)(cid:17)(cid:19)(cid:115)09.3
1.27 0.41±0.10
(cid:19)(cid:21)(cid:17)(cid:19)(cid:115)(cid:19)(cid:19)(cid:17)(cid:25)
 PACKAGE DIMENSION
(Continued)

## Page 18

MB85RC256V
(Continued)
8-pin plastic SOP (208mil) Lead pitch 1.27 mm
Package width ×
5.30 mm × 5.65 mm
package length
Lead shape Gullwing
Sealing method Plastic mold
Mounting heigth 1.73 mm MAX
8-pin plastic SOP (208mil)
Note *: These dimensions do not include resin protrution.
8pin 5pin
1pin 4pin
18 DS501-00017-11v2-E
)niM(54.1
)niM(01.0
0.4(Min)
Details of “A” part
*(cid:20)(cid:17)(cid:19)(cid:115)3.5
5.65±0.1*
452.0(cid:15996)302.0
1.27 0.42±0.1
)xaM(37.1
(cid:20)(cid:17)(cid:19)(cid:115)(cid:20)(cid:17)(cid:27)
Pins width and pins thickness include plating thickness.
Pins width not do ninclude tie bar cutting remaindar.
“A”
Dimenssions in mm

## Page 19

MB85RC256V
 MARKING(EXAMPLE)
[MB85RC256VPNF-G-JNE1]
[MB85RC256VPNF-G-JNERE1]
RC256V
E11900
300
[8-pin plastic SOP,150mil]
RC256V: Product Name
E11900: E1(Lead free code) + 1900(Year and Week code)
300: Trace code
[MB85RC256VPNF-G-AME2]
[MB85RC256VPNF-G-AMERE2]
R C 2 5 6 V
2 1 9 0 0
0 0 0
[8-pin plastic SOP,150mil]
RC256V: Product Name
21900: 2(Lead free code) + 1900(Year and Week code)
000: Trace code
DS501-00017-11v2-E 19

## Page 20

MB85RC256V
[MB85RC256VPF-G-BCE1]
[MB85RC256VPF-G-BCERE1]
C 2 5 6 V
1 1 9 0 0
V 0 0
[8-pin plastic SOP,208mil]
C256V: Product Name
11900: 1(CS code) + 1900(Year and Week code)
V00: Trace code
20 DS501-00017-11v2-E

## Page 21

MB85RC256V
 PACKING INFORMATION
(1)MB85RC256VPNF-G-JNE1
(Plastic SOP 8-pin, 150mil)
1. Tube
1.1 Tube Dimensions
• Tube/stopper shape (example)
Tube
Stopper
Tube cross-sections and Maximum quantity
Maximum quantity
MB85RC256VPNF-G-JNE1
ICs/tube ICs/inner box ICs/outer box
95 7,600 30,400
7.7
tube length:521
No heat resistance.
Package should not be baked by using tube.
(Dimensions in mm)
• Tube orientation
DS501-00017-11v2-E 21
8.3
Index mark

## Page 22

MB85RC256V
1.2 Product label indicators (example)
Label I: Label on Inner box/Moisture Barrier Bag/ (It sticks it on the reel for the emboss taping)
[C-3 Label (50mm x 100mm) Supplemental Label (20mm x 100mm)]
XXXXXXXXXXXXXX (Part number)
C-3 Label
(3N)1 XXXXXXXXXXXXXX XXX (LEAD FREE mark)
(Part number and quantity)
QC PASS
(3N)2 XXXXXXXXXX XXXXXX
(Control number bar code)
XXX pcs (Quantity)
XXXXXXXXXXXXXX (Part number)
(Part number bar code)
XXXX/XX/XX (Packed years/month/day) ASSEMBLED IN xxxx Perforated line
XXXXXXXXXXXXXX (Part number)
(Control number bar code) Supplemental Label
XX/XX XXXX-XXX XXX
(Package count) XXXX-XXX XXX
XXXXXXXXXX (Control number ) (Lot Number and quantity)
XXXXXXXXXXXXXX (Comment)
22 DS501-00017-11v2-E

## Page 23

MB85RC256V
1.3 Dimensions for Containers
(1) Dimensions for inner box
H
W
L
L W H
540 125 75
(Dimensions in mm)
(2) Dimensions for outer box
H
W
L
L W H
565 270 180
(Dimensions in mm)
DS501-00017-11v2-E 23

## Page 24

MB85RC256V
(2)MB85RC256VPNF-G-JNERE1/MB85RC256VPNF-G-AMERE2
1. Emboss Tape
1.1Tape Dimensions (not drawn to scale)(8-pin plastic SOP, 150mil)
Maximum storage
capacity
Part number Reel diameter ICs/reel ICs/inner box ICs/outer box
1,500 10,500
MB85RC256VPNF-G-JNERE1 Φ330 1,500
(1 pack/inner box) (7 inner boxes/outer box: Max)
1,500 9,000
MB85RC256VPNF-G-AMERE2 Φ254 1,500
(1 pack/inner box) (6 inner boxes/outer box: Max)
8.00 4.00
6.40
(Dimensions in mm)
Heat proof temperature : No heat resistance.
Package should not
be baked by using
tape and reel.
24 DS501-00017-11v2-E
01.2
57.1
05.5
05.5
2.00
0.30
0.21
B
A B A
SEC.B-B
SEC.A-A

## Page 25

MB85RC256V
1.2 IC orientation
• example
Index mark
(User Direction of Feed) (Reel side) (User Direction of Feed)
1.3 Reel dimensions
Dimensions in mm
Part number A B C W1 W2
MB85RC256VPNF-G-JNERE1 330 100 13 12.4 17.2
MB85RC256VPNF-G-AMERE2 254 100 13 13.5 17.5
DS501-00017-11v2-E 25
B A
Reel cutout dimensions
C
W1
W2

## Page 26

MB85RC256V
1.4 Product label indicators (example)
Label I: Label on Inner box/Moisture Barrier Bag/ (It sticks it on the reel for the emboss taping)
[C-3 Label (50mm x 100mm) Supplemental Label (20mm x 100mm)]
XXXXXXXXXXXXXX (Part number)
C-3 Label
(3N)1 XXXXXXXXXXXXXX XXX (LEAD FREE mark)
(Part number and quantity)
QC PASS
(3N)2 XXXXXXXXXX XXXXXX
(Control number bar code)
XXX pcs (Quantity)
XXXXXXXXXXXXXX (Part number)
(Part number bar code)
XXXX/XX/XX (Packed years/month/day) ASSEMBLED IN xxxx Perforated line
XXXXXXXXXXXXXX (Part number)
(Control number bar code) Supplemental Label
XX/XX XXXX-XXX XXX
(Package count) XXXX-XXX XXX
XXXXXXXXXX (Control number ) (Lot Number and quantity)
XXXXXXXXXXXXXX (Comment)
26 DS501-00017-11v2-E

## Page 27

MB85RC256V
1.5 Dimensions for Containers
(1) Dimensions for inner box
H
W
L
L W H
MB85RC256VPNF-G-JNERE1 365 345 40
MB85RC256VPNF-G-AMERE2 265 260 50
(Dimensions in mm)
(2) Dimensions for outer box
H
W
L
L W H
MB85RC256VPNF-G-JNERE1 415 400 315
MB85RC256VPNF-G-AMERE2 565 270 180
(Dimensions in mm)
DS501-00017-11v2-E 27

## Page 28

MB85RC256V
(3)MB85RC256VPNF-G-AME2
(Plastic SOP 8-pin, 150mil)
1. Tray
1.1 Tray Storage Capacity
Maximum storage capacity
ICs/tray ICs/inner box ICs/outer box
4,760 19,040
476
(Max:10 trays/inner box) (Max: 4 inner boxes/outer box)
1.2 Tray Dimensions (JEDEC Standard)
(Dimensions in mm)
Heat proof temperature :150 degC Max.
28 DS501-00017-11v2-E

## Page 29

MB85RC256V
1.3 IC Orientation
Index mark
IC
(cid:51)(cid:85)(cid:82)(cid:71)(cid:88)(cid:70)(cid:87)(cid:11)IC)
(cid:3)(cid:55)(cid:85)(cid:68)(cid:92)
(cid:38)(cid:75)(cid:68)(cid:80)(cid:73)(cid:72)(cid:85)(cid:72)(cid:71)(cid:3)(cid:70)(cid:82)(cid:85)(cid:81)(cid:72)(cid:85)
1.4 Product label indicators (an example)
Label on Inner box/Moisture Barrier Bag
[C-3 Label (50mm x 100mm) Supplemental Label (20mm x 100mm)]
XXXXXXXXXXXXXX (Part number)
C-3 Label
(3N)1 XXXXXXXXXXXXXX XXX (LEAD FREE mark)
(Part number and quantity)
QC PASS
(3N)2 XXXXXXXXXX XXXXXX
(Control number bar code)
XXX pcs (Quantity)
XXXXXXXXXXXXXX (Part number)
(Part number bar code)
XXXX/XX/XX (Packed years/month/day) ASSEMBLED IN xxxx Perforated line
XXXXXXXXXXXXXX (Part number)
(Control number bar code) Supplemental Label
XX/XX XXXX-XXX XXX
(Package count) XXXX-XXX XXX
XXXXXXXXXX (Control number ) (Lot Number and quantity)
XXXXXXXXXXXXXX (Comment)
DS501-00017-11v2-E 29

## Page 30

MB85RC256V
1.5 Dimensions for Containers
(1) Dimensions for inner box
H
W
L
L W H
165 360 75
(Dimensions in mm)
(2) Dimensions for outer box
H
W
L
L W H
355 385 195
(Dimensions in mm)
30 DS501-00017-11v2-E

## Page 31

MB85RC256V
(4)MB85RC256VPF-G-BCE1/MB85RC256VPF-G-BCERE1
(Plastic SOP 8-pin, 208mil)
1. Tube (MB85RC256VPF-G-BCE1)
1.1 Tube Dimensions
• Tube/stopper shape (example)
Tube
Stopper
Tube cross-sections and Maximum quantity
Maximum quantity
Package form
ICs/tube ICs/inner box ICs/outer box
80 4,000 16,000
10.6
tube length:508
No heat resistance.
Package should not be baked by using tube.
(Dimensions in mm)
• Tube orientation
Index mark
DS501-00017-11v2-E 31
2.4

## Page 32

MB85RC256V
1.2 Product label indicators (example)
Label I: Label on Inner box/Moisture Barrier Bag/ (It sticks it on the reel for the emboss taping)
[C-3 Label (50mm x 100mm) Supplemental Label (20mm x 100mm)]
XXXXXXXXXXXXXX (Part number)
C-3 Label
(3N)1 XXXXXXXXXXXXXX XXX (LEAD FREE mark)
(Part number and quantity)
QC PASS
(3N)2 XXXXXXXXXX XXXXXX
(Control number bar code)
XXX pcs (Quantity)
XXXXXXXXXXXXXX (Part number)
(Part number bar code)
XXXX/XX/XX (Packed years/month/day) ASSEMBLED IN xxxx Perforated line
XXXXXXXXXXXXXX (Part number)
(Control number bar code) Supplemental Label
XX/XX XXXX-XXX XXX
(Package count) XXXX-XXX XXX
XXXXXXXXXX (Control number ) (Lot Number and quantity)
XXXXXXXXXXXXXX (Comment)
32 DS501-00017-11v2-E

## Page 33

MB85RC256V
1.3 Dimensions for Containers
(1) Dimensions for inner box
H
W
L
L W H
540 125 75
(Dimensions in mm)
(2) Dimensions for outer box
H
W
L
L W H
565 270 180
(Dimensions in mm)
DS501-00017-11v2-E 33

## Page 34

MB85RC256V
2. Emboss Tape(MB85RC256VPF-G-BCERE1)
2.1 Tape Dimensions (not drawn to scale)(8-pin plastic SOP, 208mil)
Maximum storage capacity
ICs/reel(Φ=254) ICs/inner box ICs/outer box
500 3,000
500
(1 pack/inner box) (6 inner boxes/outer box: Max)
12.0 4.00
(Dimensions in mm)
Heat proof temperature : No heat resistance.
Package should not be baked by using tape
and reel.
34 DS501-00017-11v2-E
00.3
57.1
05.7
2.00
0.30
B
A B A
0.61
08.6
SEC.B-B
8.30
SEC.A-A

## Page 35

MB85RC256V
2.2 IC orientation
• example
Index mark
(User Direction of Feed) (Reel side) (User Direction of Feed)
2.3 Reel dimensions
Dimensions in mm
A B C W1 W2
254 100 13 17.5 21.5
DS501-00017-11v2-E 35
B A
Reel cutout dimensions
C
W1
W2

## Page 36

MB85RC256V
2.4 Product label indicators (example)
Label I: Label on Inner box/Moisture Barrier Bag/ (It sticks it on the reel for the emboss taping)
[C-3 Label (50mm x 100mm) Supplemental Label (20mm x 100mm)]
XXXXXXXXXXXXXX (Part number)
C-3 Label
(3N)1 XXXXXXXXXXXXXX XXX (LEAD FREE mark)
(Part number and quantity)
QC PASS
(3N)2 XXXXXXXXXX XXXXXX
(Control number bar code)
XXX pcs (Quantity)
XXXXXXXXXXXXXX (Part number)
(Part number bar code)
XXXX/XX/XX (Packed years/month/day) ASSEMBLED IN xxxx Perforated line
XXXXXXXXXXXXXX (Part number)
(Control number bar code) Supplemental Label
XX/XX XXXX-XXX XXX
(Package count) XXXX-XXX XXX
XXXXXXXXXX (Control number ) (Lot Number and quantity)
XXXXXXXXXXXXXX (Comment)
Label II*:Moisture Barrier Bag (It sticks on the Aluminum laminated bag)
SOP8(208mil) [MSL Label (100mm x 70mm)]
MSL label
36 DS501-00017-11v2-E

## Page 37

MB85RC256V
2.5 Dimensions for Containers
(1) Dimensions for inner box
H
W
L
L W H
265 260 50
(Dimensions in mm)
(2) Dimensions for outer box
H
W
L
L W H
565 270 180
(Dimensions in mm)
DS501-00017-11v2-E 37

## Page 38

MB85RC256V
 MAJOR CHANGES IN THIS EDITION
A change on a page is indicated by a vertical line drawn on the left side of that page.
Page Section Change Results
 PACKING INFORMATION Tube/stopper shape and tube-cross sections are revised.
21 (1)MB85RC256VPNF-G-JNE1
(Plastic SOP 8-pin, 150mil)
 PACKING INFORMATION Tray Dimensions is revised.
28 (3)MB85RC256VPNF-G-AME2
(Plastic SOP 8-pin, 150mil)
 PACKING INFORMATION Tube cross-section is revised.
(4)MB85RC256VPF-G-BCE1/
31
MB85RC256VPF-G-BCERE1
(Plastic SOP 8-pin, 208mil)
38 DS501-00017-11v2-E

## Page 39

MB85RC256V
RAMXEED LIMITED
Shin-Yokohama Chuo Building, 2-100-45 Shin-Yokohama,
Kohoku-ku, Yokohama, Kanagawa 222-0033, Japan
https://ramxeed.com/
All Rights Reserved.
RAMXEED LIMITED, its subsidiaries and affiliates (collectively, "RAMXEED") reserves the right to make changes to the infor-
mation contained in this document without notice. Please contact your RAMXEED sales representatives before order of RAMXEED
device.
Information contained in this document, such as descriptions of function and application circuit examples is presented solely for
reference to examples of operations and uses of RAMXEED device. RAMXEED disclaims any and all warranties of any kind, wheth-
er express or implied, related to such information, including, without limitation, quality, accuracy, performance, proper operation of
the device or non-infringement. If you develop equipment or product incorporating the RAMXEED device based on such informa-
tion, you must assume any responsibility or liability arising out of or in connection with such information or any use thereof. RAMX-
EED assumes no responsibility or liability for any damages whatsoever arising out of or in connection with such information or any
use thereof.
Nothing contained in this document shall be construed as granting or conferring any right under any patents, copyrights, or any other
intellectual property rights of RAMXEED or any third party by license or otherwise, express or implied. RAMXEED assumes no
responsibility or liability for any infringement of any intellectual property rights or other rights of third parties resulting from or in
connection with the information contained herein or use thereof.
The products described in this document are designed, developed and manufactured as contemplated for general use including
without limitation, ordinary industrial use, general office use, personal use, and household use, but are not designed, developed and
manufactured as contemplated (1) for use accompanying fatal risks or dangers that, unless extremely high levels of safety is secured,
could lead directly to death, personal injury, severe physical damage or other loss (including, without limitation, use in nuclear
facility, aircraft flight control system, air traffic control system, mass transport control system, medical life support system and
military application), or (2) for use requiring extremely high level of reliability (including, without limitation, submersible repeater
and artificial satellite). RAMXEED shall not be liable for you and/or any third party for any claims or damages arising out of or in
connection with above-mentioned uses of the products.
Any semiconductor devices fail or malfunction with some probability. You are responsible for providing adequate designs and
safeguards against injury, damage or loss from such failures or malfunctions, by incorporating safety design measures into your
facility, equipments and products such as redundancy, fire protection, and prevention of overcurrent levels and other abnormal
operating conditions.
The products and technical information described in this document are subject to the Foreign Exchange and Foreign Trade Control
Law of Japan, and may be subject to export or import laws or regulations in U.S. or other countries. You are responsible for ensuring
compliance with such laws and regulations relating to export or re-export of the products and technical information described herein.
All company names, brand names and trademarks herein are property of their respective owners.
