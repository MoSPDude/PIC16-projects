
#include <pic.h>

__CONFIG(FOSC_INTOSCIO & WDTE_OFF & LVP_OFF & BOREN_OFF & MCLRE_OFF & PWRTE_ON);

#define XTAL    4000000     // Crystal frequency (Hz).
#define BRATE   1200L       // Baud rate. "L" prevents truncation in calculations.
#define RX_OVERSAMPLE   4   // Receive oversampling. Must be at least 4 and power of two

#define byte unsigned char

// Push button
// NOTE: RA5 has no output driver!
static volatile bit PushButton @ ((unsigned)&PORTA*8+5);

// Don't change these
#define TIMER_VALUE     (XTAL / (4 * BRATE * RX_OVERSAMPLE))
#define INT_PERIOD      (1000000 / (BRATE * RX_OVERSAMPLE)) // in microseconds

// Delay constants used by routines
#define T_1000_US    (1000 / INT_PERIOD + 1)
#define T_50_MS    (50000 / INT_PERIOD + 1)

#if ((TIMER_VALUE) < 90)
#error baud rate or oversample too high for crystal speed
#endif

#if ((TIMER_VALUE) > 255)
#error baud rate or oversample too low for crystal speed
#endif

#if ((T_1000_US) > 255)
#error T_1000_US overflow
#endif

#if ((T_50_MS) > 255)
#error T_50_MS overflow
#endif

static byte led_porta;
static byte led_portb;
static byte led_brightness[15];
static byte led_shadowbright[15];
static byte scene_brightness[15];
static volatile byte scene;

// Timer variables and routines
static volatile byte delaycount;
static volatile byte scenecount;
static volatile byte scenecount_a;
static volatile byte scenecount_b;
static volatile byte led_trigger;
static volatile bit led_swapover;
static volatile bit scene_changed;

// Scene times - in 12 second units, so 0x05 per minute
static const byte scene_times[8] = { 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x23, 0x23 };

interrupt void isr(void)
{
    // +2 as timer stops for 2 cycles when writing to TMR0
    TMR0 += -TIMER_VALUE + 2;
    T0IF = 0;
    
    // Enable the LEDs
    // These are calculated by the previous interrupt, and
    // shown now to be as close to the timestep as possible.
    // The LED is on when the output pin is pulled LOW
    PORTA = ~led_porta;
    PORTB = ~led_portb;
    
    // Double buffer the LEDs
    // This allows code to prepare the next image, whilst handling
    // the LED brightness of the current image
    if (led_swapover != 0)
    {
        for (int i = 0; i < 15; ++i) led_shadowbright[i] = led_brightness[i];
        led_swapover = 0;
    }

    // LED BRIGHTNESS
    // PWM controlled - LED is on, when the counter is less than the value
    // LED value is 0x00 = 0%, 0x40 = 100% brightness
    led_porta = 0x00;
    led_portb = 0x00;
    if (led_trigger < led_shadowbright[0])
    {
        led_portb |= 0x08; // RB3
    }
    if (led_trigger < led_shadowbright[1])
    {
        led_portb |= 0x10; // RB4
    }
    if (led_trigger < led_shadowbright[2])
    {
        led_portb |= 0x20; // RB5
    }
    if (led_trigger < led_shadowbright[3])
    {
        led_portb |= 0x40; // RB6
    }
    if (led_trigger < led_shadowbright[4])
    {
        led_portb |= 0x80; // RB7
    }
    if (led_trigger < led_shadowbright[5])
    {
        led_porta |= 0x40; // RA6
    }
    if (led_trigger < led_shadowbright[6])
    {
        led_portb |= 0x01; // RB0
    }
    if (led_trigger < led_shadowbright[7])
    {
        led_portb |= 0x02; // RB1
    }
    if (led_trigger < led_shadowbright[8])
    {
        led_portb |= 0x04; // RB2
    }
    if (led_trigger < led_shadowbright[9])
    {
        led_porta |= 0x80; // RA7
    }
    if (led_trigger < led_shadowbright[10])
    {
        led_porta |= 0x01; // RA0
    }
    if (led_trigger < led_shadowbright[11])
    {
        led_porta |= 0x02; // RA1
    }
    if (led_trigger < led_shadowbright[12])
    {
        led_porta |= 0x04; // RA2
    }
    if (led_trigger < led_shadowbright[13])
    {
        led_porta |= 0x08; // RA3
    }
    if (led_trigger < led_shadowbright[14])
    {
        led_porta |= 0x10; // RA4
    }
    led_trigger = (led_trigger + 1) & 0x3F;

    // COUNTERS
    // Simple countdown timer
    if (delaycount > 0)
    {
        delaycount--;
    }

    // Scene timer
    // Countdown the time since the scene has been runnning,
    // whilst not in the middle of a scene change
    if (!scene_changed)
    {
        // 50ms * (239 + 1) = 12 seconds, by scenecount and scenecount_a.
        // scenecount_b is the count of 12 seconds, so 0x05 per minute
        if (scenecount > 0)
        {
            scenecount--;
        } else {
            if (scenecount_a > 0)
            {
                --scenecount_a;
            } else {
                if (scenecount_b > 0)
                {
                    --scenecount_b;
                } else {
                    // Countdown has reached zero, so indicate 
                    // request for scene change
                    scene_changed = 1;
                }
                scenecount_a = 0xEF;
            }
            scenecount = T_50_MS;
        }
    }
}

void delay50Ms(byte t)
{
    // Wait in a loop for 50ms
    do {
        delaycount = T_50_MS;
        while(delaycount);
    } while(--t);
}

/*void delayS(byte t)
{
    // Wait in a loop for 20 * 50ms = 1000ms
    do {
        delay50Ms(20);
    } while(--t);
}*/

void set_led_swapover(void)
{
    // Indicate to copy the image buffer over
    // as now ready to show
    while (led_swapover != 0);
    led_swapover = 1;
}

void set_led_brightness(byte value_)
{
    // Set the brightness of all LEDs the same,
    // and show
    for (int i = 0; i < 15; ++i) led_brightness[i] = value_;
    set_led_swapover();
}

byte get_led_twinkle(byte index)
{
    // Gives a brightness ramp going up, then down, over a
    // range of 0x00 to 0x3F - with a pause at 0% and 100%
    // Value at 0x00 and 0x3F = 0%, 0x20 = 100% brightness
    if (index < 0x02)
    {
        return 0x00;
    } else if (index < 0x1E)
    {
        index = index - 0x01;
        return (index << 1);
    } else if (index < 0x22)
    {
        return 0x40;
    } else if (index < 0x3E)
    {
        index = (index & 0x1F) + 0x02;
        return 0x40 - (index << 1);
    } else {
        return 0x00;
    }
}

// Scene 0 - flashing
// All LEDs ramp up, then down, together

void scene_zero_init(void)
{
    scene_brightness[0] = 0x00;
}

void scene_zero(void)
{
    set_led_brightness(get_led_twinkle(scene_brightness[0]));
    scene_brightness[0] = (scene_brightness[0] + 1) & 0x3F;
    delay50Ms(1);
}

// Scene 1 - starburst
// One LEDs down each arm lights up from the centre outwards

void scene_one_init(void)
{
    set_led_brightness(0x00);
    led_brightness[2] = 0x40;
    led_brightness[5] = 0x40;
    led_brightness[8] = 0x40;
    led_brightness[11] = 0x40;
    led_brightness[14] = 0x40;
    set_led_swapover();
    delay50Ms(3);
}

void scene_one(void)
{
    byte temp;
    temp = led_brightness[0];
    led_brightness[0] = led_brightness[1];
    led_brightness[1] = led_brightness[2];
    led_brightness[2] = temp;
    temp = led_brightness[3];
    led_brightness[3] = led_brightness[4];
    led_brightness[4] = led_brightness[5];
    led_brightness[5] = temp;
    temp = led_brightness[6];
    led_brightness[6] = led_brightness[7];
    led_brightness[7] = led_brightness[8];
    led_brightness[8] = temp;
    temp = led_brightness[9];
    led_brightness[9] = led_brightness[10];
    led_brightness[10] = led_brightness[11];
    led_brightness[11] = temp;
    temp = led_brightness[12];
    led_brightness[12] = led_brightness[13];
    led_brightness[13] = led_brightness[14];
    led_brightness[14] = temp;
    set_led_swapover();
    delay50Ms(3);
}

// Scene 2 - one arm rotating
// One arm of LEDs spin around

void scene_two_init(void)
{
    set_led_brightness(0x00);
    led_brightness[0] = 0x40;
    led_brightness[1] = 0x40;
    led_brightness[2] = 0x40;
    led_brightness[3] = 0x20;
    led_brightness[4] = 0x20;
    led_brightness[5] = 0x20;
    led_brightness[6] = 0x10;
    led_brightness[7] = 0x10;
    led_brightness[8] = 0x10;
    set_led_swapover();
    delay50Ms(3);
}

void scene_two(void)
{
    byte temp;
    temp = led_brightness[0];
    led_brightness[0] = led_brightness[3];
    led_brightness[3] = led_brightness[6];
    led_brightness[6] = led_brightness[9];
    led_brightness[9] = led_brightness[12];
    led_brightness[12] = temp;
    temp = led_brightness[1];
    led_brightness[1] = led_brightness[4];
    led_brightness[4] = led_brightness[7];
    led_brightness[7] = led_brightness[10];
    led_brightness[10] = led_brightness[13];
    led_brightness[13] = temp;
    temp = led_brightness[2];
    led_brightness[2] = led_brightness[5];
    led_brightness[5] = led_brightness[8];
    led_brightness[8] = led_brightness[11];
    led_brightness[11] = led_brightness[14];
    led_brightness[14] = temp;
    set_led_swapover();
    delay50Ms(3);
}

// Scene 3 - catherine wheel
// The LEDs light up from the center outwards, on then off,
// like a Catherine Wheel

void scene_three_init(void)
{
    for (int i = 0; i < 12; ++i) scene_brightness[i] = 0x40;
    scene_brightness[12] = 0x30;
    scene_brightness[13] = 0x18;
    scene_brightness[14] = 0x00;
    set_led_brightness(0x00);
    delay50Ms(2);
}

void scene_three(void)
{
    byte temp;
    temp = led_brightness[12];
    led_brightness[12] = led_brightness[9];
    led_brightness[9] = led_brightness[6];
    led_brightness[6] = led_brightness[3];
    led_brightness[3] = led_brightness[0];
    led_brightness[0] = led_brightness[13];
    led_brightness[13] = led_brightness[10];
    led_brightness[10] = led_brightness[7];
    led_brightness[7] = led_brightness[4];
    led_brightness[4] = led_brightness[1];
    led_brightness[1] = led_brightness[14];
    led_brightness[14] = led_brightness[11];
    led_brightness[11] = led_brightness[8];
    led_brightness[8] = led_brightness[5];
    led_brightness[5] = led_brightness[2];
    led_brightness[2] = scene_brightness[0];
    for (int i = 0; i < 14; ++i) scene_brightness[i] = scene_brightness[i+1];
    scene_brightness[14] = temp;
    set_led_swapover();
    delay50Ms(2);
}

// Scene 4 - spinning and flashing
// The inner 5 LEDs spin around, while the outer LEDs twinkle together

void scene_four_init(void)
{
    scene_brightness[0] = 0x00;
    set_led_brightness(0x00);
    led_brightness[2] = 0x40;
    led_brightness[5] = 0x15;
    led_brightness[8] = 0x05;
    set_led_swapover();
    delay50Ms(2);
}

void scene_four(void)
{
    byte temp;
    temp = led_brightness[2];
    led_brightness[2] = led_brightness[5];
    led_brightness[5] = led_brightness[8];
    led_brightness[8] = led_brightness[11];
    led_brightness[11] = led_brightness[14];
    led_brightness[14] = temp;
    temp = get_led_twinkle(scene_brightness[0]);
    led_brightness[0] = temp;
    led_brightness[1] = temp;
    led_brightness[3] = temp;
    led_brightness[4] = temp;
    led_brightness[6] = temp;
    led_brightness[7] = temp;
    led_brightness[9] = temp;
    led_brightness[10] = temp;
    led_brightness[12] = temp;
    led_brightness[13] = temp;
    scene_brightness[0] = (scene_brightness[0] + 2) & 0x3F;
    set_led_swapover();
    delay50Ms(2);
}

// Scene 5 - spinning and bursting
// The inner 5 LEDs spin around, and burst "randomly" from the head

void scene_five_init(void)
{
    set_led_brightness(0x00);
    led_brightness[2] = 0x40;
    led_brightness[5] = 0x15;
    led_brightness[8] = 0x05;
    set_led_swapover();
    delay50Ms(2);
}

void scene_five(void)
{
    byte temp;
    temp = ((scenecount_a ^ scenecount) & 0x06);
    led_brightness[0] = led_brightness[1];
    if (led_brightness[2] < 0x40)
    {
        led_brightness[1] = 0x00;
    } else if (temp == 0x00)
    {
        led_brightness[1] = 0x40;
    }
    led_brightness[3] = led_brightness[4];
    if (led_brightness[5] < 0x40)
    {
        led_brightness[4] = 0x00;
    } else if (temp == 0x00)
    {
        led_brightness[4] = 0x40;
    }
    led_brightness[6] = led_brightness[7];
    if (led_brightness[8] < 0x40)
    {
        led_brightness[7] = 0x00;
    } else if (temp == 0x00)
    {
        led_brightness[7] = 0x40;
    }
    led_brightness[9] = led_brightness[10];
    if (led_brightness[11] < 0x40)
    {
        led_brightness[10] = 0x00;
    } else if (temp == 0x00)
    {
        led_brightness[10] = 0x40;
    }
    led_brightness[12] = led_brightness[13];
    if (led_brightness[14] < 0x40)
    {
        led_brightness[13] = 0x00;
    } else if (temp == 0x00)
    {
        led_brightness[13] = 0x40;
    }
    temp = led_brightness[2];
    led_brightness[2] = led_brightness[5];
    led_brightness[5] = led_brightness[8];
    led_brightness[8] = led_brightness[11];
    led_brightness[11] = led_brightness[14];
    led_brightness[14] = temp;
    set_led_swapover();
    delay50Ms(2);
}

// Scene 6 - gentle flicker
// All LEDs are dimmed, but then each starts to "randomly" glow

void scene_six_init(void)
{
    for (int i = 0; i < 15; ++i) scene_brightness[i] = 0x04;
    set_led_brightness(0x06);
    delay50Ms(1);
}

void scene_six(void)
{
    for (int i = 0; i < 15; ++i)
    {
        // Update brightness index
        if (scene_brightness[i] > 0x04)
        {
            ++scene_brightness[i];
            if (scene_brightness[i] > 0x3a)
            {
                scene_brightness[i] = 0x05;
            }
        } else if (((scenecount_a ^ scenecount) & 0x0F) == i)
        {
            scene_brightness[i] = 0x05;
        }
        
        // Update brightness
        led_brightness[i] = get_led_twinkle(scene_brightness[i]);
    }
    set_led_swapover();
    delay50Ms(1);
}

// Secene 7 - twinkling
// All LEDs are off, but then each starts to "randomly" twinkle

void scene_seven_init(void)
{
    for (int i = 0; i < 15; ++i) scene_brightness[i] = 0;
    set_led_brightness(0x00);
    delay50Ms(1);
}

void scene_seven(void)
{
    for (int i = 0; i < 15; ++i)
    {
        // Update brightness index
        if (scene_brightness[i] != 0x00)
        {
            scene_brightness[i] = (scene_brightness[i] + 1) & 0x3F;
        } else if (((scenecount_a ^ scenecount) & 0x0F) == i)
        {
            scene_brightness[i] = 1;
        }
        
        // Update brightness
        led_brightness[i] = get_led_twinkle(scene_brightness[i]);
    }
    set_led_swapover();
    delay50Ms(1);
}

void init_chip(void)
{
    PORTA = 0x00; // clear output latches
    PORTB = 0x00;
    CMCON = 0x07;       // disable comparator
    TRISA = 0b00100000; // PORTA inputs
    TRISB = 0b00000000; // PORTB outputs

    /* Set up ISR variables */
    for (int i = 0; i < 15; ++i)
    {
        led_brightness[i] = 0x00;
        led_shadowbright[i] = 0x00;
    }
    led_trigger = 0x00;
    led_porta = 0x00;
    led_portb = 0x00;
    delaycount = 0x00;

    // Start with a scene change into scene zero
    scene_changed = 1;
    scene = 0x07;
    scenecount = 0x00;
    scenecount_a = 0x00;
    scenecount_b = 0x00;

    // The LED is on when the output pin is pulled LOW
    PORTA = 0xFF;
    PORTB = 0xFF;

    // Set up the timer
    TMR1ON = 0;
    TMR2ON = 0;
    T0CS = 0; // Set timer mode for Timer0
    TMR0 = (2-TIMER_VALUE); // +2 as timer stops for 2 cycles when writing to TMR0
    T0IE = 1; // Enable the Timer0 interrupt
    GIE = 1; // Enable interrupts
}

void main(void)
{
    init_chip();

    // Main loop
    while (1)
    {
        // Detect if the button is pressed
        if (!PushButton)
        {
            // Indicate a request for scene change
            scene_changed = 1;
            
            // Turn off the LEDs
            set_led_brightness(0x00);
            
            // Wait for button to be released
            do {
                delay50Ms(1);
            } while (!PushButton);
            delay50Ms(1);
        }

        // Handle request for scene change
        if (scene_changed)
        {
            // Move to the next scene
            scene = (scene + 1) & 0x07;
            
            // Set the scene countdown
            scenecount = 0x00;
            scenecount_a = 0x00;
            scenecount_b = scene_times[scene];
            
            // Start the scene
            scene_changed = 0;
            switch (scene)
            {
                case 0 :
                    scene_zero_init();
                    break;
                case 1 :
                    scene_one_init();
                    break;
                case 2 :
                    scene_two_init();
                    break;
                case 3 :
                    scene_three_init();
                    break;
                case 4 :
                    scene_four_init();
                    break;
                case 5 :
                    scene_five_init();
                    break;
                case 6 :
                    scene_six_init();
                    break;
                default :
                    scene_seven_init();
                    break;
            }
        }

        // Handle the scene loop
        switch (scene)
        {
            case 0 :
                scene_zero();
                break;
            case 1 :
                scene_one();
                break;
            case 2 :
                scene_two();
                break;
            case 3 :
                scene_three();
                break;
            case 4 :
                scene_four();
                break;
            case 5 :
                scene_five();
                break;
            case 6 :
                scene_six();
                break;
            default : 
                scene_seven();
                break;
        }
    }
}
