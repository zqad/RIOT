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
	"Too many devices",
	"No such error",
};

#include "onewire.h"

static inline void set_gpio_direction(struct onewire_port *port,
		gpio_mode_t direction)
{
	gpio_init(port->gpio, direction);
}

static inline void sleep_quarter_timeslot(uint8_t num_quarters)
{
	/* One timeslot == 60us */
	xtimer_usleep(15 * num_quarters);
}

/* Many parts of the protocol is centered around quarter-timeslot sleeps */
static inline void sleep_timeslots(uint8_t num_timeslots)
{
	sleep_quarter_timeslot(num_timeslots * 4);
}


static inline enum onewire_result reset_pulse(struct onewire_port *port)
{
	/* Send the reset signal */
	set_gpio_direction(port, ONEWIRE_GPIO_OUT);
	gpio_clear(port->gpio);
	/* Keep the port low for "at least 8 time slots", we will do 10 */
	sleep_timeslots(10);

	/* Time to read from the port */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	/* Allow the line some time to float up */
	sleep_quarter_timeslot(1);

	/* Spin until line is low */
	port->tpdh_quarters = 1;
	port->tpdl_quarters = 0;
	do {
		sleep_quarter_timeslot(1);
		port->tpdh_quarters++;
		/* Should not exceed 1 timeslots, but allow +1/2 */
		if (port->tpdh_quarters > 6)
			return ONEWIRE_RESULT_COMM_ERROR;
	} while (gpio_read(port->gpio));

	/* Spin until line is high */
	do {
		sleep_quarter_timeslot(1);
		port->tpdl_quarters++;
		/* Should not exceed 1 timeslots, but allow +1/2 */
		if (port->tpdl_quarters > 18)
			return ONEWIRE_RESULT_COMM_ERROR;
	} while (!gpio_read(port->gpio));

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
	/* Send a write pulse */
	set_gpio_direction(port, ONEWIRE_GPIO_OUT);
	gpio_clear(port->gpio);

	/* Sleep for t_LOW1, do 2us to make sure that the line goes up fully */
	xtimer_usleep(2);

	/* Write the 1 */
	if (value)
		gpio_set(port->gpio);

	/* Sleep for 1+1/4 of a slot */
	sleep_quarter_timeslot(5);

	/* Sleep for t_REC, do 5us to make sure that the line floats up */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	xtimer_usleep(5);
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

	/* Sleep for t_LOWR, do 2us to make sure that the line goes up fully */
	xtimer_usleep(2);

	/* Release the line, wait for the write */
	set_gpio_direction(port, ONEWIRE_GPIO_IN);
	xtimer_usleep(7);

	/* Read the bit and or it to the value at the LSB position */
	*value |= gpio_read(port->gpio) ? 1 : 0;

	/* Wait out the timeslot */
	sleep_timeslots(1);

	/* Check that the line has gone high */
	if (!gpio_read(port->gpio))
		return ONEWIRE_RESULT_COMM_ERROR;

	/* Sleep for t_REC */
	xtimer_usleep(1);

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

void onewire_send_command(struct onewire_port *port, uint8_t cmd)
{
	reset_pulse(port);
	write_octet(port, cmd);
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
		result = reset_pulse(port);
		if (result)
			return result;
		onewire_send_command(port, ONEWIRE_NL_CMD_SEARCH_ROM);

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
