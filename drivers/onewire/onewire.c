/*
 * Copyright 2017 Jonas Eriksson
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_onewire
 * @{
 *
 * @file
 * @brief       1-wire driver
 *
 * @author      Jonas Eriksson <zqad@acc.umu.se>
 *
 * @}
 */

#include <stdint.h>

#include "log.h"
#include "assert.h"
#include "xtimer.h"
#include "timex.h"
#include "periph/gpio.h"

#ifdef ONEWIRE_EXTERNAL_PULLUP
#define ONEWIRE_GPIO_OUT GPIO_OUT
#define ONEWIRE_GPIO_IN GPIO_IN
#else
#define ONEWIRE_GPIO_OUT GPIO_OUT
#define ONEWIRE_GPIO_IN GPIO_IN_PU
#endif

#define ONEWIRE_PORT_FLAG_HAS_TIMING_INFO 1<<0

static const char *onewire_result_strings[] = {
	"No error",
	"Communication Error",
	"Too many devices",
	"No such error",
};

#include "onewire.h"

static inline void assert_gpio_direction(struct onewire_port *port,
		gpio_mode_t direction)
{
	gpio_init(port->gpio, direction);
}

static inline enum onewire_result reset_pulse(struct onewire_port *port)
{
	if (!(port->flags & ONEWIRE_PORT_FLAG_HAS_TIMING_INFO))
		return ONEWIRE_RESULT_TOO_MANY_DEVICES;
	gpio_set(port->gpio);
	xtimer_usleep(480);
	gpio_clear(port->gpio);
	return ONEWIRE_RESULT_OK;
}

const char *onewire_error_to_string(enum onewire_result result) {
	if (result >= __ONEWIRE_RESULT_MAX)
		return onewire_result_strings[__ONEWIRE_RESULT_MAX];
	return onewire_result_strings[result];
}

enum onewire_result onewire_send_command(struct onewire_port *port, uint8_t cmd)
{
	assert_gpio_direction(port, ONEWIRE_GPIO_OUT);
	reset_pulse(port);
	xtimer_usleep(1);
	return ONEWIRE_RESULT_OK;
}

uint8_t onewire_search(struct onewire_port *port, uint8_t cmd)
{
	return ONEWIRE_RESULT_OK;
}

//static uint8_t read_octet(gpio_t pin)
//{
//    uint16_t res = 0;
//
//    for (int i = 0; i < 8; i++) {
//        uint32_t start, end;
//        res <<= 1;
//        /* measure the length between the next rising and falling flanks (the
//         * time the pin is high - smoke up :-) */
//        while (!gpio_read(pin));
//        start = xtimer_now_usec();
//        while (gpio_read(pin));
//        end = xtimer_now_usec();
//        /* if the high phase was more than 40us, we got a 1 */
//        if ((end - start) > PULSE_WIDTH_THRESHOLD) {
//            res |= 0x0001;
//        }
//    }
//    return res;
//}
