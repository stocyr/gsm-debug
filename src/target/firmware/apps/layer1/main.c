/* main program of Free Software for Calypso Phone */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <delay.h>
#include <rffe.h>
#include <keypad.h>
#include <board.h>

#include <abb/twl3025.h>
#include <display.h>
#include <rf/trf6151.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <calypso/clock.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/irq.h>
#include <calypso/misc.h>

#include <layer1/sync.h>
#include <layer1/tpu_window.h>

const char *hr = "======================================================================\n";

/* SIM Stuff, TODO: clean it up */

#include <calypso/sim.h>

#include <l1ctl_proto.h>

#define SIM_CLASS		0xA0	/* Class that contains the following instructions */
#define SIM_GET_RESPONSE	0xC0	/* Get the response of a command from the card */
#define SIM_READ_BINARY		0xB0	/* Read file in binary mode */

#define L3_MSG_HEAD 4

static uint8_t sim_data[256]; /* buffer for SIM command */
static volatile uint16_t sim_len = 0; /* lenght of data in sim_data[] */

void sim_apdu(uint16_t len, uint8_t *data)
{
	memcpy(sim_data, data, len);
	sim_len = len;
}

/* allocate a large enough buffer for the SIM response */

struct msgb *my_l1ctl_msgb_alloc(uint8_t msg_type)
{
	struct msgb *msg;
	struct l1ctl_hdr *l1h;

	msg = msgb_alloc_headroom(256, L3_MSG_HEAD, "l1ctl1");
	if (!msg) {
		while (1) {
			puts("OOPS. Out of buffers...\n");
		}

		return NULL;
	}
	l1h = (struct l1ctl_hdr *) msgb_put(msg, sizeof(*l1h));
	l1h->msg_type = msg_type;
	l1h->flags = 0;

	msg->l1h = (uint8_t *)l1h;

	return msg;
}

static void sim_handler(void)
{
	uint8_t status_word[2];
	struct msgb *msg;
	uint8_t *dat;
	uint16_t length;

	if(sim_len) /* a new SIM command has arrived */
	{
		status_word[0] = 0;
		status_word[1] = 0;

		msg = my_l1ctl_msgb_alloc(L1CTL_SIM_CONF);

		/* check if instructions expects a response (TODO: add more instructions */
		if (/* GET RESPONSE needs SIM_APDU_GET */
		    (sim_len == 5 && sim_data[0] == SIM_CLASS &&
		     sim_data[1] == SIM_GET_RESPONSE && sim_data[2] == 0x00 &&
		     sim_data[3] == 0x00) ||
		    /* READ BINARY needs SIM_APDU_GET */
		     (sim_len >= 5 && sim_data[0] == SIM_CLASS &&
		      sim_data[1] == SIM_READ_BINARY))
		{
			/* allocate space for expected response */
			length = sim_data[4];
			dat = msgb_put(msg, length + 2);

			if(calypso_sim_transceive(sim_data[0], sim_data[1], sim_data[2], sim_data[3], sim_data[4], dat, status_word, SIM_APDU_GET) != 0)
				puts("SIM ERROR !\n");
			printf("Status 1: %02X %02X\n", status_word[0], status_word[1]);

			/* copy status at the end */
			memcpy(dat + length, status_word, 2);

			l1_queue_for_l2(msg);
		}
		else
		{
			if(calypso_sim_transceive(sim_data[0], sim_data[1], sim_data[2], sim_data[3], sim_data[4], &sim_data[5], status_word, SIM_APDU_PUT) != 0)
				puts("SIM ERROR !\n");
			printf("Status 2: %02X %02X\n", status_word[0], status_word[1]);

			/* 2 bytes status */
			length = 2;
			dat = msgb_put(msg, length);
			memcpy(dat, status_word, length);

			l1_queue_for_l2(msg);
		}

		sim_len = 0;
	}
}

/* MAIN program **************************************************************/

static void key_handler(enum key_codes code, enum key_states state);

/* called while waiting for SIM */

void sim_wait_handler(void)
{
	l1a_compl_execute();
	update_timers();
}

int main(void)
{
	uint8_t atr[20];
	uint8_t atrLength = 0;

	board_init();

	puts("\n\nOSMOCOM Layer 1 (revision " GIT_REVISION ")\n");
	puts(hr);

	/* Dump device identification */
	dump_dev_id();
	puts(hr);

	keypad_set_handler(&key_handler);

	/* Dump clock config after PLL set */
	calypso_clk_dump();
	puts(hr);

	display_puts("layer1.bin");

	/* initialize SIM */
        calypso_sim_init(sim_wait_handler);

        puts("Power up simcard:\n");
        memset(atr,0,sizeof(atr));
        atrLength = calypso_sim_powerup(atr);


	layer1_init();

	display_unset_attr(DISP_ATTR_INVERT);

	tpu_frame_irq_en(1, 1);

	while (1) {
		l1a_compl_execute();
		update_timers();
		sim_handler();
	}

	/* NOT REACHED */

	twl3025_power_off();
}

static int8_t vga_gain = 40;
static int high_gain = 0;
static int afcout = 0;

static void update_vga_gain(void)
{
	printf("VGA Gain: %u %s\n", vga_gain, high_gain ? "HIGH" : "LOW");
	trf6151_set_gain(vga_gain, high_gain);
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

static void tspact_toggle(uint8_t num)
{
	printf("TSPACT%u toggle\n", num);
	tsp_act_toggle((1 << num));
	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

static void key_handler(enum key_codes code, enum key_states state)
{
	if (state != PRESSED)
		return;

	switch (code) {
	case KEY_1:	/* VGA gain decrement */
		vga_gain -= 2;
		if (vga_gain < 14)
			vga_gain = 14;
		update_vga_gain();
		break;
	case KEY_2: 	/* High/Low Rx gain */
		high_gain ^= 1;
		update_vga_gain();
		break;
	case KEY_3:	/* VGA gain increment */
		vga_gain += 2;
		if (vga_gain > 40)
			vga_gain = 40;
		update_vga_gain();
		break;
	case KEY_4:
		tspact_toggle(6);	/* TRENA (RFFE) */
		break;
	case KEY_5:
		tspact_toggle(8);	/* GSM_TXEN (RFFE) */
		break;
	case KEY_6:
		tspact_toggle(1);	/* PAENA (RFFE) */
		break;
	case KEY_7:			/* decrement AFC OUT */
		afcout -= 100;
		if (afcout < -4096)
			afcout = -4096;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	case KEY_9:			/* increase AFC OUT */
		afcout += 100;
		if (afcout > 4095)
			afcout = 4095;
		twl3025_afc_set(afcout);
		printf("AFC OUT: %u\n", twl3025_afcout_get());
		break;
	default:
		break;
	}
	/* power down SIM, TODO:  this will happen with every key pressed,
       put it somewhere else ! */
	calypso_sim_powerdown();
}


