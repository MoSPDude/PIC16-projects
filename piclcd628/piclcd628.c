//
//
// v2.22
//

#define FIRMWARE "v2.22 20161015"

#include <pic.h>

__CONFIG(FOSC_HS & WDTE_OFF & LVP_OFF & BOREN_OFF & MCLRE_OFF & PWRTE_ON);

#define    XTAL        20000000    // Crystal frequency (Hz).
#define    BRATE        9600L    // Baud rate. "L" prevents truncation in calculations.
#define RX_OVERSAMPLE    4    // Receive oversampling. Must be at least 4 and power of two

#define byte unsigned char

#define RS232_BUF_SIZE 80

// LCD pins
static bit LCD_RS    @ ((unsigned)&PORTA*8+2);    // Register select (output)
static bit LCD_RW   @ ((unsigned)&PORTA*8+3);   // Read/Write select (output)
static bit LCD_EN    @ ((unsigned)&PORTA*8+4);    // Enable (output)
static volatile bit LCD_BK    @ ((unsigned)&PORTA*8+1);    // Backlight (output)

// Transmit and Receive port bits
static volatile bit    RxData @ ((unsigned)&PORTA*8+5); // Data In (input) NOTE: RA5 has no output driver!
static volatile bit CTSData @ ((unsigned)&PORTA*8+0); // Clear-To-Send Data (output)       

// Don't change these
#define TIMER_VALUE    (XTAL / (4 * BRATE * RX_OVERSAMPLE))
#define INT_PERIOD    (1000000 / (BRATE * RX_OVERSAMPLE)) //that is in microseconds (approx 26uS at 9600bps x 4)
#define RX_BITCENTER    ((RX_OVERSAMPLE/2) - 1)

#if ((TIMER_VALUE) < 90)
#error baud rate or oversample too high for crystal speed
#endif

#if ((TIMER_VALUE) > 255)
#error baud rate or oversample too low for crystal speed
#endif

// Delay constants used by LCD routines
#define T_40_US        (40 / INT_PERIOD + 1)
#if (T_40_US < 2)
#undef T_40_US
#define T_40_US 1
#endif
#define T_120_US    (120 / INT_PERIOD + 1)
#if (T_120_US < 2)
#undef T_120_US
#define T_120_US 2
#endif
#define T_1000_US    (1000 / INT_PERIOD + 1)

// LCD line addresses
#define BEGIN_LINE_1    0x00
#define END_LINE_1        0x13
#define BEGIN_LINE_2    0x40
#define END_LINE_2      0x53
#define BEGIN_LINE_3    0x14
#define END_LINE_3      0x27
#define BEGIN_LINE_4    0x54
#define END_LINE_4      0x67

#define LINE_1_MINUS_1    0xFF
#define LINE_2_MINUS_1    0x3F
#define LINE_3_MINUS_1    0x13
#define LINE_4_MINUS_1    0x53

// LCD variables and routines
static bit lcd_display;
static bit lcd_cursor;
static bit lcd_cursorblink;
static bit lcd_wrap;
static byte lcd_light;
static byte lcd_backlight;
static byte lcd_cursorpos;
void lcd_strobe(void);
void lcd_wait(void);
void lcd_puts(const byte *s);
void lcd_putch(byte c);
void lcd_putcmd(byte c);
byte lcd_getch(void);
void lcd_inc_cursor(void);
void lcd_dec_cursor(void);
void lcd_goto(byte pos);
void lcd_setdisplay(void);
void lcd_setbacklight(byte value);
void lcd_clear(void);
void lcd_cursorup(void);
void lcd_cursordown(void);
void lcd_gotoxy(byte x, byte y);

// RS232 receiver states
enum receiver_state {
    RS_HAVE_NOTHING,
    RS_WAIT_HALF_A_BIT,
    RS_HAVE_STARTBIT,
    RS_WAIT_FOR_STOP = RS_HAVE_STARTBIT + 8
};

// RS232 variables and routines
static unsigned byte bank1 rx_buffer[RS232_BUF_SIZE];
static byte rx_buffer_out;
static byte rx_buffer_in;
static byte rx_buffer_len;
static volatile byte rx_state;
static byte    rx_skipoversamples;
static byte    rx_shift;
static volatile bit rx_cts;
byte rs232_getch(void);

// Timer variables and routines
static volatile byte delaycount;
static volatile byte marqueecount;
static volatile byte marqueecount_mul;
static volatile byte marqueecount_ms;
void delayMs(byte t);
void delayS(byte t);

// Initialisation routines
void init_chip(void);
void init_lcd(void);

// Processing routines
void disp_cmd(byte cmd);
byte disp_data();
void disp_createbar(void);
void disp_createbar_udg(byte n, byte x, byte y);
void disp_drawmarquee(void);
void disp_loadudg(void);
void disp_drawnumbers(void);
void disp_bootmsg(void);

static byte disp_datalist[9];
static byte disp_opcode;
static byte disp_datalen;
static byte disp_marquee[20];
static byte disp_marquee_line;
static byte disp_marquee_speed;
static byte disp_marquee_speedmul;
static volatile bit disp_bignumbers;

// Big number table
const byte disp_big_blocks[8][8] = {
    {152,152,140,140,134,134,131,131}, // left slash
    {131,131,134,134,140,140,152,152}, // right slash
    {159,159,128,128,128,128,128,128}, // top bar
    {128,128,128,128,128,128,159,159}, // bottom bar
    {152,152,152,152,152,152,152,152}, // left bar
    {131,131,131,131,131,131,131,131}, // right bar
    {131,131,135,135,139,139,147,147}, // right angle bar
    {129,131,130,134,140,136,159,159}  // left angle bar
};

const byte disp_big_numbers[10][6] = {
    {0x14, 0x40, 0x28, 0x83, 0x05, 0x51}, // 0
    {0x81, 0x88, 0x65, 0x55, 0x88, 0x88}, // 1
    {0x28, 0x87, 0x28, 0x13, 0x01, 0x83}, // 2
    {0x18, 0x80, 0x28, 0x23, 0x01, 0x01}, // 3
    {0x81, 0x28, 0x18, 0x28, 0x44, 0x44}, // 4
    {0x55, 0x80, 0x23, 0x83, 0x28, 0x01}, // 5
    {0x87, 0x40, 0x18, 0x23, 0x88, 0x01}, // 6
    {0x28, 0x88, 0x21, 0x44, 0x48, 0x88}, // 7
    {0x10, 0x10, 0x23, 0x83, 0x01, 0x01}, // 8
    {0x10, 0x80, 0x23, 0x83, 0x06, 0x51}  // 9
};


interrupt void isr(void) 
{

    TMR0 += -TIMER_VALUE + 2;    // +2 as timer stops for 2 cycles when writing to TMR0
    T0IF = 0;

    // RECEIVE
    if( --rx_skipoversamples == 0)
    {
        rx_skipoversamples++; // check next time
        switch(rx_state)
        {
        case RS_HAVE_NOTHING:
            /* Check for start bit of a received char. */
            if (!RxData)
            {
                rx_skipoversamples = RX_BITCENTER;
                rx_state++;
            }
            /* Check to see if we're ready to receive data */
            if (!rx_cts && (rx_buffer_len < (RS232_BUF_SIZE - 1)))
            {
                CTSData = 1;
                rx_cts = 1;
            }
            break;

        case RS_WAIT_HALF_A_BIT:
            if (!RxData) // valid start bit
            {    
                rx_skipoversamples = RX_OVERSAMPLE;
                rx_state++;
            } else {
                rx_state = RS_HAVE_NOTHING;
            }
            break;

            // case RS_HAVE_STARTBIT: and subsequent values
        default:
            rx_shift = rx_shift >> 1;
            if (RxData) rx_shift |= 0x80;
            rx_skipoversamples = RX_OVERSAMPLE;
            rx_state++;
            break;

        case RS_WAIT_FOR_STOP:
            if (rx_cts)
            {
                ++rx_buffer_len;
                rx_buffer[rx_buffer_in] = rx_shift; // set new data
                ++rx_buffer_in;
                if (rx_buffer_in >= RS232_BUF_SIZE)
                {
                    rx_buffer_in -= RS232_BUF_SIZE;
                }
                if (rx_buffer_len >= RS232_BUF_SIZE) // check to see if we're full of data
                {
                    CTSData = 0;
                    rx_cts = 0;
                }
            }
            rx_state = RS_HAVE_NOTHING;
            break;
        }
    }

    //BACKLIGHT
    if (lcd_backlight)
    {
        if (lcd_light < lcd_backlight)
        {
            LCD_BK = 1;
        } else {
            LCD_BK = 0;
        }
        lcd_light = (lcd_light + 1) & 0x3F;
    }

    // MARQUEE
    if (marqueecount_ms)
    {
        marqueecount_ms--;
    } else {
        if (marqueecount_mul)
        {
            marqueecount_mul--;
            marqueecount_ms = T_1000_US;
        } else {
            if (marqueecount)
            {
                marqueecount--;
                marqueecount_mul = disp_marquee_speedmul;
            }
        }
    }

    // COUNTER
    if (delaycount)
        delaycount--;         
}

void delayMs(byte t)  
{
    do
    {
        delaycount = T_1000_US;
        while(delaycount);
    } while(--t);
}

void delayS(byte t) 
{
    byte temp;

    do
    {
        temp = 4;
        do
        {
            delayMs(250);
        } while (--temp);
    } while(--t);
}

void main(void)
{
    init_chip();
    init_lcd();
    disp_bootmsg();

    // Main loop
    while (1) 
    {
        // Transfer bytes from receive FIFO into display commands
        if (rx_buffer_len > 0)
        {
            disp_cmd(rs232_getch());
        }
        
        // Scroll the marquee
        if ((disp_marquee_line < 0x04) && (marqueecount == 0))
        {
            disp_drawmarquee();
            marqueecount = disp_marquee_speed;
        }
    }
}

void init_chip(void)
{
    byte temp;

    PORTA = 0x00;            // clear output latches
    PORTB = 0x00;
    CMCON = 0x07;            // disable comparator
    TRISA = 0b00100000;        // PORTA inputs
    TRISB = 0b00000000;        // PORTB outputs

    /* Set up ISR variables */
    lcd_light = 0;
    delaycount = 0;
    marqueecount = 0;
    marqueecount_mul = 0;
    marqueecount_ms = 0;
    rx_state = RS_HAVE_NOTHING;
    rx_buffer_len = 0;
    rx_buffer_out = 0;
    rx_buffer_in = 0;
    rx_skipoversamples = 1;
    rx_cts = 1;

    /* Set up other variables */
    disp_opcode = 0;
    disp_marquee_line = 0xFF;
    for (temp = 0; temp < 20; temp++)
    {
        disp_marquee[temp] = 0x41 + temp;
    }
    disp_bignumbers = 0;

    /* Set up the timer. */
    TMR1ON = 0;
    TMR2ON = 0;
    T0CS = 0; // Set timer mode for Timer0
    TMR0 = (2-TIMER_VALUE);    // +2 as timer stops for 2 cycles when writing to TMR0
    T0IE = 1; // Enable the Timer0 interrupt
    GIE = 1; // Enable interrupts

    CTSData = 1;
}

void init_lcd(void)
{
    // Wait for more than 30ms after Vdd > 4.5V
    delayMs(50);
    // Function set
    LCD_RS = 0;
    LCD_RW = 0;
    PORTB = 0b00111100;
    lcd_strobe();
    // Wait for more than 39us
    delaycount = T_40_US;
    while(delaycount) continue;
    // Display ON/OFF control
    PORTB = 0b00001100;
    lcd_strobe();
    // Wait for more than 39us
    delaycount = T_40_US;
    while(delaycount) continue;
    // Display clear
    PORTB = 0b00000001;
    lcd_strobe();
    // Wait for more than 1.53ms
    delaycount = T_40_US;
    while(delaycount) continue;
    // Entry mode set
    PORTB = 0b00000110;
    lcd_strobe();
    // Initialisation complete
    delayMs(1);
    // Display ON, cursor OFF, blink OFF
    PORTB = 0b00001100;
    lcd_strobe();
    // Wait for more than 39us
    delaycount = T_40_US;
    while(delaycount) continue;
    // Return home
    PORTB = 0b00000010;
    lcd_strobe();
    // Wait for more than 1.53ms
    delayMs(2);

    lcd_backlight = 0;
    LCD_BK = 0;
    lcd_display = 1;
    lcd_cursor = 0;
    lcd_cursorblink = 0;
    lcd_wrap = 1;
    lcd_cursorpos = BEGIN_LINE_1;
}

void lcd_strobe(void)
{
    LCD_EN = 1;
    delaycount = 1;
    //while(delaycount) continue;
    LCD_EN = 0;
}

void lcd_wait(void)
{
    byte flag;

    TRISB = 0b11111111;
    LCD_RS = 0;
    LCD_RW = 1;
    do
    {
        LCD_EN = 1;
        delaycount = 1;
        //while(delaycount) continue;
        flag = PORTB;
        LCD_EN = 0;
    } while (flag & 0b10000000);
    LCD_RW = 0;
    TRISB = 0b00000000;
}


void lcd_puts(const byte *s)
{
    while(*s)
    {
        lcd_putch(*s++);
    }
}

void lcd_putch(byte c) 
{
    lcd_wait();
    LCD_RS = 1;
    LCD_RW = 0;
    PORTB = c;
    lcd_strobe();
}

void lcd_putcmd(byte c)
{
    lcd_wait();
    LCD_RS = 0;
    LCD_RW = 0;
    PORTB = c;
    lcd_strobe();
}

byte lcd_getch(void)
{
    byte data;

    lcd_wait();
    TRISB = 0b11111111;
    LCD_RS = 1;
    LCD_RW = 1;
    LCD_EN = 1;
    delaycount = 1;
    //while(delaycount) continue;
    data = PORTB;
    LCD_EN = 0;
    LCD_RW = 0;
    TRISB = 0b00000000;

    return data;
}

void lcd_inc_cursor(void) 
{
    lcd_cursorpos++;
    if (lcd_wrap)
    {
        switch(lcd_cursorpos) 
        {
        case (END_LINE_1 + 1):
            lcd_cursorpos = BEGIN_LINE_2;
            lcd_goto(lcd_cursorpos);
            break;
        case (END_LINE_2 + 1):
            lcd_cursorpos = BEGIN_LINE_3;
            lcd_goto(lcd_cursorpos);
            break;
        case (END_LINE_3 + 1):
            lcd_cursorpos = BEGIN_LINE_4;
            lcd_goto(lcd_cursorpos);
            break;
        case (END_LINE_4 + 1):
            lcd_cursorpos = BEGIN_LINE_1;
            lcd_goto(lcd_cursorpos);
            break;
        }
    }
}

void lcd_dec_cursor(void) 
{
    lcd_cursorpos--;
    if (lcd_wrap)
    {
        switch(lcd_cursorpos) 
        {
        case (LINE_1_MINUS_1):
            lcd_cursorpos = END_LINE_4;
            lcd_goto(lcd_cursorpos);
            break;
        case (LINE_2_MINUS_1):
            lcd_cursorpos = END_LINE_1;
            lcd_goto(lcd_cursorpos);
            break;
        case (LINE_3_MINUS_1):
            lcd_cursorpos = END_LINE_2;
            lcd_goto(lcd_cursorpos);
            break;
        case (LINE_4_MINUS_1):
            lcd_cursorpos = END_LINE_3;
            lcd_goto(lcd_cursorpos);
            break;
        }
    }
}

void lcd_goto(byte pos)
{
    lcd_putcmd(0x80 + pos);
    lcd_cursorpos = pos;
}

void lcd_setdisplay(void)
{
    lcd_putcmd(0x08 + (lcd_display << 2) + (lcd_cursor << 1) + lcd_cursorblink);
}

void lcd_setbacklight(byte value)
{
    if (value > 99) {
        lcd_backlight = 0;
        LCD_BK = 1;
    } else if (value > 0)
    {
        lcd_backlight = ((value >> 1) + (value >> 3)) & 0x3F;
    } else //if (value == 0)
    {
        lcd_backlight = 0;
        LCD_BK = 0;
    }
}

void lcd_clear(void)
{
    lcd_putcmd(0x01);
    lcd_cursorpos = BEGIN_LINE_1;
}

void lcd_cursorup(void)
{
    if (lcd_cursorpos <= END_LINE_1)
    {
        lcd_goto(BEGIN_LINE_4 + (lcd_cursorpos - BEGIN_LINE_1));
    } else if ((lcd_cursorpos >= BEGIN_LINE_2) && (lcd_cursorpos <= END_LINE_2))
    {
        lcd_goto(BEGIN_LINE_1 + (lcd_cursorpos - BEGIN_LINE_2));
    } else if ((lcd_cursorpos >= BEGIN_LINE_3) && (lcd_cursorpos <= END_LINE_3))
    {
        lcd_goto(BEGIN_LINE_2 + (lcd_cursorpos - BEGIN_LINE_3));
    } else if ((lcd_cursorpos >= BEGIN_LINE_4) && (lcd_cursorpos <= END_LINE_4))
    {
        lcd_goto(BEGIN_LINE_3 + (lcd_cursorpos - BEGIN_LINE_4));
    }
}

void lcd_cursordown(void)
{
    if (lcd_cursorpos <= END_LINE_1)
    {
        lcd_goto(BEGIN_LINE_2 + (lcd_cursorpos - BEGIN_LINE_1));
    } else if ((lcd_cursorpos >= BEGIN_LINE_2) && (lcd_cursorpos <= END_LINE_2))
    {
        lcd_goto(BEGIN_LINE_3 + (lcd_cursorpos - BEGIN_LINE_2));
    } else if ((lcd_cursorpos >= BEGIN_LINE_3) && (lcd_cursorpos <= END_LINE_3))
    {
        lcd_goto(BEGIN_LINE_4 + (lcd_cursorpos - BEGIN_LINE_3));
    } else if ((lcd_cursorpos >= BEGIN_LINE_4) && (lcd_cursorpos <= END_LINE_4))
    {
        lcd_goto(BEGIN_LINE_1 + (lcd_cursorpos - BEGIN_LINE_4));
    }
}

void lcd_gotoxy(byte x, byte y)
{
    switch(y)
    {
        case 0:
            lcd_goto(BEGIN_LINE_1 + x);
            break;
        case 1:
            lcd_goto(BEGIN_LINE_2 + x);
            break;
        case 2:
            lcd_goto(BEGIN_LINE_3 + x);
            break;
        case 3:
            lcd_goto(BEGIN_LINE_4 + x);
            break;
    }
}

byte rs232_getch(void) 
{
    byte in;

    while(rx_buffer_len == 0) continue;
    --rx_buffer_len;
    in = rx_buffer[rx_buffer_out];
    ++rx_buffer_out;
    if (rx_buffer_out >= RS232_BUF_SIZE)
    {
        rx_buffer_out -= RS232_BUF_SIZE;
    }
    return in;
}

void disp_cmd(byte cmd)
{
    if (disp_opcode > 0)
    {
        // Process character as operation data
        disp_datalist[disp_datalen] = cmd;
        ++disp_datalen;
        
        if (disp_data() != 0)
        {
            // Operation completed, reset opcode and data
            disp_opcode = 0;
            disp_datalen = 0;
        }
    } else if (cmd < 0x20)
    {
        switch(cmd)
        {
            case 0x01:
                lcd_putcmd(0x02);
                lcd_cursorpos = BEGIN_LINE_1;
                break;
            case 0x02:
                lcd_display = 0;
                lcd_setdisplay();
                break;
            case 0x03:
                lcd_display = 1;
                lcd_setdisplay();
                break;
            case 0x04:
                lcd_cursor = 0;
                lcd_cursorblink = 0;
                lcd_setdisplay();
                break;
            case 0x05:
                lcd_cursor = 1;
                lcd_cursorblink = 0;
                lcd_setdisplay();
                break;
            case 0x06:
                lcd_cursor = 0;
                lcd_cursorblink = 1;
                lcd_setdisplay();
                break;
            case 0x07:
                lcd_cursor = 1;
                lcd_cursorblink = 1;
                lcd_setdisplay();
                break;
            case 0x08:
                lcd_putcmd(0x10); //left
                lcd_dec_cursor();
                lcd_putch(0x20); //space
                lcd_inc_cursor();
                lcd_putcmd(0x10); //left
                lcd_dec_cursor();
                break;
            case 0x0A:
                lcd_cursordown();
                break;
            case 0x0B:
                lcd_putcmd(0x10); //left
                lcd_dec_cursor();
                lcd_putch(0x20); //space
                lcd_inc_cursor();
                break;
            case 0x0C:
                lcd_clear();
                break;
            case 0x0D:
                if (lcd_cursorpos <= END_LINE_1)
                {
                    lcd_goto(BEGIN_LINE_1);
                } else if ((lcd_cursorpos >= BEGIN_LINE_2) && (lcd_cursorpos <= END_LINE_2))
                {
                    lcd_goto(BEGIN_LINE_2);
                } else if ((lcd_cursorpos >= BEGIN_LINE_3) && (lcd_cursorpos <= END_LINE_3))
                {
                    lcd_goto(BEGIN_LINE_3);
                } else if ((lcd_cursorpos >= BEGIN_LINE_4) && (lcd_cursorpos <= END_LINE_4))
                {
                    lcd_goto(BEGIN_LINE_4);
                }
                break;
            case 0x10:
            case 0x13:
            case 0x14:
                break;
            case 0x17:
                lcd_wrap = 1;
                break;
            case 0x18:
                lcd_wrap = 0;
                break;
            case 0x1A:
                // Reboot
                init_lcd();
                disp_bignumbers = 0;
                disp_bootmsg();
                break;
            case 0x1F:
                // Show information screen
                lcd_clear();
                lcd_puts("CFA634 Emulator");
                lcd_goto(BEGIN_LINE_2);
                lcd_puts(FIRMWARE);
                lcd_goto(lcd_cursorpos);
                disp_bignumbers = 0;
                break;
            default:
                disp_datalen = 0;
                disp_opcode = cmd;
                break;
        }
    } else {
        // Output character
        if ((cmd >= 0x80) && (cmd < 0xA0)) 
        {
            cmd &= 0x07; // Map the custom characters
        }
        lcd_putch(cmd);
        lcd_inc_cursor();
    }
}

byte disp_data()
{
    switch (disp_opcode)
    {
        case 0x09:
            return 1;
        case 0x0E:
            lcd_setbacklight(disp_datalist[0]);
            return 1;
        case 0x0F:
            return 1;
        case 0x11:
            if (disp_datalen == 2)
            {
                lcd_gotoxy(disp_datalist[0], disp_datalist[1]);
                return 1;
            }
            return 0;
        case 0x12:
            if (disp_datalen == 6)
            {
                disp_createbar();
                return 1;
            }
            return 0;
        case 0x15:
            if (disp_datalen == 2)
            {
                // Write marquee character
                if ((disp_datalist[1] >= 0x80) && (disp_datalist[1] < 0xA0)) 
                {
                    disp_datalist[1] &= 0x07; // Map the custom characters
                }
                if (disp_datalist[0] < 21)
                {
                    disp_marquee[disp_datalist[0]] = disp_datalist[1];
                }
                return 1;
            }
            return 0;
        case 0x16:
            if (disp_datalen == 3)
            {
                // Set marquee speed
                if (disp_datalist[1] < 0x07)
                {
                    disp_marquee_speedmul = (7 - disp_datalist[1]) * 10;
                    disp_marquee_speed = disp_datalist[2];
                    disp_marquee_line = disp_datalist[0];
                }
                return 1;
            }
            return 0;
        case 0x19:
            if (disp_datalen == 9)
            {
                disp_loadudg();
                return 1;
            }
            return 0;
        case 0x1B:
            if (disp_datalen == 2)
            {
                if (disp_datalist[0] == 0x5B)
                {
                    switch (disp_datalist[1])
                    {
                        case 0x41:
                            lcd_cursorup();
                            break;
                        case 0x42:
                            lcd_cursordown();
                            break;
                        case 0x43:
                            lcd_cursorpos++;
                            lcd_putcmd(0x14);
                            break;
                        case 0x44:
                            lcd_cursorpos--;
                            lcd_putcmd(0x10);
                            break;
                    }
                }
                return 1;
            }
            return 0;
        case 0x1C:
            if (disp_datalen == 3)
            {
                disp_drawnumbers();
                return 1;
            }
            return 0;
        case 0x1D:
            if (disp_datalen == 2)
            {
                // Send command or data to LCD controller directly
                switch (disp_datalist[0])
                {
                    case 0x01:
                        lcd_putch(disp_datalist[1]);
                        break;
                    default:
                        lcd_putcmd(disp_datalist[1]);
                        break;
                }
                return 1;
            }
            return 0;
    }
    return 0;
}

void disp_createbar(void)
{
    // Check "disp_datalist[0]" as graph_index
    // Check "disp_datalist[5]" as row
    // Check "disp_datalist[3]" as end_column
    // Check "disp_datalist[2]" as start_column less than end_column
    if ((disp_datalist[0] < 0x04) && (disp_datalist[5] < 0x04) &&
        (disp_datalist[3] < 20) && (disp_datalist[2] <= disp_datalist[3]))
    {
        byte a;
        byte b;
        signed char temp;
        signed char a_length;
        signed char b_length;
        byte part_width;
        byte bar_length;
        byte negative;
        byte length = (disp_datalist[3] - disp_datalist[2]);
        byte cursorpos = lcd_cursorpos;

        // Create "bulk" bar UDG
        disp_createbar_udg((disp_datalist[0] << 1), 0xFF, disp_datalist[1]);

        // Calculate length
        if (disp_datalist[4] & 0x80)
        {
            //negative
            disp_datalist[4] = ((~disp_datalist[4]) + 1) & 0x7F;
            negative = 1;
        } else {
            negative = 0;
        }
        bar_length = disp_datalist[4] / 6;
        part_width = disp_datalist[4] - (6 * bar_length);
        if (part_width == 0)
        {
            part_width = 6;
            a_length = bar_length - 1;
        } else {
            a_length = bar_length;
        }

        // Create partial bar UDG
        if (negative != 0)
        {
            disp_createbar_udg((disp_datalist[0] << 1) + 1, 
                0x1F >> (6 - part_width), disp_datalist[1]);
            
            // Swap bar direction for right-to-left
            a = 0x20;
            b = (disp_datalist[0] << 1);
            b_length = a_length;
            a_length = length - b_length;
        } else {
            disp_createbar_udg((disp_datalist[0] << 1) + 1, 
                0x1F << (6 - part_width), disp_datalist[1]); 

            // Bar direction is left-to-right
            a = (disp_datalist[0] << 1);
            b = 0x20;
            b_length = length - a_length;
        }

        // Draw left side (UDG or space)
        lcd_gotoxy(disp_datalist[2], disp_datalist[5]);
        for (temp = 0; temp < a_length; ++temp)
        {
            lcd_putch(a);
        }
        
        // Draw partial UDG
        lcd_putch((disp_datalist[0] << 1) + 1);
        
        // Draw right side (space or UDG)
        for (temp = 0; temp < b_length; ++temp)
        {
            lcd_putch(b);
        }

        // Flag as user-defined UDGs
        disp_bignumbers = 0;
        
        // Restore cursor
        lcd_goto(cursorpos);
    }
}

void disp_createbar_udg(byte n, byte x, byte y)
{
    byte temp;
    byte line;
    
    // n = 0x40 + (n << 3)
    n <<= 3;
    n += 0x40;
    
    for (temp = 0; temp < 8; ++temp)
    {
        if (y & 0x80)
        {
            line = x;
        } else {
            line = 0x00;
        }
        lcd_putcmd(n + temp);
        lcd_putch(line);
        y = (y << 1) & 0xFE;
    }
}

void disp_drawmarquee(void)
{
    if (disp_marquee_line < 0x04)
    {
        char end;
        byte temp;
        byte cursorpos = lcd_cursorpos;

        lcd_gotoxy(0, disp_marquee_line);
        end = lcd_getch();
        for (temp = 1; temp < 20; ++temp)
        {
            char ch;

            lcd_gotoxy(temp, disp_marquee_line);
            ch = lcd_getch();
            lcd_gotoxy(temp - 1, disp_marquee_line);
            lcd_putch(ch);
        }
        lcd_gotoxy(19, disp_marquee_line);
        lcd_putch(disp_marquee[0]);
        for (temp = 1; temp < 20; ++temp)
        {
            disp_marquee[temp - 1] = disp_marquee[temp];
        }
        disp_marquee[19] = end;

        // Restore cursor
        lcd_goto(cursorpos);
    }
}

void disp_loadudg(void)
{
    // Load the UDG data for character "disp_datalist[0]"
    if (disp_datalist[0] < 0x08)
    {
        byte temp;
        
        // disp_datalist[0] = 0x3F + (disp_datalist[0] << 3)
        // NOTE: 0x3F + 1 = 0x40 base address
        disp_datalist[0] <<= 3;
        disp_datalist[0] += 0x3F;
        for (temp = 1; temp < 9; ++temp)
        {
            lcd_putcmd(disp_datalist[0] + temp);
            lcd_putch(disp_datalist[temp]);
        }

        // Restore cursor
        lcd_goto(lcd_cursorpos);
    }
    
    // Flag as user-defined UDGs
    disp_bignumbers = 0;
}

void disp_drawnumbers(void)
{
    // Check "disp_datalist[0]" as style
    // Check "disp_datalist[1]" as column
    // Check "disp_datalist[2]" as number
    if ((disp_datalist[0] < 2) && (disp_datalist[1] < 18) &&
        (disp_datalist[2] > 47) && (disp_datalist[2] < 58))
    {
        byte i;
        byte j;
        byte cursorpos = lcd_cursorpos;

        // Load big numbers segments as UDGs
        if (disp_bignumbers == 0)
        {
            for (i = 0; i < 8; ++i)
            {
                byte temp = 0x40 + (i << 3);
                for (j = 0; j < 8; ++j)
                {
                    lcd_putcmd(temp + j);
                    lcd_putch(disp_big_blocks[i][j]);
                }
            }
            
            // Flag as big number segment UDGs
            disp_bignumbers = 1;
        }

        // Write out 3x4 character
        disp_datalist[2] -= 48;
        for (i = 0; i < 3; ++i)
        {
            for (j = 0; j < 2; ++j)
            {
                byte code = disp_big_numbers[disp_datalist[2]][(i << 1) + j];
                byte bottom = code & 0x0F;
                byte top = code >> 4;
                lcd_gotoxy(disp_datalist[1] + i, (j << 1));
                if (top == 0x08)
                {
                    lcd_putch(0x20);
                } else {
                    lcd_putch(top);
                }
                lcd_gotoxy(disp_datalist[1] + i, (j << 1) + 1);
                if (bottom == 0x08)
                {
                    lcd_putch(0x20);
                } else {
                    lcd_putch(bottom);
                }
            }
        }

        // A "4x4 style" character only clears the 4th column
        if (disp_datalist[0] == 1)
        {
            for (i = 0; i < 4; ++i)
            {
                lcd_gotoxy(disp_datalist[1] + 3, i);
                lcd_putch(0x20);
            }
        }

        // Restore cursor
        lcd_goto(cursorpos);
    }
}

void disp_bootmsg(void)
{
    byte temp;
    lcd_setbacklight(100);
    lcd_puts("CFA634 Emulator");
    lcd_goto(BEGIN_LINE_2);
    lcd_puts(FIRMWARE);
    lcd_goto(lcd_cursorpos);
    delayS(1);
    for (temp = 33; temp < 113; ++temp)
    {
        lcd_putch(temp);
        lcd_inc_cursor();
    }
    delayS(1);
    lcd_clear();
}
