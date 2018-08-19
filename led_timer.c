#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// ATtiny85 fuses -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m
// ATmega328p fuses -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U efuse:w:0xfd:m

#define SLEEP_TIMER_DURATION 1
#define WAKEUP_TIMER_DURATION 5

#define BUTTON_DEBOUNCE_SAMPLES (F_CPU / 400000) // 50 @ 20MHz
#define FAST_FADE_TIME_MS 1000
#define OFF_MIN_BRIGHT 0
#define ON_MAX_BRIGHT 0xff

#if _AVR_IOM328P_H_
// ATmega328p
#   define PRESCALE 1024 // might be too high for second-level accuracy
#else
// ATtiny85
#   define PRESCALE 16384 // might be too high for second-level accuracy
#endif

#define CALC_PRESCALE 16384
//#define OVF_PER_UNIT ((F_CPU / CALC_PRESCALE) * (60 * 60) / 0xff) // 1 hour
#define OVF_PER_UNIT ((F_CPU / CALC_PRESCALE) * (60) / 0xff) // 1 minute
//#define OVF_PER_UNIT ((F_CPU / CALC_PRESCALE) / 0xff) // 1 second

#if SLEEP_TIMER_DURATION > WAKEUP_TIMER_DURATION
#error Sleep timer duration must be smaller than wakeup timer duration
#endif

#define FAST_FADE_STEP_DURATION (FAST_FADE_TIME_MS / ON_MAX_BRIGHT)

// ATmega328p
// PD3 - mode switch PCINT2
// PB1 - LED
// PD2 - button

// ATtiny85
// PB2 - mode switch PCINT2
// PB1 - LED
// PB0 - button

typedef enum {
    STATE_OFF,
    STATE_ON,
} state_t;

typedef enum {
    MODE_NORMAL,
    MODE_AUTO,
} mode_t;

#if _AVR_IOM328P_H_
// ATmega328p
#   define MODE_PIN    PIND
#   define BUTTON_PIN  PIND

#   define MODE_DDR    DDRD
#   define BUTTON_DDR  DDRD
#   define LED_DDR     DDRD

#   define MODE_PORT   PORTD
#   define BUTTON_PORT PORTD
#   define LED_PORT    PORTD

#   define MODE        PORTD3
#   define BUTTON      PORTD2
#   define LED         PORTD5
#else
// ATtiny85
#   define MODE_PIN    PINB
#   define BUTTON_PIN  PINB

#   define MODE_DDR    DDRB
#   define BUTTON_DDR  DDRB
#   define LED_DDR     DDRB

#   define MODE_PORT   PORTB
#   define BUTTON_PORT PORTB
#   define LED_PORT    PORTB

#   define MODE        PORTB2
#   define BUTTON      PORTB0
#   define LED         PORTB1
#endif

#define TIMER_ENABLED   (1<<0)
#define TIMER_TRIGGERED (1<<1)

typedef struct {
    uint8_t flags;
    uint32_t ovf_count;
    uint32_t ovf_max;
} timer_t;

static volatile state_t state;
static volatile mode_t mode;
static volatile timer_t wakeup_timer;
static volatile timer_t sleep_timer;

static mode_t get_mode(void) {
    if (MODE_PIN & _BV(MODE)) {
        return MODE_NORMAL;
    } else {
        return MODE_AUTO;
    }
}

static void lights_off(void) {
    if (OCR0B == OFF_MIN_BRIGHT) {
        return;
    }
    for (int32_t i = OCR0B; i >= OFF_MIN_BRIGHT && state == STATE_OFF; i--) {
        OCR0B = i;
        _delay_ms(FAST_FADE_STEP_DURATION);
    }
    if (OCR0B == OFF_MIN_BRIGHT) {
        TCCR0A &= ~_BV(COM0B1);
    }
}

static void lights_on(void) {
    if (OCR0B == ON_MAX_BRIGHT) {
        return;
    }
    for (int32_t i = OCR0B; i <= ON_MAX_BRIGHT && state == STATE_ON; i++) {
        OCR0B = i;
        TCCR0A |= _BV(COM0B1);
        _delay_ms(FAST_FADE_STEP_DURATION);
    }
}

static void timer_tick(volatile timer_t *timer) {
    if (timer->flags & TIMER_ENABLED) {
        timer->ovf_count += 1;
        if (timer->ovf_count >= timer->ovf_max) {
            timer->flags &= ~TIMER_ENABLED;
            timer->flags |= TIMER_TRIGGERED;
        }
    }
}

static void timer_start(volatile timer_t *timer) {
    timer->flags &= ~TIMER_TRIGGERED;
    timer->ovf_count = 0;
    timer->flags |= TIMER_ENABLED;
}

static void timer_cancel(volatile timer_t *timer) {
    timer->flags &= ~(TIMER_TRIGGERED | TIMER_ENABLED);
}

static uint8_t timer_triggered(volatile timer_t *timer) {
    if (timer->flags & TIMER_TRIGGERED) {
        return 1;
    } else {
        return 0;
    }
}

static void _mode_interrupt(void) {
    mode_t new_mode = get_mode();
    if (new_mode != mode) {
        mode = new_mode;
    }
}

#if _AVR_IOM328P_H_ // ATmega328p
ISR(INT1_vect, ISR_BLOCK) {
    _mode_interrupt();
}
#else // ATtiny85
ISR(PCINT0_vect, ISR_BLOCK) {
    _mode_interrupt();
}
#endif

static volatile uint32_t ovf0_count = 0;
ISR(TIMER0_OVF_vect, ISR_BLOCK) {
    ovf0_count++;
}

ISR(TIMER1_OVF_vect, ISR_BLOCK) {
    timer_tick(&wakeup_timer);
    timer_tick(&sleep_timer);
}

int main(void) {

    DDRC |= _BV(PORTC5);

    LED_DDR |= _BV(LED); // LED output
    MODE_DDR &= ~_BV(MODE); // mode switch input
    BUTTON_DDR &= ~_BV(BUTTON); // button input
    MODE_PORT |= _BV(MODE); // pull ups
    //BUTTON_PORT |= _BV(BUTTON); // pull ups

    // external interrupt on falling edge
#if _AVR_IOM328P_H_
    // ATmega328p
    EICRA = _BV(ISC10) | _BV(ISC01);
    EIMSK = _BV(INT1);
    EIFR |= _BV(INTF0) | _BV(INTF1);
#else
    // ATtiny85
    MCUCR |= _BV(ISC01);
    GIMSK |= _BV(PCIE);
    GIFR |= _BV(PCIF);
    PCMSK |= _BV(PCINT2);
#endif

    mode = get_mode();
    state = STATE_OFF;

    wakeup_timer.flags = 0;
    wakeup_timer.ovf_count = 0;
    wakeup_timer.ovf_max = OVF_PER_UNIT * WAKEUP_TIMER_DURATION;
    
    sleep_timer.flags = 0;
    sleep_timer.ovf_count = 0;
    sleep_timer.ovf_max = OVF_PER_UNIT * SLEEP_TIMER_DURATION;

    sei();

    // timer config
#if _AVR_IOM328P_H_
    // ATmega328p
    TIMSK1 = _BV(TOIE1);
    TIMSK0 = _BV(TOIE0);

    ICR1 = 4080;

    TCCR1A = _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12);
#   if PRESCALE == 1024
    TCCR1B |= _BV(CS12) | _BV(CS10);
#   else
#   error bad prescale value
#   endif

    TCCR0A = _BV(WGM01) | _BV(WGM00);
    //TCCR0B = _BV(CS00);
    TCCR0B = _BV(CS01);

#else
    // ATtiny85
    GTCCR = 0;
    TIMSK = _BV(TOIE0) | _BV(TOIE1);

#   if PRESCALE == 16384
    TCCR1 = _BV(CS13) | _BV(CS12) | _BV(CS11) | _BV(CS10);
#   else
#   error bad prescale value
#   endif

    TCCR0A = _BV(WGM00);
    //TCCR0B = _BV(CS00);
    TCCR0B = _BV(CS01);
#endif
    
    OCR0B = 0;

    state_t old_state = state;
    mode_t old_mode = mode;
    uint8_t button_samples = 0;
    uint32_t debounce_start = 0;
    while (1) {

        // button debounce
        uint8_t button_state = BUTTON_PIN & _BV(BUTTON);
        if (!button_state) {
            if (button_samples == 0) {
                debounce_start = ovf0_count;
                button_samples = 1;
            } else if ((ovf0_count - debounce_start) >= BUTTON_DEBOUNCE_SAMPLES) {
                // if button is pressed
                if (state == STATE_OFF) {
                    state = STATE_ON;
                } else if (state == STATE_ON) {
                    state = STATE_OFF;
                }
                button_samples = 0;
            }
        } else {
            button_samples = 0;
        }


        // timer triggers
        if (timer_triggered(&wakeup_timer)) {
            timer_cancel(&wakeup_timer);
            state = STATE_ON;
        }
        if (timer_triggered(&sleep_timer)) {
            timer_cancel(&sleep_timer);
            state = STATE_OFF;
        }
        
        state_t now_state = state;
        mode_t now_mode = mode;

        if (old_state == STATE_ON && old_state != state) {
            // STATE_ON - on exit
            timer_cancel(&sleep_timer);
        }

        // state transitions
        if (state == STATE_ON) {
            if (old_state != STATE_ON) {
                // STATE_ON - on entry
                if (now_mode == MODE_AUTO) {
                    // start timers
                    timer_start(&wakeup_timer);
                    timer_start(&sleep_timer);
                }
            }
            lights_on();
        } else if (state == STATE_OFF) {
            lights_off();
        }


        // mode transitions
        if (old_mode != now_mode) {
            if (now_mode == MODE_NORMAL) {
                // MODE_NORMAL - on entry
                timer_cancel(&wakeup_timer);
                timer_cancel(&sleep_timer);
            } else if (now_mode == MODE_AUTO) {
                // MODE_AUTO - on entry
                if (state == STATE_ON) {
                    timer_start(&sleep_timer);
                }
            }
        }

        if (old_state != state) {
        }

        old_state = now_state;
        old_mode = now_mode;

        if (now_mode == MODE_NORMAL) {
            PORTC &= ~_BV(PORTC5);
        } else {
            PORTC |= _BV(PORTC5);
        }
    }

    return 0;
}








