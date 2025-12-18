#include "nst112.h"
#include "fm33lc0xx.h"
#include "fm33lc0xx_ll_gpio.h"

/* Simple bit-banged I2C for NST112 (standard-mode). */

static void i2c_delay(void)
{
    /* ~10-20us at 8MHz. Adjust if needed. */
    for (volatile int i = 0; i < 120; i++) {
        __NOP();
    }
}

static void scl_high(void) { LL_GPIO_SetOutputPin(NST112_SCL_PORT, NST112_SCL_PIN); }
static void scl_low(void)  { LL_GPIO_ResetOutputPin(NST112_SCL_PORT, NST112_SCL_PIN); }

static void sda_high(void) { LL_GPIO_SetOutputPin(NST112_SDA_PORT, NST112_SDA_PIN); }
static void sda_low(void)  { LL_GPIO_ResetOutputPin(NST112_SDA_PORT, NST112_SDA_PIN); }

static void sda_out(void)
{
    LL_GPIO_InitTypeDef io = {0};
    io.Pin        = NST112_SDA_PIN;
    io.Mode       = LL_GPIO_MODE_OUTPUT;
    io.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    io.Pull       = ENABLE;
    io.RemapPin   = DISABLE;
    LL_GPIO_Init(NST112_SDA_PORT, &io);
}

static void sda_in(void)
{
    LL_GPIO_InitTypeDef io = {0};
    io.Pin        = NST112_SDA_PIN;
    io.Mode       = LL_GPIO_MODE_INPUT;
    io.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    io.Pull       = ENABLE;
    io.RemapPin   = DISABLE;
    LL_GPIO_Init(NST112_SDA_PORT, &io);
}

static int sda_read(void)
{
    return (int)LL_GPIO_IsInputPinSet(NST112_SDA_PORT, NST112_SDA_PIN);
}

void NST112_GPIO_Init(void)
{
    LL_GPIO_InitTypeDef io = {0};

    /* SCL as open-drain output with pull-up */
    io.Pin        = NST112_SCL_PIN;
    io.Mode       = LL_GPIO_MODE_OUTPUT;
    io.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    io.Pull       = ENABLE;
    io.RemapPin   = DISABLE;
    LL_GPIO_Init(NST112_SCL_PORT, &io);

    /* SDA as open-drain output with pull-up (we will switch it to input for ACK/read) */
    sda_out();

    /* Idle state: both high */
    scl_high();
    sda_high();
}

static void i2c_start(void)
{
    sda_out();
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
    i2c_delay();
}

static void i2c_stop(void)
{
    sda_out();
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
    i2c_delay();
}

static int i2c_write_byte(uint8_t b)
{
    sda_out();
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) sda_high(); else sda_low();
        i2c_delay();
        scl_high();
        i2c_delay();
        scl_low();
        i2c_delay();
        b <<= 1;
    }

    /* ACK bit */
    sda_in(); /* release SDA */
    i2c_delay();
    scl_high();
    i2c_delay();
    int ack = (sda_read() == 0) ? 1 : 0;
    scl_low();
    i2c_delay();
    sda_out();
    sda_high();
    return ack;
}

static uint8_t i2c_read_byte(int ack)
{
    uint8_t b = 0;
    sda_in();
    for (int i = 0; i < 8; i++) {
        b <<= 1;
        scl_high();
        i2c_delay();
        if (sda_read()) b |= 1;
        scl_low();
        i2c_delay();
    }

    /* Send ACK/NACK */
    sda_out();
    if (ack) sda_low(); else sda_high();
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    i2c_delay();
    sda_high();
    return b;
}

int NST112_ReadTempQ4(int16_t *out_q4)
{
    if (!out_q4) return 2;

    /* NST112 pointer register for temperature is 0x00 */
    const uint8_t addr_w = (uint8_t)((NST112_I2C_ADDR << 1) | 0u);
    const uint8_t addr_r = (uint8_t)((NST112_I2C_ADDR << 1) | 1u);

    i2c_start();
    if (!i2c_write_byte(addr_w)) { i2c_stop(); return 3; }
    if (!i2c_write_byte(0x00u))   { i2c_stop(); return 4; }

    /* repeated start */
    i2c_start();
    if (!i2c_write_byte(addr_r)) { i2c_stop(); return 5; }

    uint8_t msb = i2c_read_byte(1); /* ACK */
    uint8_t lsb = i2c_read_byte(0); /* NACK */
    i2c_stop();

    uint16_t raw = ((uint16_t)msb << 8) | (uint16_t)lsb;

    /* Default NST112 format: 12-bit temperature left-aligned (bits 15..4), LSB = 0.0625C.
       After shifting right by 4 we have a 12-bit signed value in Q4 format. */
    int16_t t12 = (int16_t)(raw >> 4);
    if (t12 & 0x0800) {
        t12 |= (int16_t)0xF000; /* sign-extend from 12-bit */
    }

    *out_q4 = t12;
    return 0;
}
