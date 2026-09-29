#include "app.h"
#include "config.h"
#include "gpio.h"
#include "pins.h"
#include "uart.h"
#include "millis.h"
#include "spi.h"
#include "buzzer.h"
#include "servo.h"
#include "mfrc522.h"
#include "74hc595.h"
#include "keypad.h"
#include "ds1307.h"
#include "twi.h"
#include "led.h"
#include "access_protocol.h"

#include <stdint.h>
#include <string.h>
#include <util/delay.h>

#define PIN_LENGTH 4
#define PIN_TIMEOUT_MS 10000
#define BACKEND_TIMEOUT_MS 3000

typedef enum
{
    STATE_IDLE,
    STATE_WAIT_REQ_PIN,
    STATE_WAIT_PIN,
    STATE_WAIT_ACCESS_RESULT
} app_state_t;

static app_state_t state = STATE_IDLE;

static uint16_t current_sid = 0;

static char pin_code[PIN_LENGTH + 1];
static uint8_t pin_index = 0;

static uint32_t state_start_time = 0;
static uint32_t last_yellow_blink = 0;

static uint8_t yellow_on = 0;
static uint8_t rfid_blocked = 0;
static uint8_t no_card_count = 0;

static char hex_digit(uint8_t value)
{
    if (value < 10)
    {
        return '0' + value;
    }

    return 'A' + (value - 10);
}

static void uid_to_string(const mfrc522_uid_t *uid, char *out)
{
    uint8_t i;

    for (i = 0; i < uid->size; i++)
    {
        out[i * 2] = hex_digit((uid->uid[i] >> 4) & 0x0F);
        out[i * 2 + 1] = hex_digit(uid->uid[i] & 0x0F);
    }

    out[uid->size * 2] = '\0';
}

static void led_off_all(void)
{
    red_led_off();
    green_led_off();
    blue_led_off();
}

static void led_red(void)
{
    led_off_all();
    red_led_on();
}

static void led_green(void)
{
    led_off_all();
    green_led_on();
}

static void led_blue(void)
{
    led_off_all();
    blue_led_on();
}

static void led_yellow(void)
{
    led_off_all();
    red_led_on();
    green_led_on();
}

static void blink_blue(void)
{
    uint8_t i;

    for (i = 0; i < 3; i++)
    {
        led_blue();
        _delay_ms(200);
        led_off_all();
        _delay_ms(200);
    }
}

static void blink_red(void)
{
    uint8_t i;

    for (i = 0; i < 3; i++)
    {
        led_red();
        _delay_ms(200);
        led_off_all();
        _delay_ms(200);
    }
}

static void reset_pin(void)
{
    pin_index = 0;
    pin_code[0] = '\0';
}

static void go_idle(void)
{
    state = STATE_IDLE;

    current_sid = 0;
    reset_pin();

    yellow_on = 0;

    buzzer_quiet();
    servo_close();
    led_red();
}

static void access_granted(void)
{
    led_green();
    servo_open();
    buzzer_scream();

    _delay_ms(5000);

    buzzer_quiet();
    servo_close();

    go_idle();
}

static void access_denied(void)
{
    blink_red();
    go_idle();
}

static void backend_timeout(void)
{
    blink_blue();
    go_idle();
}

static void start_pin_input(uint16_t sid)
{
    current_sid = sid;
    reset_pin();

    state = STATE_WAIT_PIN;
    state_start_time = millis();
    last_yellow_blink = millis();

    yellow_on = 1;
    led_yellow();
}

static void key_feedback(void)
{
    led_green();
    _delay_ms(100);
    led_yellow();
}

static void update_yellow_blink(void)
{
    if (millis() - last_yellow_blink >= 400)
    {
        last_yellow_blink = millis();

        if (yellow_on == 0)
        {
            led_yellow();
            yellow_on = 1;
        }
        else
        {
            led_off_all();
            yellow_on = 0;
        }
    }
}

static void handle_idle(void)
{
    uint8_t atqa[2];
    uint8_t atqa_len;
    mfrc522_uid_t uid;
    char uid_text[21];

    led_red();

    if (mfrc522_request_a(atqa, &atqa_len) != MFRC522_OK)
    {
        if (no_card_count < 20)
        {
            no_card_count++;
        }

        if (no_card_count >= 20)
        {
            rfid_blocked = 0;
        }

        return;
    }

    no_card_count = 0;

    if (rfid_blocked == 1)
    {
        return;
    }

    if (mfrc522_anticoll_select(&uid) != MFRC522_OK)
    {
        return;
    }

    rfid_blocked = 1;

    uid_to_string(&uid, uid_text);
    access_send_uid(uid_text);

    mfrc522_halt();

    state = STATE_WAIT_REQ_PIN;
    state_start_time = millis();

    led_off_all();
}

static void handle_wait_req_pin(void)
{
    access_message_t msg;
    access_status_t status;

    if (millis() - state_start_time > BACKEND_TIMEOUT_MS)
    {
        backend_timeout();
        return;
    }

    status = access_read_message(&msg);

    if (status != ACCESS_STATUS_OK)
    {
        return;
    }

    if (msg.command == ACCESS_CMD_REQ_PIN)
    {
        start_pin_input(msg.sid);
        return;
    }

    if (msg.command == ACCESS_CMD_ERR ||
        msg.command == ACCESS_CMD_LOCKED ||
        msg.command == ACCESS_CMD_NACK)
    {
        access_denied();
        return;
    }

    if (msg.command == ACCESS_CMD_TIMEOUT)
    {
        backend_timeout();
        return;
    }

    if (msg.command == ACCESS_CMD_PING)
    {
        access_send_pong();
        return;
    }
}

static void handle_wait_pin(void)
{
    char key;

    update_yellow_blink();

    if (millis() - state_start_time > PIN_TIMEOUT_MS)
    {
        go_idle();
        return;
    }

    key = keypad_get_key_debounced();

    if (key == 0)
    {
        return;
    }

    if (key == '*')
    {
        reset_pin();
        led_yellow();
        return;
    }

    if (key < '0' || key > '9')
    {
        return;
    }

    key_feedback();

    if (pin_index < PIN_LENGTH)
    {
        pin_code[pin_index] = key;
        pin_index++;
        pin_code[pin_index] = '\0';
    }

    if (pin_index == PIN_LENGTH)
    {
        access_send_pin(current_sid, pin_code);

        state = STATE_WAIT_ACCESS_RESULT;
        state_start_time = millis();

        led_off_all();
    }
}

static void handle_wait_access_result(void)
{
    access_message_t msg;
    access_status_t status;

    if (millis() - state_start_time > BACKEND_TIMEOUT_MS)
    {
        backend_timeout();
        return;
    }

    status = access_read_message(&msg);

    if (status != ACCESS_STATUS_OK)
    {
        return;
    }

    if (msg.command == ACCESS_CMD_OK)
    {
        access_granted();
        return;
    }

    if (msg.command == ACCESS_CMD_ERR ||
        msg.command == ACCESS_CMD_LOCKED ||
        msg.command == ACCESS_CMD_NACK)
    {
        access_denied();
        return;
    }

    if (msg.command == ACCESS_CMD_TIMEOUT)
    {
        backend_timeout();
        return;
    }

    if (msg.command == ACCESS_CMD_PING)
    {
        access_send_pong();
        return;
    }
}

void app_init(void)
{
    gpio_pin_output(&LED_DDR, LED_PIN);
    gpio_pin_low(&LED_PORT, LED_PIN);

    millis_init();
    uart_init(UART_BAUDRATE);

    spi_init();

    servo_init();
    buzzer_init();
    mfrc522_init();
    shift_register_init();

    keypad_init();
    twi_init(100000);

    init_led();
    access_init();

    rfid_blocked = 0;
    no_card_count = 0;

    go_idle();
}

void app_run(void)
{
    if (state == STATE_IDLE)
    {
        handle_idle();
    }
    else if (state == STATE_WAIT_REQ_PIN)
    {
        handle_wait_req_pin();
    }
    else if (state == STATE_WAIT_PIN)
    {
        handle_wait_pin();
    }
    else if (state == STATE_WAIT_ACCESS_RESULT)
    {
        handle_wait_access_result();
    }
}