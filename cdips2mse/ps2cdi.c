/*
 *	v1.50
 *
 *	compile with PICCLITE from www.htsoft.com
 *	command line:
 *	picl -O -Zg9 -16F84A -V cdimouse.c
 *
 *	pin assignments:
 *	PORTA				4 p3	3 p2	2 p1	1 p18	0 p17
 *							PS2DATA	CDIRTS	MSEDATA
 *	
 *	PORTB	7 p13	6 p12	5 p11	4 p10	3 p9	2 p8	1 p7	0 p6
 *					PS2CLK		LED	MSERTS	CDIDATA
 *
 *	OTHER	p5	p14	p4	p15	p16
 *		Vss/GND	Vdd/+5	/MCLR	OSC2CKO	OSC1CKI
 */
 
#include	<pic.h>
__CONFIG(FOSC_HS & WDTE_OFF & CPD_OFF & LVP_OFF & BOREN_OFF & MCLRE_OFF & PWRTE_ON);

#define	XTAL		10000000	// Crystal frequency (Hz).
#define	BRATE		1200L		// Baud rate. "L" prevents truncation in calculations.
#define RX_OVERSAMPLE	16		// Receive oversampling. Must be at least 4 and power of two

/**********YOU DON'T NEED TO CHANGE ANYTHING BEYOND THIS POINT************/

// Keeping you honest :-)
#if	(RX_OVERSAMPLE-1)&RX_OVERSAMPLE
#error	RX_OVERSAMPLE value must be a power of 2
#endif
#define RX_BITCENTER	((RX_OVERSAMPLE/2) - 1)

#define byte unsigned char

// Other pins
static volatile bit LED		@ (unsigned)&PORTB*8+2;	// RB2 = LED output
static volatile bit MSERTS	@ (unsigned)&PORTB*8+1;	// RB1 = MSERTS output
static volatile bit CDIRTS	@ (unsigned)&PORTA*8+1;	// RA1 = CDIRTS input

// Transmit and Receive port bits
static volatile bit TxData 	@ (unsigned)&PORTB*8+0;	// RB0 = CDIDATA output
static volatile bit RxData 	@ (unsigned)&PORTA*8+0;	// RA0 = MSEDATA input

// P/S2 IO
static volatile bit PS2CLK	@ (unsigned)&PORTB*8+4;	// RB4 = PS2CLK				
static volatile bit PS2DATA	@ (unsigned)&PORTA*8+2;	// RA2 = PS2DATA				
// _HI = input and _LO = output
// _HI = high impedance and _LO = GNDed
#define PS2DATA_HI 	{PS2DATA = 0;TRISA2 = 1;}
#define PS2DATA_LO	{PS2DATA = 0;TRISA2 = 0;}
#define PS2CLK_HI	{PS2CLK = 0;TRISB4 = 1;}
#define PS2CLK_LO	{PS2CLK = 0;TRISB4 = 0;}

// Don't change these
#define TIMER_VALUE	(XTAL / (4 * BRATE * RX_OVERSAMPLE))
#define TRANSMIT_NUM_BITS	10	// 1 start bit + 7 data bits + 2 stop bits + safe.
#define INT_PERIOD	(1000000 / (BRATE * RX_OVERSAMPLE)) //that is in microseconds

#if ((TIMER_VALUE) < 90)
#error baud rate or oversample too high for crystal speed
#endif

#if ((TIMER_VALUE) > 255)
#error baud rate or oversample too low for crystal speed
#endif

// Delay constants
#define T_80_US	(80 / INT_PERIOD + 1)
#if (T_80_US < 2)
#undef T_80_US
#define T_80_US 2
#endif

#define T_1000_US	(1000 / INT_PERIOD + 1)

// RS232 constants
#define TX_START	0x00
#define TX_STOP		0x80

// P/S2 constants
#define PS2_RET_BAT	0xAA
#define PS2_RET_ID	0x00
#define PS2_RET_ACK	0xFA
#define PS2_RET_NAK	0xFE

#define PS2_CMD_GETID	0xF2
#define PS2_CMD_RESET	0xFF
#define PS2_CMD_ENABLE	0xF4
#define PS2_CMD_QUERY	0xEB
#define PS2_CMD_SETREMOTE 0xF0
#define PS2_CMD_SETSTREAM 0xEA
#define PS2_CMD_SETRATE	0xF3
#define PS2_CMD_SETRES	0xE8
#define PS2_CMD_DEFAULT	0xF6
#define PS2_CMD_GETINFO	0xE9
#define PS2_CMD_SETSCALE 0xE6

// ps2_rmode flags
//
// There are 3 basic levels of mouse operation depending on quality of mouse,
// 1. Stream mode at 40 samples/sec
// 2. Stream mode at 20 samples/sec with data polling
// 3. Remote mode using data polling
//
#define PS2_MODE_RATE_40	0x01
#define	PS2_MODE_RATE_20	0x02
#define PS2_MODE_STREAM		0x04
#define PS2_MODE_REMOTE		0x08

// Receiver states.
enum receiver_state {
	RS_HAVE_NOTHING,
	RS_WAIT_HALF_A_BIT,
	RS_HAVE_STARTBIT,
	RS_WAIT_FOR_STOP = RS_HAVE_STARTBIT+7
};

enum ps2_receiver_state {
	PS_HAVE_NOTHING,
	PS_HAVE_STARTBIT,
	PS_WAIT_FOR_PARITY = RS_HAVE_STARTBIT+7,
	PS_WAIT_FOR_STOP = RS_HAVE_STARTBIT+8
};

// VARIABLES
static byte	rxbuffer[8]; 		//data array
static byte	ps2_rxbuffer[9];
static byte 	rxbufferpointer;	//pointer to current data
static byte 	ps2_rxbufferpointer;
static byte 	rxbufferinput;		//address of next data
static byte	ps2_rxbufferinput;
static byte 	receivebufferfull;	//length of buffer (distance between current and next)
static byte	ps2_receivebufferfull;
static byte	receivestate; 		// Initial state of the receiver (0).
static byte	ps2_receivestate;	
static byte	rxshift;
static byte	ps2_rxshift;

static byte	skipoversamples;	// Used to skip receive samples.

static byte	send_bitno;
static byte	ps2_send_bitno;
static bit	tx_next_bit;
static bit	ps2_tx_next_bit;
static byte	sendbuffer;		// Where the character to sent is stored.
static byte	ps2_sendbuffer;	
static bit	ps2_parity;

static bit	ps2_inited = 0;		// mouse inited
static bit	ps2_stoped = 0;		// mouse communication stopped
static bit	ps2_overflow = 0;	// set to slow sample rate
static byte	ps2_rmode;		// mouse transmit mode

static byte	ps2_ack;
static byte	ps2_laststate;

static volatile byte	delaycount;		//decrements every interrupt if nonzero
static byte 	temp;
static byte ia,ib,ic;

// FUNCTION PROTOTYPES
void init_stuff(void);		//sets up interrupt, pins, etc
void init_mouse(void);		//send mouse ID
void rs232_putch(byte c);	//puts a byte to serial transmit buffer
byte rs232_getch(void);		//gets byte from serial receive buffer
void delayMs(byte t);		//delays 1 to 255 milliseconds
void ps2_putch(byte c);		//puts a byte to P/S2 transmit buffer and store ACK into ps2_ack
void ps2_putbit(byte c);	//puts out a P/S2 bit
byte ps2_getch(void);		//gets byte from P/S2 receive buffer
void ps2_packet(void);		//received P/S2 packet handler
void ps2_mouse_init(void);	//init P/S2 mouse
void ps2_putcmd(byte a);	//puts out a byte P/S2 cmd and resends if neccessary
void ps2_putcmd2(byte a,byte b); //puts out a 2 byte P/S2 cmd and resends if neccessary

void error_flash(byte err);

// THE PROGRAM
void main(void) 
{
	init_stuff();	//init PIC
	LED = 0;	//turn off LED
	delayMs(150);	//wait 150ms for CD-i
	rs232_putch(0x4D); 	//transmit mouse ID
	rs232_putch(0x40);	//and data packet
	rs232_putch(0x00);
	rs232_putch(0x00);
	LED = 1;	//turn on LED
	
	while(1) {
		if (CDIRTS)
		{
			init_mouse();	//check for negated RTS
			continue;
		}
		if (receivebufferfull)		//check for mouse data in buffer
		{
			rs232_putch(rs232_getch()); //transmit mouse data to CD-i
			if (receivebufferfull) rs232_putch(rs232_getch());
			if (receivebufferfull) rs232_putch(rs232_getch());
		}
		if (ps2_receivebufferfull)
		{
			ps2_packet();
		} else {
			if (ps2_rmode & PS2_MODE_REMOTE)
			{
				ps2_putch(PS2_CMD_QUERY);
				if (ps2_ack == PS2_RET_ACK)
				{
					ps2_packet();
				}
			}
		}
		if (ps2_overflow)
		{
			ps2_overflow = 0;
			if (ps2_rmode & PS2_MODE_RATE_20)
			{
				ps2_rmode &= ~PS2_MODE_STREAM;
				ps2_putcmd(PS2_CMD_SETREMOTE);
				if (ps2_ack == PS2_RET_ACK)
				{
					ps2_rmode |= PS2_MODE_REMOTE;
				} else {
					ps2_rmode &= ~PS2_MODE_REMOTE;
					error_flash(11);
				}
			}
			if (ps2_rmode & PS2_MODE_RATE_40)
			{
				ps2_putcmd2(PS2_CMD_SETRATE,0x14);
				if (ps2_ack == PS2_RET_ACK)
				{
					ps2_rmode &= ~PS2_MODE_RATE_40;
					ps2_rmode |= PS2_MODE_RATE_20;
				} else {
					error_flash(5);
				}
				ps2_rmode |= PS2_MODE_REMOTE;
			}
		}	
	}
}

// THE FUNCTIONS
void init_stuff(void) 
{
	CMCON = 0x07;			// disable comparator (16F627)
	PORTA = 0;PORTB = 0;		// Clear output latches
	TRISA = 0b00011111;		// PORTA inputs.
	TRISB = 0b00000000;		// PORTB outputs
	nRBPU = 1;			// Disable PORTB pullup resistors

	receivestate = RS_HAVE_NOTHING;
	ps2_receivestate = RS_HAVE_NOTHING;
	ps2_rmode = 0;
	receivebufferfull = 0;
	skipoversamples = 1;		// check each interrupt for start bit

	PS2CLK_HI;
	PS2DATA_HI;

	/* Set up the timer. */
	T0CS = 0;			// Set timer mode for Timer0
	TMR0 = (2-TIMER_VALUE);		// +2 as timer stops for 2 cycles
					//   when writing to TMR0
	T0IE = 1;			// Enable the Timer0 interrupt
	RBIE = 1;			// Enable INT-on-change RB4:7
	INTCON &= 0b11111000;		// Clear interrupt flags
	GIE = 1;			// Enable interrupts

	MSERTS = 0;	//enable mouse
}

void delayMs(byte t) 
{
	do
	{
		delaycount = T_1000_US;
		while(delaycount);
	} while(--t);
}

void rs232_putch(byte c) 
{
	while(send_bitno) continue;	//wait if still sending previous byte
	if (CDIRTS) return;		//if RTS is negated don't send any data
	tx_next_bit = TX_START;		//set to send start bit first
	sendbuffer = c & 0x7F;		//set sendbuffer bit 0-6 (7 bits data)
	sendbuffer |= TX_STOP;		//set to send bit 7 as stop bit
	send_bitno = TRANSMIT_NUM_BITS*RX_OVERSAMPLE; //set number of bits to send as timer value
}

byte rs232_getch(void) 
{
	while(!receivebufferfull) continue;
	receivebufferfull--; // decrease buffer length
	temp = rxbuffer[rxbufferpointer];
	rxbufferpointer++; // move pointer
	rxbufferpointer &= 0x07;
	return temp;
}

void ps2_putch(byte c)
{
	//stop mouse data transmission
	if (ps2_receivestate == PS_WAIT_FOR_STOP)
	{
		while (ps2_receivestate != PS_HAVE_NOTHING);
	}
	PS2CLK_LO;		//hold CLK and wait for over 60uS
	delaycount = T_80_US;
	while (delaycount);
	//clear and reset receiver
	ps2_rxshift = 0;
	ps2_receivestate = PS_HAVE_NOTHING;
	ps2_rxbufferpointer = ps2_rxbufferinput;
	ps2_receivebufferfull = 0;
	//enter a RTS state
	PS2DATA_LO;		//set START bit and release CLK
	PS2CLK_HI;
	//prepare to send 8 bits data and parity
	ps2_tx_next_bit = c & 0x01; //send LSB first and shift for next
	ps2_sendbuffer = c >> 1; 
	ps2_parity = 1;		//prepare for odd parity
	//set transmit 9 bits and wait to complete
	ps2_send_bitno = 9;
	while (ps2_send_bitno);
	GIE = 0;
	while (PS2CLK); //wait for CLK to go low
	PS2DATA_HI; //set STOP bit
	while (PS2DATA); //wait for mouse to force PS2DATA low
	while (PS2CLK);  //then wait for one last CLK pulse
	while (!PS2CLK);
	GIE = 1;
	
	//place ACK into ps2_ack
	ps2_ack = ps2_getch();
}

byte ps2_getch(void)
{
	while(!ps2_receivebufferfull) continue;
	ps2_receivebufferfull--; // decrease buffer length
	temp = ps2_rxbuffer[ps2_rxbufferpointer];
	ps2_rxbufferpointer++; // move pointer
	if (ps2_rxbufferpointer == 9) ps2_rxbufferpointer = 0;
	return temp;
}
	
/* Interrupt service routine
 * 
 * Transmits and receives characters which have been
 * "rs232_putch"ed and "rs232_getch"ed.
 * 
 * This ISR runs BRATE * RX_OVERSAMPLE times per second.
 * 
 */

interrupt void isr(void)
{
	/*** P/S2 IO ***/
	if (RBIF)
	{
	if (!PS2CLK) //host always I/Os data on LOW clock
	{
		/*** TRANSMIT ***/
		if (ps2_send_bitno)
		{
			if (ps2_send_bitno == 1) ps2_tx_next_bit = ps2_parity;
			if (ps2_tx_next_bit)
			{
				PS2DATA_HI;
				ps2_parity = !ps2_parity; 
			} else {
				PS2DATA_LO;
			}
			ps2_tx_next_bit = ps2_sendbuffer & 1;	//set next bit to send LSB first
			ps2_sendbuffer = (ps2_sendbuffer >> 1); //shift right one
			ps2_send_bitno--;
		} else {
		/** RECEIVE ***/
			switch (ps2_receivestate) {
			case PS_HAVE_NOTHING:
				if (PS2DATA == 0)
				{
					ps2_receivestate++;
					ps2_parity = 1;
					ps2_rxshift = 0;
				}
				break;

			default:
				ps2_rxshift = (ps2_rxshift >> 1);
				if (PS2DATA)
				{
					ps2_rxshift |= 0x80;
					ps2_parity = !ps2_parity;
				}
				ps2_receivestate++;
				break;

			case PS_WAIT_FOR_PARITY:
				if (ps2_parity == PS2DATA)
				{
					ps2_receivestate++;
				} else {
					ps2_receivestate = PS_HAVE_NOTHING;
				}
				break;
			
			case PS_WAIT_FOR_STOP:
				if (PS2DATA == 1)
				{
					if (ps2_receivebufferfull == 9)
					{
						// BUFFER OVERFLOW!!
						// inhibit mouse communication
						ps2_stoped = 1;
						ps2_overflow = 1;
						PS2CLK_LO;
						LED = 0;
					} else {
						ps2_receivebufferfull++;
						ps2_rxbuffer[ps2_rxbufferinput] = ps2_rxshift; // set new data
						ps2_rxbufferinput++; // move input pointer
						if (ps2_rxbufferinput == 9) ps2_rxbufferinput = 0;
					}
				}
				ps2_receivestate = PS_HAVE_NOTHING;
				break;	
			}
		}
	}
	RBIF = 0;
	} else
	/*** RS232 IO ***/
	{
	TMR0 += -TIMER_VALUE + 2;	// +2 as timer stops for 2 cycles when writing to TMR0
	T0IF = 0;

	/*** RECEIVE ***/
	if( --skipoversamples == 0) {
		skipoversamples++;		// check next time
		switch(receivestate) {

		case RS_HAVE_NOTHING:
			/* Check for start bit of a received char. */
			if(!RxData){
				skipoversamples = RX_BITCENTER;
				receivestate++;
			}
			break;

		case RS_WAIT_HALF_A_BIT:
			if(!RxData) 
			{	// valid start bit
				skipoversamples = RX_OVERSAMPLE;
				rxshift = 0;
				receivestate++;
			} else {
				receivestate = RS_HAVE_NOTHING;
			}
			break;
			
		// case RS_HAVE_STARTBIT: and subsequent values
		default:
			rxshift = rxshift >> 1;
			if (RxData) rxshift |= 0x80;
			skipoversamples = RX_OVERSAMPLE;
			receivestate++;
			break;

		case RS_WAIT_FOR_STOP:
			receivebufferfull++;
			rxbuffer[rxbufferinput] = rxshift >> 1; // set new data
			rxbufferinput++; // move input pointer
			rxbufferinput &= 0x07;
			receivestate = RS_HAVE_NOTHING;
			break;

		}
	}
	
	/*** TRANSMIT ***/
	/* This will be called every RX_OVERSAMPLEth time
 	* (because the RECEIVE needs to over-sample the incoming data). */

	if(send_bitno) {
	       	if((send_bitno & (RX_OVERSAMPLE-1)) == 0) {
			TxData = tx_next_bit;		//send next bit.
			tx_next_bit = sendbuffer & 1;	//set next bit to send LSB first
			sendbuffer = (sendbuffer >> 1); //shift right one
			sendbuffer |= TX_STOP;		//set to send bit 7 as stop bit
		}
		send_bitno--;	//count down until out of bits to send
	}

	/*** COUNTERS ETC ***/
	if(delaycount)
		delaycount--;

	/*** P/S2 BUFFER RELEASE ***/
	if (ps2_stoped == 1)
	{
		if (ps2_receivebufferfull < 4)
		{
			// buffer overflow cleared by 6 bytes
			// enable mouse communication
			ps2_stoped = 0;
			PS2CLK_HI;
			LED = 1;
		}
	}
	}
}

void init_mouse(void)
{
	LED = 0;		//turn off LED
	while(CDIRTS); 		//wait if RTS still negated
	delayMs(2);
	if (ps2_rmode & PS2_MODE_REMOTE)	//transmit mouse ID and a data packet
	{
		ps2_putch(PS2_CMD_QUERY);
		if (ps2_ack == PS2_RET_ACK)
		{
			ps2_laststate = 0x08;
			rs232_putch(0x4D);
			ps2_packet();
			LED = 1;
			return;
		}
	}
	rs232_putch(0x4D);
	rs232_putch(0x40);
	rs232_putch(0x00);
	rs232_putch(0x00);
	LED = 1;		//turn on LED
}

void ps2_packet(void)
{
	byte a,b,c;

	ia = ps2_getch();
	if (ia == PS2_RET_BAT && !(ps2_rmode & 0x0C)) // 0x0C = (PS2_MODE_STREAM | PS2_MODE_REMOTE)
	{
		//received BAT successful
		ia = ps2_getch();
		if (ia == PS2_RET_ID)
		{
			LED = 0;
			ps2_mouse_init(); //perform mouse init
			if (!ps2_inited) return;
			LED = 1;
		}
		return;
	}
	//receive data packet in remote or stream mode
	if ((ps2_rmode & 0x0C) && (ia & 0x08)) // 0x0C = (PS2_MODE_STREAM | PS2_MODE_REMOTE)
	{
		ib = ps2_getch();
		ic = ps2_getch();

		if ( ((ia & 0x07) == ps2_laststate) && (ib == 0) && (ic == 0) ) return;
		ps2_laststate = ia & 0x07;
		a = 0;b = 0;c = 0;

		a |= 0x40;
		if (ia & 1) a |= 0x20;	// bit 0 -> bit 5 = left button
		if (ia & 2) a |= 0x10;	// bit 1 -> bit 4 = right button
		if (ia & 4) a |= 0x30;	// bit 2 -> b 4/5 = both buttons

		if (ia & 16) //negative X
		{
			a |= 0x02;
			if (!(ib & 128)) ib = 0x01;
		} else {
			if (ib & 128) ib = 0xFF;
		}
		if (ib & 64) a |= 0x01;	// bit 0 = X6
		b = ib & 0x3F;

		ic = (~ic + 1) & 0xFF; //invert the Y up/down direction
		if (ia & 32) //positive Y
		{
			if (ic & 128) ic = 0xFF;
		} else {
			if (ic)
			{
				if (!(ic & 128)) ic = 0x01;
				a |= 0x08;
			}
		}
		if (ic & 64) a |= 0x04;	// bit 2 = Y6
		c = ic & 0x3F;

		rs232_putch(a);
		rs232_putch(b);
		rs232_putch(c);
	}	
}

void error_flash(byte err)
{
	LED = 0;
	delayMs(250);
	do
	{
		LED = 1;
		delayMs(250);
		delayMs(250);
		LED = 0;
		delayMs(250);
		delayMs(250);
	} while (--err);
}

void ps2_mouse_init(void)
{
	// perform mouse initialisation
	// to get mouse into decent working state!!
	//
	ps2_inited = 0;

	//step 1 - perform reset
	ps2_putcmd(PS2_CMD_RESET);
	if (ps2_ack != PS2_RET_ACK) {error_flash(1);return;}
	temp = ps2_getch();
	if (temp != PS2_RET_BAT) {error_flash(2);return;}
	temp = ps2_getch();
	if (temp != PS2_RET_ID) {error_flash(3);return;}

	//step 2 - set defaults
	ps2_putcmd(PS2_CMD_DEFAULT);
	if (ps2_ack != PS2_RET_ACK) {error_flash(4);return;}

	//step 3 - set sample rate
	ps2_putcmd2(PS2_CMD_SETRATE,0x28);
	if (ps2_ack != PS2_RET_ACK) {error_flash(5);return;}

	//step 4 - set resolution
	ps2_putcmd2(PS2_CMD_SETRES,0x03);
	if (ps2_ack != PS2_RET_ACK) {error_flash(6);return;}

	//step 5 - set scaling 1:1
	ps2_putcmd(PS2_CMD_SETSCALE);
	if (ps2_ack != PS2_RET_ACK) {error_flash(7);return;}
	
	//step 6 - check the settings
	ps2_putcmd(PS2_CMD_GETINFO);
	if (ps2_ack != PS2_RET_ACK) {error_flash(8);return;}
	temp = ps2_getch();
	temp = ps2_getch();
	if (temp != 0x03) {error_flash(9);return;}
	temp = ps2_getch();
	if (temp != 0x28) {error_flash(10);return;}
	ps2_rmode |= PS2_MODE_RATE_40;

	//step 7 - enable data reporting
	ps2_putcmd(PS2_CMD_ENABLE);
	if (ps2_ack == PS2_RET_ACK)
	{
		ps2_rmode |= PS2_MODE_STREAM;
	} else {
		ps2_putcmd(PS2_CMD_SETREMOTE);
		if (ps2_ack != PS2_RET_ACK) {error_flash(11);return;};
		ps2_rmode |= PS2_MODE_REMOTE;
	}
	
	ps2_inited = 1;
}

void ps2_putcmd(byte a)
{
	byte t = 0;
	putcmd:
	ps2_putch(a);
	if (ps2_ack == PS2_RET_NAK)
	{
		if (t == 4) return;
		t++;
		goto putcmd;
	}
}

void ps2_putcmd2(byte a,byte b)
{
	byte t = 0;
	putcmd2:
	ps2_putch(a);
	if (ps2_ack == PS2_RET_NAK)
	{
		if (t == 4) return;
		t++;
		goto putcmd2;
	}
	ps2_putch(b);
	if (ps2_ack == PS2_RET_NAK)
	{
		if (t == 4) return;
		t++;
		goto putcmd2;
	}
}
