/*
 * Copyright 2017 Jonas Eriksson
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#define ENABLE_DEBUG  (1)
#include "debug.h"

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
#include <string.h>

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

static const char *onewire_result_strings[] = {
	"No error",
	"Communication Error",
	"No devices",
	"Too many devices",
	"No such error",
};

#include "onewire.h"

static inline void set_gpio_direction(struct onewire_port *port,
		gpio_mode_t direction)
{
	gpio_init(port->gpio, direction);
}

static inline void delay_us(uint32_t num)
{
	/* Spin instead of sleeping, as sleeping would mean that we get hit by
	 * the scheduling delay which may be upwards to 100us */
	xtimer_ticks32_t t = xtimer_ticks_from_usec(num);
	if (t.ticks32 == 0)
		t.ticks32 = 1;
	xtimer_spin(t);
}

static inline void delay_quarter_timeslot(uint8_t num_quarters)
{
	/* One timeslot == 60us */
	delay_us(15 * num_quarters);
}

static inline void delay_timeslots(uint8_t num_timeslots)
{
	delay_quarter_timeslot(num_timeslots * 4);
}

#include <stdio.h>
static inline enum onewire_result reset_pulse(struct onewire_port *port)
{
	uint8_t limit = 0;

	/* Send the reset signal */
	set_gpio_direction(port, ONEWIRE_GPIO_OUT);
	gpio_clear(port->gpio);
	/* Keep the port low for "at least 8 time slots", we will do 10 */
	delay_timeslots(10);

	/* Time to read from the port */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	/* Allow the line some time to float up */
	while (!gpio_read(port->gpio)) {
		delay_us(1);
		/* If the line after 200us still is not up, something is wrong
		 * with the line */
		if (limit++ > 200)
			return ONEWIRE_RESULT_COMM_ERROR;
	}

	/* Spin until line is low */
	port->tpdh_quarters = 0;
	port->tpdl_quarters = 0;
	do {
		delay_quarter_timeslot(1);
		port->tpdh_quarters++;
		/* Should not exceed 1 timeslots, but allow +1/2 */
		if (port->tpdh_quarters > 90)
			return ONEWIRE_RESULT_NO_DEVICES;
	} while (gpio_read(port->gpio));

	/* Spin until line is high */
	do {
		delay_quarter_timeslot(1);
		port->tpdl_quarters++;
		/* Should not exceed 1 timeslots, but allow +1/2 */
		if (port->tpdl_quarters > 90)
			return ONEWIRE_RESULT_COMM_ERROR;
	} while (!gpio_read(port->gpio));

	/* Make sure that we waited at least 8 time slots after the reset
	 * pulse ended so that all devices will be ready to recieve commands
	 */
	delay_quarter_timeslot(8 * 4 - port->tpdh_quarters -
			port->tpdl_quarters);

	/* Check that the tdpl period was at least one time period, allow for
	 * -1/4 for any sampling issue */
	if (port->tpdl_quarters < 3)
		return ONEWIRE_RESULT_COMM_ERROR;

	return ONEWIRE_RESULT_OK;
}

const char *onewire_error_to_string(enum onewire_result result)
{
	if (result >= __ONEWIRE_RESULT_MAX)
		return onewire_result_strings[__ONEWIRE_RESULT_MAX];
	return onewire_result_strings[result];
}

static inline void write_bit(struct onewire_port *port, uint8_t value)
{
	set_gpio_direction(port, ONEWIRE_GPIO_OUT);

	/* Send a write pulse */
	gpio_clear(port->gpio);

	/* Sleep for t_LOW1, do one half sample to make sure that the line
	 * goes down fully */
	delay_us(7);

	/* Pull the line up if data is 1 */
	if (value)
		gpio_set(port->gpio);

	/* Sleep for 1+1/4 of a slot */
	delay_quarter_timeslot(5);

	/* Sleep for t_REC, do 20us to make sure that the line floats up */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	delay_us(20);
}

static void write_octet(struct onewire_port *port, uint8_t value)
{
	for (uint8_t i = 0; i < 8; i++) {
		write_bit(port, value & 1);
		value >>= 1;
	}
}

static inline enum onewire_result read_or_bit_lsb(struct onewire_port *port,
		uint8_t *value)
{
	/* Send a read pulse */
	set_gpio_direction(port, ONEWIRE_GPIO_OUT);
	gpio_clear(port->gpio);

	/* Sleep for t_LOWR, do 7us to make sure that the line goes up fully */
	delay_us(5);

	/* Release the line, wait for the write */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	delay_us(5);

	/* Read the bit and or it to the value at the LSB position */
	*value |= gpio_read(port->gpio) ? 1 : 0;

	/* Wait out the timeslot */
	delay_timeslots(1);

	/* Check that the line has gone high */
	if (!gpio_read(port->gpio))
		return ONEWIRE_RESULT_COMM_ERROR;

	/* Sleep for t_REC */
	delay_us(10);

	return ONEWIRE_RESULT_OK;
}

enum onewire_result onewire_read_octet(struct onewire_port *port, uint8_t *value)
{
	enum onewire_result result;
	*value = 0;
	for (uint8_t i = 0; i < 8; i++) {
		result = read_or_bit_lsb(port, value);
		if (result)
			break;
		*value <<= 1;
	}
	return result;
}

enum onewire_result onewire_send_command(struct onewire_port *port, uint8_t cmd)
{
	enum onewire_result result = reset_pulse(port);
	if (result)
		return result;
	write_octet(port, cmd);
	return ONEWIRE_RESULT_OK;
}

enum onewire_result onewire_search(struct onewire_port *port)
{
	enum onewire_result result;
	uint8_t value;
	uint8_t inv_value;
	uint8_t current_split_position;
	uint8_t last_split_position = -1;
	uint8_t next_bit;
	uint8_t device_position = 0;

	/* Think of the search space as a binary tree, where each address is
	 * layed out with the first transferred bit at the level below the
	 * root, and the second bit following. The addresses of the nodes on
	 * the bus may then be described as paths starting at the root and
	 * ending up in a leaf.
	 */

	do {
		/* Initialize current_split_position, so it may be used to
		 * detect whether or not we found anything */
		current_split_position = -1;

		/* Clear the current device entry, as we will bitwise-or data
		 * into it later */
		for (uint8_t i = 0; i < 8; i++) {
			port->devices[device_position].a[i] = 0;
		}

		/* Send reset and search command */
		result = onewire_send_command(port, ONEWIRE_NL_CMD_SEARCH_ROM);
		if (result)
			return result;

		for (uint8_t position = 0; position < 64; position++) {
			/* Read address bit */
			value = 0;
			inv_value = 0;
			read_or_bit_lsb(port, &value);
			read_or_bit_lsb(port, &inv_value);

			if (value & inv_value) {
				/* No response, either no devices are present
				 * on the bus (position == 0), or a device
				 * stopped responding during the search */
				if (position == 0)
					return ONEWIRE_RESULT_NO_DEVICES;
				else
					return ONEWIRE_RESULT_COMM_ERROR;
			}
			else if (!(value | inv_value)) {
				/* Multiple responses, select the 0, unless
				 * it's already visited. */
				if (position < last_split_position) {
					next_bit = 0;
					/* Record the last position where we
					 * chose 0 */
					current_split_position = position;
				}
				else {
					next_bit = 1;
				}
			}
			else {
				/* Unique, use the value */
				next_bit = value;
			}
			port->devices[device_position].a[position / 8] |=
				next_bit << (position % 8);
			write_bit(port, next_bit);
		}

		/* Save the split position */
		last_split_position = current_split_position;

		/* Move on to the next device, check size */
		device_position++;
		if (device_position == port->max_devices)
			return ONEWIRE_RESULT_TOO_MANY_DEVICES;

	} while (current_split_position >= 0);

	/* Save the number of devices found */
	port->num_devices = device_position;

	return ONEWIRE_RESULT_OK;
}

void onewire_format_address(onewire_address_t *addr, char *buf) {
	sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			addr->a[0], addr->a[1], addr->a[2], addr->a[3], 
			addr->a[4], addr->a[5], addr->a[6], addr->a[7]);
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
