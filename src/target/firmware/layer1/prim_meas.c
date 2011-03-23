/* Layer 1 Power Measurement */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <string.h>
#include <stdlib.h>

#include <defines.h>
#include <debug.h>
#include <memory.h>
#include <byteorder.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>
#include <asm/system.h>

#include <layer1/sync.h>
#include <layer1/agc.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/prim.h>

#include <l1ctl_proto.h>

/* scheduler callback to issue a power measurement task to the DSP */
static int l1s_meas_cmd(uint8_t num_meas,
		      __unused uint8_t p2, uint16_t p3)
{
	if (l1s.meas.n == 0)
		return 0;

	dsp_api.db_w->d_task_md = num_meas; /* number of measurements */
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */

	/* Tell the RF frontend to set the gain appropriately (keep last) */
	rffe_set_gain(-85, CAL_DSP_TGT_BB_LVL);

	/* Program TPU */
	/* FIXME: RXWIN_PW needs to set up multiple times in case
	 * num_meas > 1 */
	l1s_rx_win_ctrl(l1s.meas.band_arfcn[l1s.meas.pos], L1_RXWIN_PW, 0);

	l1s.meas.running = 1;

	return 0;
}

/* scheduler callback to read power measurement resposnse from the DSP */
static int l1s_meas_resp(uint8_t num_meas, __unused uint8_t p2,
		       uint16_t arfcn)
{
	uint16_t level;

	if (l1s.meas.n == 0 || !l1s.meas.running)
		return 0;

	level = (uint16_t) ((dsp_api.db_r->a_pm[0] & 0xffff) >> 3);
	l1s.meas.level[l1s.meas.pos] = dbm2rxlev(agc_inp_dbm8_by_pm(level)/8);

	printf("measurement result of %d on pos %u at ARFCN %u\n", l1s.meas.level[l1s.meas.pos], l1s.meas.pos, l1s.meas.band_arfcn[l1s.meas.pos]);
	if (++l1s.meas.pos >= l1s.meas.n) {
		struct msgb *msg;
		struct l1ctl_meas_ind *mi;
		int i;

		l1s.meas.pos = 0;
		/* return result */
		msg = l1ctl_msgb_alloc(L1CTL_MEAS_IND);
		for (i = 0; i < l1s.meas.n; i++) {
			if (msgb_tailroom(msg) < (int) sizeof(*mi)) {
				l1_queue_for_l2(msg);
				msg = l1ctl_msgb_alloc(L1CTL_MEAS_IND);
			}
			mi = (struct l1ctl_meas_ind *)
				msgb_put(msg, sizeof(*mi));
			mi->band_arfcn = htons(l1s.meas.band_arfcn[i]);
			mi->pm[0] = l1s.meas.level[i];
			mi->pm[1] = 0;
		}
		l1_queue_for_l2(msg);
	}

	/* set previous RF gain */
	rffe_set_gain(l1s.rx_gain, CAL_DSP_TGT_BB_LVL);

	l1s.meas.running = 0;

	return 0;
}

const struct tdma_sched_item meas_sched_set[] = {
	SCHED_ITEM_DT(l1s_meas_cmd, 0, 1, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_meas_resp, -4, 1, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

