#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// fuses -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m

#define SLEEP_TIMER_DURATION 4
#define WAKEUP_TIMER_DURATION 24

#define FAST_FADE_TIME_MS 1500
#define PRESCALE 16384 // might be too high for second-level accuracy
#define ON_MAX_BRIGHT 255
#define OFF_MIN_BRIGHT 0

#define OVF_PER_HOUR ((F_CPU / PRESCALE) * (60 * 60) / 0xff)

#if SLEEP_TIMER_DURATION > WAKEUP_TIMER_DURATION
#error Sleep timer duration must be smaller than wakeup timer duration
#endif

#define FAST_FADE_STEP_DURATION (FAST_FADE_TIME_MS / ON_MAX_BRIGHT)

// PB3 - 
// PB4 - 
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
    if (PINB & _BV(PORTB2)) {
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
}

static void lights_on(void) {
    if (OCR0B == ON_MAX_BRIGHT) {
        return;
    }
    for (int32_t i = OCR0B; i <= ON_MAX_BRIGHT && state == STATE_ON; i++) {
        OCR0B = i;
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

ISR(PCINT0_vect, ISR_BLOCK) {
    uint8_t button_state = PINB & _BV(PORTB0);
    mode_t new_mode = get_mode();
    if (new_mode != mode) {
        mode = new_mode;
    } else if (!button_state) {
        // if button is pressed
        if (state == STATE_OFF) {
            state = STATE_ON;
        } else if (state == STATE_ON) {
            state = STATE_OFF;
        }
    }
}

ISR(TIMER1_OVF_vect, ISR_BLOCK) {
    timer_tick(&wakeup_timer);
    timer_tick(&sleep_timer);
}

int main(void) {
    DDRB |= _BV(PORTB1); // LED output
    DDRB &= ~(_BV(PORTB2) | _BV(PORTB0)); // mode switch input / button input
    PORTB |= _BV(PORTB2) | _BV(PORTB0); // pull ups

    // external interrupt on falling edge
    MCUCR |= _BV(ISC01);
    GIMSK |= _BV(INT0) | _BV(PCIE);
    GIFR |= _BV(INTF0) | _BV(PCIF);
    PCMSK |= _BV(PCINT2);

    mode = get_mode();
    state = STATE_OFF;

    wakeup_timer.flags = 0;
    wakeup_timer.ovf_count = 0;
    wakeup_timer.ovf_max = OVF_PER_HOUR * WAKEUP_TIMER_DURATION;
    
    sleep_timer.flags = 0;
    sleep_timer.ovf_count = 0;
    sleep_timer.ovf_max = OVF_PER_HOUR * SLEEP_TIMER_DURATION;

    sei();

    // timer config
    GTCCR = 0;
    TIMSK = _BV(TOIE1);

#if PRESCALE == 16384
    TCCR1 = _BV(CS13) | _BV(CS12) | _BV(CS11) | _BV(CS10);
#else
#error bad prescale value
#endif

    TCCR0A = _BV(COM0B1) | _BV(WGM00);
    TCCR0B = _BV(CS00);
    OCR0B = 128;

    state_t old_state = state;
    mode_t old_mode = mode;
    while (1) {
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
    }

    return 0;
}








