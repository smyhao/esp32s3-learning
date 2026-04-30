#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"

#define LED_GPIO_PIN GPIO_NUM_5


#define LED_ON()  gpio_set_level(LED_GPIO_PIN, 1)
#define LED_OFF() gpio_set_level(LED_GPIO_PIN, 0)
#define LED_TOGGLE() gpio_set_level(LED_GPIO_PIN, !gpio_get_level(LED_GPIO_PIN))

void led_init();

#endif // __LED_H_