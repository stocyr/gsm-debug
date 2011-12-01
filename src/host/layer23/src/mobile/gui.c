/*
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

#include <stdio.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/ui/telnet_interface.h>
#include <osmocom/bb/mobile/mnccms.h>

/*
 * status screen generation
 */

static int status_netname(struct osmocom_ms *ms, char *text)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	int len, shift;

	/* No Service */
	if (ms->shutdown == 2 || !ms->started) {
		strncpy(text, "SHUTDOWN", UI_COLS);
	} else
	/* No Service */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && mm->substate == GSM48_MM_SST_NO_CELL_AVAIL) {
		strncpy(text, "No Service", UI_COLS);
	} else
	/* Searching */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && (mm->substate == GSM48_MM_SST_PLMN_SEARCH_NORMAL
	  || mm->substate == GSM48_MM_SST_PLMN_SEARCH)) {
		strncpy(text, "Searching...", UI_COLS);
	} else
	/* no network selected */
	if (!cs->selected) {
		strncpy(text, "", UI_COLS);
	} else
	/* network name set for currently selected network */
	if (cs->selected && (mm->name_short[0] || mm->name_long[0])
	 && cs->sel_mcc == mm->name_mcc && cs->sel_mnc == mm->name_mnc) {
		const char *name;

	 	/* only short name */ 
	 	if (mm->name_short[0] && !mm->name_long[0])
			name = mm->name_short;
	 	/* only long name */ 
	 	else if (!mm->name_short[0] && mm->name_long[0])
			name = mm->name_long;
	 	/* both names, long name fits */ 
	 	else if (strlen(mm->name_long) <= UI_COLS)
			name = mm->name_long;
	 	/* both names, use short name, even if it does not fit */ 
		else
			name = mm->name_short;

		strncpy(text, name, UI_COLS);
	} else
	/* no network name for currently selected network */
	{
		const char *mcc_name, *mnc_name;
		int mcc_len, mnc_len;

		mcc_name = gsm_get_mcc(cs->sel_mcc);
		mnc_name = gsm_get_mnc(cs->sel_mcc, cs->sel_mnc);
		mcc_len = strlen(mcc_name);
		mnc_len = strlen(mnc_name);

	 	/* MCC / MNC fits */ 
		if (mcc_len + 3 + mnc_len <= UI_COLS)
			sprintf(text, "%s / %s", mcc_name, mnc_name);
	 	/* MCC/MNC fits */ 
		else if (mcc_len + 1 + mnc_len <= UI_COLS)
			sprintf(text, "%s/%s", mcc_name, mnc_name);
		/* use MNC, even if it does not fit */
		else
			strncpy(text, mnc_name, UI_COLS);
	}
	text[UI_COLS] = '\0';

	/* center */
	len = strlen(text);
	if (len + 1 < UI_COLS) {
		shift = (UI_COLS - len) / 2;
		memcpy(text + shift, text, len + 1);
		memset(text, ' ', shift);
	}

	return 1;
}

static int status_lai(struct osmocom_ms *ms, char *text)
{
	struct gsm322_cellsel *cs = &ms->cellsel;

	sprintf(text, "%s %s %04x", gsm_print_mcc(cs->sel_mcc),
		gsm_print_mnc(cs->sel_mnc), cs->sel_lac);
	text[UI_COLS] = '\0';
	return 1;
}

static int status_imsi(struct osmocom_ms *ms, char *text)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	int len;

	if (subscr->imsi[0])
		strcpy(text, subscr->imsi);
	else
		strcpy(text, "---------------");
	if (subscr->tmsi < 0xfffffffe)
		sprintf(strchr(text, '\0'), " %08x", subscr->tmsi);
	else
		strcat(text, " --------");
	len = strlen(text);
	/* wrap */
	if (len > UI_COLS) {
		memcpy(text + UI_COLS + 1, text + UI_COLS, len - UI_COLS);
		text[UI_COLS] = '\0';
		text[2 * UI_COLS + 1] = '\0';

		return 2;
	}

	return 1;
}

static int status_imei(struct osmocom_ms *ms, char *text)
{
	struct gsm_settings *set = &ms->settings;
	int len;

	sprintf(text, "%s/%s", set->imei, set->imeisv + strlen(set->imei));
	len = strlen(text);
	/* wrap */
	if (len > UI_COLS) {
		memcpy(text + UI_COLS + 1, text + UI_COLS, len - UI_COLS);
		text[UI_COLS] = '\0';
		text[2 * UI_COLS + 1] = '\0';

		return 2;
	}

	return 1;
}

static int status_channel(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	uint16_t arfcn = 0xffff;
	int hopping = 0;

	/* arfcn */
	if (rr->dm_est) {
		if (rr->cd_now.h)
			hopping = 1;
		else
			arfcn = rr->cd_now.arfcn;
	} else if (cs->selected)
		arfcn = cs->sel_arfcn;
		
	if (hopping)
		strcpy(text, "a:HOPP");
	else if (arfcn < 0xffff) {
		sprintf(text, "a:%d", arfcn & 1023);
		if (arfcn & ARFCN_PCS)
			strcat(text, "P");
		else if (arfcn >= 512 && arfcn <= 885)
			strcat(text, "D");
		else if (arfcn < 10)
			strcat(text, "   ");
		else if (arfcn < 100)
			strcat(text, "  ");
		else if (arfcn < 1000)
			strcat(text, " ");
	} else
		strcpy(text, "a:----");
	
	/* channel */
	if (!rr->dm_est)
		strcat(text, " b:0");
	else {
		uint8_t ch_type, ch_subch, ch_ts;

		rsl_dec_chan_nr(rr->cd_now.chan_nr, &ch_type, &ch_subch,
			&ch_ts);
		switch (ch_type) {
		case RSL_CHAN_SDCCH8_ACCH:
		case RSL_CHAN_SDCCH4_ACCH:
			sprintf(strchr(text, '\0'), " s:%d/%d", ch_ts,
				ch_subch);
			break;
		case RSL_CHAN_Lm_ACCHs:
			sprintf(strchr(text, '\0'), " h:%d/%d", ch_ts,
				ch_subch);
			break;
		case RSL_CHAN_Bm_ACCHs:
			sprintf(strchr(text, '\0'), " f:%d", ch_ts);
			break;
		default:
			sprintf(strchr(text, '\0'), " ?:%d", ch_ts);
			break;
		}
	}
	text[UI_COLS] = '\0';

	return 1;
}

static int status_rx(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	/* rxlev */
	if (rr->rxlev != 255) {
		sprintf(text, "rx:%d dbm", rr->rxlev - 110);
	} else
		strcpy(text, "rx:--");
	text[UI_COLS] = '\0';

	return 1;
}

static int status_tx(struct osmocom_ms *ms, char *text)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_settings *set = &ms->settings;

	/* ta, pwr */
	if (rr->dm_est)
		sprintf(text, "ta:%d tx:%d",
			rr->cd_now.ind_ta - set->alter_delay,
			(set->alter_tx_power) ? set->alter_tx_power_value
					: rr->cd_now.ind_tx_power);
	else
		strcpy(text, "ta:-- tx:--");
	text[UI_COLS] = '\0';

	return 1;
}

static int status_nb(struct osmocom_ms *ms, char *text)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_nb_summary *nb;
	int i;

	for (i = 0; i < 6; i++) {
		nb = &cs->nb_summary[i];
		if (nb->valid) {
			sprintf(text, "%d:%d", i + 1,
				nb->arfcn & 1023);
			if (nb->arfcn & ARFCN_PCS)
				strcat(text, "P");
			else if (nb->arfcn >= 512 && nb->arfcn <= 885)
				strcat(text, "D");
			else if (nb->arfcn < 10)
				strcat(text, "   ");
			else if (nb->arfcn < 100)
				strcat(text, "  ");
			else if (nb->arfcn < 1000)
				strcat(text, " ");
			sprintf(strchr(text, '\0'), " %d", nb->rxlev_dbm);
		} else
			sprintf(text, "%d:-", i + 1);
		text[UI_COLS] = '\0';
		text += (UI_COLS + 1);
	}

	return i;
}

static int status_fkeys(struct osmocom_ms *ms, char *text)
{
	return 0;
}

/*
 * status screen structure
 */

#define SHOW_HIDE	"(show|hide)"
#define SHOW_HIDE_STR	"Show this feature on display\n" \
			"Do not show this feature on display"

struct status_screen status_screen[GUI_NUM_STATUS] = {
	/* network name */
	{
		.feature	= "network-name",
		.feature_vty	= "network-name " SHOW_HIDE,
		.feature_help	= "Show network name on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 1,
		.lines		= 1,
		.display_func	= status_netname,
	},
	/* LAI */
	{
		.feature	= "location-area-info",
		.feature_vty	= "location-area-info " SHOW_HIDE,
		.feature_help	= "Show LAI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_lai,
	},
	/* IMSI */
	{
		.feature	= "imsi",
		.feature_vty	= "imsi " SHOW_HIDE,
		.feature_help	= "Show IMSI/TMSI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 2,
		.display_func	= status_imsi,
	},
	/* IMEI */
	{
		.feature	= "imei",
		.feature_vty	= "imei " SHOW_HIDE,
		.feature_help	= "Show IMEI on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 2,
		.display_func	= status_imei,
	},
	/* channel */
	{
		.feature	= "channel",
		.feature_vty	= "channel " SHOW_HIDE,
		.feature_help	= "Show current channel on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_channel,
	},
	/* tx */
	{
		.feature	= "tx",
		.feature_vty	= "tx " SHOW_HIDE,
		.feature_help	= "Show current timing advance and rx level "
				  "on display\n" SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_tx,
	},
	/* rx */
	{
		.feature	= "rx",
		.feature_vty	= "rx " SHOW_HIDE,
		.feature_help	= "Show current rx level on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 1,
		.display_func	= status_rx,
	},
	/* nb */
	{
		.feature	= "neighbours",
		.feature_vty	= "neighbours " SHOW_HIDE,
		.feature_help	= "Show neighbour cells on display\n"
				  SHOW_HIDE_STR,
		.default_en	= 0,
		.lines		= 6,
		.display_func	= status_nb,
	},
	/* function keys */
	{
		.feature	= "function-keys",
		.feature_vty	= "function-keys " SHOW_HIDE,
		.feature_help	= "Show function keys (two keys right "
				  "below the display) on the display's bottom "
				  "line\n" SHOW_HIDE_STR,
		.default_en	= 1,
		.lines		= 0,
		.display_func	= status_fkeys,
	},
};

/*
 * UI handling for mobile instance
 */

static void update_status(void *arg);

enum {
	MENU_STATUS,
	MENU_DIALING,
	MENU_CALL,
};

static int beep_cb(struct ui_inst *ui)
{
	ui_telnet_puts(ui, "");

	return 0;
}

static int key_cb(struct ui_inst *ui, enum ui_key kp)
{
	struct gsm_ui *gui = container_of(ui, struct gsm_ui, ui);
	struct osmocom_ms *ms = container_of(gui, struct osmocom_ms, gui);
	struct gsm_call *call, *selected_call = NULL;
	int num_calls = 0, num_hold = 0;

	switch (gui->menu) {
	case MENU_STATUS:
		if ((kp >= UI_KEY_0 && kp <= UI_KEY_9) ||
		 kp == UI_KEY_STAR || kp == UI_KEY_HASH) {
			gui->dialing[0] = kp;
			gui->dialing[1] = '\0';
dial:
			/* go to dialing screen */
			gui->menu = MENU_DIALING;
			ui_inst_init(ui, &ui_dialview, key_cb, beep_cb);
			ui->ud.dialview.title = "Number:";
			ui->ud.dialview.number = gui->dialing;
			ui->ud.dialview.num_len = sizeof(gui->dialing);
			ui->ud.dialview.pos = 1;
			ui_inst_refresh(ui);

			return 1; /* handled */
		}
		if (kp == UI_KEY_PICKUP) {
			gui->dialing[0] = '\0';
			goto dial;
		}
		break;
	case MENU_DIALING:
		if (kp == UI_KEY_PICKUP) {
			mncc_call(ms, ui->ud.dialview.number);

			/* go to call screen */
			gui->menu = MENU_STATUS;
			gui_notify_call(ms);

			return 1; /* handled */
		}
		if (kp == UI_KEY_HANGUP) {
			llist_for_each_entry(call, &ms->mncc_entity.call_list,
				entry) {
				num_calls++;
			}
			if (!num_calls) {
				/* go to status screen */
				gui_start(ms);
			} else {
				/* go to call screen */
				gui->menu = MENU_STATUS;
				gui_notify_call(ms);
			}

			return 1; /* handled */
		}
		break;
	case MENU_CALL:
		if (kp == UI_KEY_UP) {
			if (gui->selected_call > 0) {
				gui->selected_call--;
				gui_notify_call(ms);
			}

			return 1; /* handled */
		}
		if (kp == UI_KEY_DOWN) {
			gui->selected_call++;
			gui_notify_call(ms);

			return 1; /* handled */
		}
		llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
			if (num_calls == gui->selected_call)
				selected_call = call;
			num_calls++;
			if (call->call_state == CALL_ST_HOLD)
				num_hold++;
		}
		if (!selected_call)
			return 1;
		if (kp == UI_KEY_HANGUP) {
			mncc_hangup(ms, gui->selected_call + 1);

			return 1; /* handled */
		}
		if (kp == UI_KEY_PICKUP) {
			if (selected_call->call_state == CALL_ST_MT_RING
			 || selected_call->call_state == CALL_ST_MT_KNOCK)
				mncc_answer(ms, gui->selected_call + 1);
			else
			if (selected_call->call_state == CALL_ST_HOLD)
				mncc_retrieve(ms, gui->selected_call + 1);

			return 1; /* handled */
		}
		if (kp == UI_KEY_F1) {
			/* only if all calls on hold */
			if (num_calls == num_hold) {
				gui->dialing[0] = '\0';
				goto dial;
			}

			return 1; /* handled */
		}
		if (kp == UI_KEY_F2) {
			if (selected_call->call_state == CALL_ST_ACTIVE)
				mncc_hold(ms, gui->selected_call + 1);
			else
			if (selected_call->call_state == CALL_ST_HOLD)
				mncc_retrieve(ms, gui->selected_call + 1);

			return 1; /* handled */
		}
		if ((kp >= UI_KEY_0 && kp <= UI_KEY_9) ||
		 kp == UI_KEY_STAR || kp == UI_KEY_HASH) {
			if (selected_call->call_state == CALL_ST_ACTIVE) {
				/* if dtmf is not supported */
				if (!ms->settings.cc_dtmf)
					return 1; /* handled */
				char dtmf[2];

				dtmf[0] = kp;
				dtmf[1] = '\0';
				mncc_dtmf(ms, gui->selected_call + 1, dtmf);
				return 1; /* handled */
			}
			/* only if all calls on hold */
			if (num_calls == num_hold) {
				gui->dialing[0] = kp;
				gui->dialing[1] = '\0';
				goto dial;
			}

			return 1; /* handled */
		}
		break;
	}

	return 0;
}

/* generate status and display it */
static void update_status(void *arg)
{
	struct osmocom_ms *ms = arg;
	struct gsm_settings *set = &ms->settings;
	struct gsm_ui *gui = &ms->gui;
	int i, j = 0, n, lines = 0, has_network_name = 0, has_bottom_line = 0;
	char *p = gui->status_text;

	/* if timer fires */
	if (gui->menu != MENU_STATUS)
		return;

	gui->ui.bottom_line = NULL;

	for (i = 0; i < GUI_NUM_STATUS; i++) {
		if (i == 0)
			has_network_name = 1;
		lines += status_screen[i].lines;
		if (!(set->status_enable & (1 << i)))
			continue;
		/* finish loop if number of lines exceed the definition */
		if (lines > GUI_NUM_STATUS_LINES)
			continue;
		/* special case where function keys' help is displayed */
		if (i == GUI_NUM_STATUS - 1) {
			has_bottom_line = 1;
			gui->ui.bottom_line = "menu setup";
			continue;
		}
		n = status_screen[i].display_func(ms, p);
		while (n--) {
			gui->status_lines[j] = p;
			p += (UI_COLS + 1);
			j++;
			if (j == GUI_NUM_STATUS_LINES)
				break;
		}
	}

	/* if network name is present */
	if (has_network_name) {
		/* if not all lines are occupied */
		if (j + has_bottom_line < UI_ROWS && j > 1) {
			/* insert space below network name */
			memcpy(gui->status_lines + 2, gui->status_lines + 1,
				(j - 1) * sizeof(char *));
			gui->status_lines[1] = "";
			j++;
		}
		/* if not all lines are occupied */
		if (j + has_bottom_line < UI_ROWS && j > 1) {
			/* insert space above network name */
			memcpy(gui->status_lines + 1, gui->status_lines,
				j * sizeof(char *));
			gui->status_lines[0] = "";
			j++;
		}
	}

	gui->ui.ud.listview.lines = j;
	gui->ui.ud.listview.text = gui->status_lines;
	ui_inst_refresh(&gui->ui);

	/* schedule next refresh */
	gui->timer.cb = update_status;
        gui->timer.data = ms;
        osmo_timer_schedule(&gui->timer, 1,0);
}

int gui_start(struct osmocom_ms *ms)
{
	/* go to status screen */
	ms->gui.menu = MENU_STATUS;
	ui_inst_init(&ms->gui.ui, &ui_listview, key_cb, beep_cb);
	update_status(ms);

	return 0;
}

int gui_stop(struct osmocom_ms *ms)
{
	struct gsm_ui *gui = &ms->gui;

        osmo_timer_del(&gui->timer);

	return 0;
}

/* call instances have changed */
int gui_notify_call(struct osmocom_ms *ms)
{
	struct gsm_ui *gui = &ms->gui;
	struct gsm_call *call;
	const char *state;
	char *p = gui->status_text, *n;
	int len, shift;
	int j = 0, calls = 0, calls_on_hold = 0;
	int last_call_j_first = 0, selected_call_j_first = 0;
	int last_call_j_last = 0, selected_call_j_last = 0;
	struct gsm_call *last_call = NULL, *selected_call = NULL;

	if (gui->menu != MENU_STATUS
	 && gui->menu != MENU_CALL)
	 	return 0;

	if (gui->menu == MENU_STATUS) {
		ms->gui.menu = MENU_CALL;
		ui_inst_init(&ms->gui.ui, &ui_listview, key_cb, beep_cb);
		gui->selected_call = 999;
		/* continue here */
	}

	llist_for_each_entry(call, &ms->mncc_entity.call_list, entry) {
		switch (call->call_state) {
		case CALL_ST_MO_INIT:
			state = "Dialing...";
			break;
		case CALL_ST_MO_PROC:
			state = "Proceeding";
			break;
		case CALL_ST_MO_ALERT:
			state = "Ringing";
			break;
		case CALL_ST_MT_RING:
			state = "Incomming";
			break;
		case CALL_ST_MT_KNOCK:
			state = "Knocking";
			break;
		case CALL_ST_ACTIVE:
			state = "Connected";
			break;
		case CALL_ST_HOLD:
			state = "On Hold";
			calls_on_hold++;
			break;
		case CALL_ST_DISC_TX:
			state = "Releasing";
			break;
		case CALL_ST_DISC_RX:
			state = "Hung Up";
			break;
		default:
			continue;
		}

		/* store first line of selected call */
		if (calls == gui->selected_call) {
			selected_call_j_first = j;
			selected_call = call;
		}
		/* set first line of last call */
		last_call_j_first = j;
		last_call = call;

		/* state */
		strncpy(p, state, UI_COLS);
		p[UI_COLS] = '\0';
		/* center */
		len = strlen(p);
		if (len + 1 < UI_COLS) {
			shift = (UI_COLS - len) / 2;
			memcpy(p + shift, p, len + 1);
			memset(p, ' ', shift);
		}
		gui->status_lines[j] = p;
		p += (UI_COLS + 1);
		j++;
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* number */
		n = call->number;
		while (1) {
			strncpy(p, n, UI_COLS);
			p[UI_COLS] = '\0';
			gui->status_lines[j] = p;
			p += (UI_COLS + 1);
			j++;
			if (j == GUI_NUM_STATUS_LINES)
				break;
			if (strlen(n) <= UI_COLS)
				break;
			n += UI_COLS;
		}
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* store last line of selected call */
		if (calls == gui->selected_call)
			selected_call_j_last = j;
		/* set last line of last call */
		last_call_j_last = j;

		/* empty line */
		p[0] = '\0';
		gui->status_lines[j] = p;
		p += (UI_COLS + 1);
		j++;
		if (j == GUI_NUM_STATUS_LINES)
			break;

		/* count calls */
		calls++;
	}

	/* return to status menu */
	if (!calls)
		return gui_start(ms);

	/* remove last empty line */
	if (j)
		j--;

	/* in case there are less calls than the selected one */
	if (!calls) {
		gui->selected_call = 0;
	} else if (gui->selected_call >= calls) {
		gui->selected_call = calls - 1;
		selected_call_j_first = last_call_j_first;
		selected_call_j_last = last_call_j_last;
		selected_call = last_call;
	}

	/* adjust vpos, so the selected call fits on display */
	if (selected_call_j_last - gui->ui.ud.listview.vpos > UI_ROWS - 2)
		gui->ui.ud.listview.vpos = selected_call_j_last - (UI_ROWS - 1);
	if (gui->ui.ud.listview.vpos > selected_call_j_first)
		gui->ui.ud.listview.vpos = selected_call_j_first;
	

	/* mark selected call */
	if (calls > 1)
		gui->status_text[selected_call_j_first * (UI_COLS + 1)] = '*';
	
	/* if only one call */
	if (calls == 1) {
		/* insert space above call state */
		memcpy(gui->status_lines + 1, gui->status_lines,
			j * sizeof(char *));
		gui->status_lines[0] = "";
		j++;
		if (j > 2) {
			/* insert space below call state */
			memcpy(gui->status_lines + 3, gui->status_lines + 2,
				(j - 2) * sizeof(char *));
			gui->status_lines[2] = "";
			j++;
		}
	}
	/* set bottom line */
	gui->ui.bottom_line = " ";
	if (calls && selected_call) {
		switch (selected_call->call_state) {
		case CALL_ST_ACTIVE:
			gui->ui.bottom_line = " hold";
			break;
		case CALL_ST_HOLD:
			/* offer new call, if all calls are on hold */
			if (calls_on_hold == calls)
				gui->ui.bottom_line = "new resume";
			else
				gui->ui.bottom_line = " resume";
			break;
		}
	}

	gui->ui.ud.listview.lines = j;
	gui->ui.ud.listview.text = gui->status_lines;
	ui_inst_refresh(&gui->ui);

	return 0;
}

