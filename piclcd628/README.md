# Crystalfontz CFA634 to HD47880 20x4 LCD emulator

Uses a PIC16F628 and 20 MHz crystal.

Need to find where the schematic went!

 * RB0-7 is LCD DB0-7
 * RA5 is Serial RX data
 * RA4 is LCD En
 * RA3 is LCD R/W
 * RA2 is LCD RS
 * RA1 is backlight
 * RA0 is Serial CTS

RA1 was ran through a suitable resistor to the base of an NPN transitor,

* Base to RA1 through suitable resistor
* Collector to LCD LED-
    * LCD LED+ to +5V via suitable resistor
* Emitter to GND

Serial is 9600 baud, 1 start, 1 stop and no parity.
