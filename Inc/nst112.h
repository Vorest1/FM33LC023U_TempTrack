#ifndef __NST112_H
#define __NST112_H

#include <stdint.h>

/* NST112 I2C pins */
#define NST112_SCL_PORT   GPIOA
#define NST112_SCL_PIN    LL_GPIO_PIN_11
#define NST112_SDA_PORT   GPIOA
#define NST112_SDA_PIN    LL_GPIO_PIN_12

/*
 * Default 7-bit I2C address for NST112 depends on ADD0 strap.
 * Common default is 0x48. If your board uses another address, change it here.
 */
#define NST112_I2C_ADDR   0x48u

void NST112_GPIO_Init(void);

/*
 * Read temperature.
 * out_q4: temperature in Q4 format (degC * 16). Example: 25.0625C => 25*16 + 1 = 401.
 * Returns 0 on success, non-zero on error.
 */
int NST112_ReadTempQ4(int16_t *out_q4);

#endif
