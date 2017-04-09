/*
 * Copyright (C) 2017 Jonas Eriksson
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     onewire
 *
 * @{
 * @file
 * @brief       1-wire support
 *
 * @author      Jonas Eriksson <zqad@acc.umu.se>
 */

#ifndef ONEWIRE_H
#define ONEWIRE_H

#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

enum onewire_result {
	ONEWIRE_RESULT_OK,
	ONEWIRE_RESULT_COMM_ERROR,
	ONEWIRE_RESULT_TOO_MANY_DEVICES,
	__ONEWIRE_RESULT_MAX,
};

#ifndef onewire_result_t_strings
extern const char *onewire_result_t_strings[];
#endif

struct onewire_port {
	gpio_t gpio;
	uint8_t flags;
	uint8_t found_devices;
	uint8_t max_devices;
	uint8_t *devices;
};

#define ONEWIRE_PORT_STATIC_ALLOC(_name, _num_devices) \
	static uint8_t __onewire_devices ## _name [_num_devices * 6]; \
	struct onewire_port _name = { \
		.max_devices = _num_devices, \
		.devices = __onewire_devices ## _name, \
	}

static inline uint8_t *onewire_nth_device(struct onewire_port *port,
		uint8_t n) {
	if (n >= port->found_devices)
		return NULL;
	return &port->devices[n * 6];
}

#ifdef __cplusplus
}
#endif

#endif /* ONEWIRE_H */
/** @} */
