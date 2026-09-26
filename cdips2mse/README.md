# PS/2 (and Serial) Mouse to Philips CD-i adapter

Uses a PIC16F627 and 10 MHz crystal.

The original README.TXT is below.

## BEFORE YOU START

Read this document!!
Make sure you can successfully program an 18pin PIC microcontroller. There are free circuits
on the net. I use a Velleman kit PIC programmer that Maplins had on sale.
Test the mouse you plan to use on a PC to make sure it works, especially if its old!

## SERIAL MOUSE

You'll need to check the output of your serial mouse is mainly 3 bytes at 1200 baud, 7 bit 
data and 2 stop bits. Use a program like "serialwatcher" to check (ensuring RTS and DTR are
enabled). Clicking the left mouse button without moving should show (in decimal) :-
```
64 0 0
96 0 0
64 0 0
```

Old 2 button mice are best, with most 3 button mice if it sends only 3 bytes most the time.
I tried making a straight cable, but the CD-i didn't want to recognise any type of mouse 
at all, even the really old serial mouse I had that did send the initialisation 'M' 
command. So I decided to use a PIC for the initialisation and for most mice this is all 
that was needed.

The other thing to check is how your mouse gets power. 4 of the 5 mice I have use,

| DB9 | RS232 | Mouse |
| --- | --- | --- |
| 2 | RXD | RXD |
| 3 | TXD | GND |
| 5 | GND | V+ |
| 7 | RTS | RTS |

Note that GND is actually the +5V and TXD is 0V. This is because of RS232 negative logic, 
and when not in use TXD is actually around -5V relative to GND.

## PS/2 MOUSE

The converter works with every PS/2 mouse I have, with mild differecnes in degrees of 
smoothness. You can use PS/2 scroll mice, but it will operate as a 3 button PS/2 mouse.
The middle button simulates buttons 1+2.

NOTE: As a side note, you can use a serial and PS/2 mouse together. I do this for 
playing Cluedo with 2 players.

## CIRCUIT

The crystal is 10MHz, although if you do download the HI-TECH PICC compiler you can 
recompile the code for different clock speeeds. All capacitors near the MAX232CPE are 1uF,
but for an ordinary MAX232 the capacitors are 10uF.
The capacitors used near the crystal should be identical and around 15pF to 27pF.
The LED was mainly for debugging, and will flicker when the CD-i wants the mouse to 
initialise - remaining on once it has succeeded.

## FIRMWARE

I built this using a 16F84A but Maplins have the 16F627 in cheaper so I've provided the 
files for this PIC (but I've not tested them). The 16F627 is pin identical to the
16F84A.

The serial code is mainly based on the sample code with the HI-TECH PICC compiler, 
with some of the fixes from Bob Blicks LCDterm 
"http://www.bobblick.com/techref/projects/lcdterm/lcdterm.html" and then a few more of my 
own (such as the buffer etc).

## TROUBLESHOOTING

I left in my flashing LED debugging code, just in case. Errors are shown by a definate flash
of 500ms on then 500ms off.

 1 flash   = Error resetting mouse (bad command)
 2 flashes = Error resetting mouse (bad BAT)
 3 flashes = Error resetting mouse (bad BAT ID)
 4 flashes = Error setting defaults (bad command)
 5 flashes = Error setting sample rate (bad command)
 6 flashes = Error setting resolution (bad command)
 7 flashes = Error setting scaling (bad command)
 8 flashes = Error fetching info (bad command)
 9 flashes = Bad resolution setting
10 flashes = Bad sample rate setting
11 flashes = Error entering remote mode

Most errors are down to bad data transmission to the mouse.
Occassionally it errors, but on a power off then on, works fine. I've no idea what causes them.

## MISC

Naturally, I take no responsibility if it goes horribly wrong or damages yor CD-i etc.
You attempt this project at your own risk.

Thanks to,
    * www.icdia.co.uk for tech sheets and CD-i resources
    * Bob Blick and LCDterm for RS232 PIC code fixes
    * www.computer-engineering.org (Adam Chapweske) for an understandable PS/2 specification

If you do see any faults in what I've drawn, feel free to fix them.
