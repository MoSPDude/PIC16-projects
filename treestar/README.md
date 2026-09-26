# Christmas Tree Star

Uses a PIC16F628.

It is a simple circuit, that I build on Veroboard,
 * Place a suitable electrolytic capacitor across +5V and GND.
 * Run 15 LEDs and suitable resistors from +5V to RB0-7 and RA0-4.
 * RA5 can be a button to GND to change the scene.

I had arranged the LEDs in 5 points of the star, with 3 LEDs on each leg - you can change the order in the ISR.

 * RB3 on outer edge, RB4, then RB5 in centre
 * RB6 on outer edge, RB7, then RA6 in centre
 * RB0 on outer edge, RB1, then RB2 in centre
 * RA7 on outer edge, RA0, then RA1 in centre
 * RA2 on outer edge, RA3, then RA4 in centre
