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

#define ONEWIRE_NL_CMD_READ_ROM		0x33
#define ONEWIRE_NL_CMD_SKIP_ROM		0xCC
#define ONEWIRE_NL_CMD_MATCH_ROM	0x55
#define ONEWIRE_NL_CMD_SEARCH_ROM	0x0F
#define ONEWIRE_TL_CMD_READ_MEMORY	0xF0
#define ONEWIRE_TL_CMD_EXT_READ_MEMORY	0xA5
#define ONEWIRE_TL_CMD_READ_SUBKEY	0x66
#define ONEWIRE_TL_CMD_WRITE_SCRATCHPAD	0x0F
#define ONEWIRE_TL_CMD_READ_SCRATCHPAD	0xAA
#define ONEWIRE_TL_CMD_COPY_SCRATCHPAD	0x55
#define ONEWIRE_TL_CMD_WRITE_SUBKEY	0x99
#define ONEWIRE_TL_CMD_WRITE_PASSWORD	0x5A
#define ONEWIRE_TL_CMD_WRITE_MEMORY	0x0F
#define ONEWIRE_TL_CMD_WRITE_STATUS	0x55
#define ONEWIRE_TL_CMD_READ_STATUS	0xAA

#ifdef __cplusplus
extern "C" {
#endif

enum onewire_result {
	ONEWIRE_RESULT_OK,
	ONEWIRE_RESULT_COMM_ERROR,
	ONEWIRE_RESULT_NO_DEVICES,
	ONEWIRE_RESULT_TOO_MANY_DEVICES,
	__ONEWIRE_RESULT_MAX,
};

typedef struct onewire_address {
	uint8_t a[8];
} onewire_address_t;

struct onewire_port {
	gpio_t gpio;
	uint16_t tpdh_quarters;
        uint16_t tpdl_quarters;
	uint8_t max_devices;
	uint8_t num_devices;
	onewire_address_t *devices;
};

#define ONEWIRE_PORT_STATIC_ALLOC(_name, _num_devices) \
	static onewire_address_t __onewire_devices ## _name [_num_devices]; \
	struct onewire_port _name = { \
		.max_devices = _num_devices, \
		.num_devices = 0, \
		.devices = __onewire_devices ## _name, \
	}

/* Assume buf is at least 24 characters long */
void onewire_format_address(onewire_address_t *addr, char *buf);

enum onewire_result onewire_search(struct onewire_port *port);

const char *onewire_error_to_string(enum onewire_result result);

#ifdef __cplusplus
}
#endif

#endif /* ONEWIRE_H */
/** @} */
