/* (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <string.h>
#include <errno.h>

#include <osmocom/core/select.h>
#include <osmocom/bb/ui/ui.h>
#include <osmocom/bb/ui/telnet_interface.h>

/*
 * io functions
 */

int ui_clearhome(struct ui_inst *ui)
{
	int i;

	/* initialize with spaces */
	memset(ui->buffer, ' ', sizeof(ui->buffer));
	/* terminate with EOL */
	for (i = 0; i < UI_ROWS; i++)
		ui->buffer[(UI_COLS + 1) * (i + 1) - 1] = '\0';

	ui->cursor_x = ui->cursor_y = 0;
	ui->cursor_on = 0;

	return 0;
}

int ui_puts(struct ui_inst *ui, int ln, const char *text)
{
	int len = strlen(text);

	/* out of range */
	if (ln < 0 || ln >= UI_ROWS)
		return -EINVAL;

	/* clip */
	if (len > UI_COLS)
		len = UI_COLS;

	/* copy */
	if (len)
		memcpy(ui->buffer + (UI_COLS + 1) * ln, text, len);

	return 0;
}

int ui_flush(struct ui_inst *ui)
{
	int i;
	char frame[UI_COLS + 5];
	char line[UI_COLS + 5];
	char cursor[16];

	/* clear */
	ui_telnet_puts(ui, "\033c");

	/* display */
	memset(frame + 1, '-', UI_COLS);
	frame[0] = frame[UI_COLS + 1] = '+';
	frame[UI_COLS + 2] = '\r';
	frame[UI_COLS + 3] = '\n';
	frame[UI_COLS + 4] = '\0';
	ui_telnet_puts(ui, frame);
	for (i = 0; i < UI_ROWS; i++) {
		sprintf(line, "|%s|\r\n", ui->buffer + (UI_COLS + 1) * i);
		ui_telnet_puts(ui, line);
	}
	ui_telnet_puts(ui, frame);

	ui_telnet_puts(ui, "\r\nPos1 = pickup, End = hangup, F1 = left button, "
		"F2 = right button\r\narrow keys = navigation buttons\r\n"); 

	/* set cursor */
	if (ui->cursor_on) {
		sprintf(cursor, "\033[%d;%dH", ui->cursor_y + 2,
			ui->cursor_x + 2);
		ui_telnet_puts(ui, cursor);
	}

	return 0;
}

static int bottom_puts(struct ui_inst *ui, const char *text)
{
	char bottom_line[UI_COLS + 1], *p;
	int space;

	strncpy(bottom_line, text, UI_COLS);
	bottom_line[UI_COLS] = '\0';
	if ((p = strchr(bottom_line, ' '))
	 && (space = UI_COLS - strlen(bottom_line))) {
	 	p++;
	 	memcpy(p + space, p, strlen(p));
		memset(p, ' ', space);
	}

	return ui_puts(ui, UI_ROWS - 1, bottom_line);
}

/*
 * listview
 */

static int init_listview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->listview.vpos = 0;
	ud->listview.lines = 0;

	return 0;
}

static int keypad_listview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	int rows = UI_ROWS;

	if (ui->bottom_line)
		rows--;

	switch (kp) {
	case UI_KEY_UP:
		if (ud->listview.vpos == 0)
			return -1;
		ud->listview.vpos--;
		break;
	case UI_KEY_DOWN:
		if (rows + ud->listview.vpos >= ud->listview.lines)
			return -1;
		ud->listview.vpos++;
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_listview(struct ui_inst *ui, union ui_view_data *ud)
{
	const char **text = ud->listview.text;
	int lines = ud->listview.lines;
	int i;
	int rows = UI_ROWS;
	
	if (ui->bottom_line)
		rows--;

	/* vpos will skip lines */
	for (i = 0; i < ud->listview.vpos; i++) {
		/* if we reached end of test, we leave the pointer there */
		if (*text == NULL)
			break;
		text++;
		lines--;
	}

	ui_clearhome(ui);
	for (i = 0; i < rows; i++) {
		if (*text && i < lines) {
			ui_puts(ui, i, *text);
			text++;
		} else
			break;
//			ui_puts(ui, i, "~");
	}
	if (ui->bottom_line)
		bottom_puts(ui, ui->bottom_line);
	ui_flush(ui);

	return 0;
}

/*
 * selectview
 */

static int init_selectview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->selectview.vpos = 0;
	ud->selectview.cursor = 0;
	ud->selectview.lines = 0;

	return 0;
}

static int keypad_selectview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	switch (kp) {
	case UI_KEY_UP:
		if (ud->selectview.cursor == 0)
			return -1;
		ud->selectview.cursor--;
		/* follow cursor */
		if (ud->selectview.cursor < ud->selectview.vpos)
			ud->selectview.vpos = ud->selectview.cursor;
		break;
	case UI_KEY_DOWN:
		if (UI_ROWS + ud->selectview.cursor >= ud->selectview.lines)
			return -1;
		ud->selectview.cursor++;
		/* follow cursor */
		if (ud->selectview.cursor > ud->selectview.vpos + UI_ROWS - 1)
			ud->selectview.vpos = ud->selectview.cursor -
								(UI_ROWS - 1);
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_selectview(struct ui_inst *ui, union ui_view_data *ud)
{
	const char **text = ud->selectview.text;
	int i;

	/* vpos will skip lines */
	for (i = 0; i < ud->selectview.vpos; i++) {
		/* if we reached end of test, we leave the pointer there */
		if (*text == NULL)
			break;
	}

	ui_clearhome(ui);
	for (i = 0; i < UI_ROWS; i++) {
		if (*text) {
			ui_puts(ui, i, *text);
			text++;
		} else
			break;
//			ui_puts(ui, i, "~");
	}
	ui_flush(ui);

	return 0;
}

/*
 * dialview
 */

static int init_dialview(struct ui_view *uv, union ui_view_data *ud)
{
	ud->dialview.pos = 0;

	return 0;
}

static int keypad_dialview(struct ui_inst *ui, struct ui_view *uv,
	union ui_view_data *ud, enum ui_key kp)
{
	switch (kp) {
	case UI_KEY_STAR:
	case UI_KEY_HASH:
	case UI_KEY_1:
	case UI_KEY_2:
	case UI_KEY_3:
	case UI_KEY_4:
	case UI_KEY_5:
	case UI_KEY_6:
	case UI_KEY_7:
	case UI_KEY_8:
	case UI_KEY_9:
	case UI_KEY_0:
		/* check if number is full */
		if (strlen(ud->dialview.number) + 1 == ud->dialview.num_len)
			return -1;
		/* add digit */
		if (ud->dialview.number[ud->dialview.pos] == '\0') {
			/* add to the end */
			ud->dialview.number[ud->dialview.pos] = kp;
			ud->dialview.pos++;
			ud->dialview.number[ud->dialview.pos] = '\0';
		} else {
			/* insert digit */
			memcpy(ud->dialview.number + ud->dialview.pos + 1,
				ud->dialview.number + ud->dialview.pos,
				strlen(ud->dialview.number + ud->dialview.pos)
					+ 1);
			ud->dialview.number[ud->dialview.pos] = kp;
			ud->dialview.pos++;
		}
		break;
	case UI_KEY_LEFT:
		if (ud->dialview.pos == 0)
			return -1;
		ud->dialview.pos--;
		break;
	case UI_KEY_RIGHT:
		if (ud->dialview.pos == strlen(ud->dialview.number))
			return -1;
		ud->dialview.pos++;
		break;
	case UI_KEY_F1: /* clear */
		ud->dialview.pos = 0;
		ud->dialview.number[0] = '\0';
		break;
	case UI_KEY_F2: /* delete */
		if (ud->dialview.pos == 0)
			return -1;
		/* del digit */
		if (ud->dialview.number[ud->dialview.pos] == '\0') {
			/* del digit from the end */
			ud->dialview.pos--;
			ud->dialview.number[ud->dialview.pos] = '\0';
		} else {
			/* remove digit */
			memcpy(ud->dialview.number + ud->dialview.pos - 1,
				ud->dialview.number + ud->dialview.pos,
				strlen(ud->dialview.number + ud->dialview.pos)
					+ 1);
			ud->dialview.pos--;
		}
		break;
	default:
		return 0;
	}
	/* refresh display */
	uv->display(ui, ud);

	return 0;
}

static int display_dialview(struct ui_inst *ui, union ui_view_data *ud)
{
	char line[UI_COLS + 1];
	char *p = ud->dialview.number;
	int len = strlen(p);
	int i = 1;

	/* if number shrunk */
	if (ud->dialview.pos > len)
		ud->dialview.pos = len;

	ui_clearhome(ui);
	/* title */
	if (ud->dialview.title) {
		char title[UI_COLS + 1];
		int len, shift;

		strncpy(title, ud->dialview.title, UI_COLS);
		title[UI_COLS] = '\0';
		len = strlen(title);
		if (len + 1 < UI_COLS) {
			shift = (UI_COLS - len) / 2;
			memcpy(title + shift, title, len + 1);
			memset(title, ' ', shift);
		}
		ui_puts(ui, i++, title);
		i++;
	}
	/* if line exceeds display width */
	while (len > UI_COLS) {
		memcpy(line, p, UI_COLS);
		line[UI_COLS] = '\0';
		ui_puts(ui, i++, line);
		p += UI_COLS;
		len -= UI_COLS;
	}
	/* last line */
	if (len)
		ui_puts(ui, i, p);
	/* cursor */
	ui->cursor_on = 1;
	ui->cursor_x = ud->dialview.pos % UI_COLS;
	ui->cursor_y = i;
	/* F-keys info */
	bottom_puts(ui, "clr del");
	ui_flush(ui);

	return 0;
}

/*
 * structure of all views
 */

struct ui_view ui_listview = {
	.name = "list-view",
	.init = init_listview,
	.keypad = keypad_listview,
	.display = display_listview,
};

struct ui_view ui_selectview = {
	.name = "select-view",
	.init = init_selectview,
	.keypad = keypad_selectview,
	.display = display_selectview,
};

struct ui_view ui_dialview = {
	.name = "dial-view",
	.init = init_dialview,
	.keypad = keypad_dialview,
	.display = display_dialview,
};

/*
 * instance handling
 */

int ui_inst_init(struct ui_inst *ui, struct ui_view *uv,
	int (*key_cb)(struct ui_inst *ui, enum ui_key kp),
	int (*beep_cb)(struct ui_inst *ui))
{
	ui->uv = uv;
	ui->key_cb = key_cb;
	ui->beep_cb = beep_cb;

	ui_clearhome(ui);

	/* initialize view */
	uv->init(uv, &ui->ud);

	return 0;
}

int ui_inst_refresh(struct ui_inst *ui)
{
	/* refresh display */
	return ui->uv->display(ui, &ui->ud);
}

/* process keypress at user interface */
int ui_inst_keypad(struct ui_inst *ui, enum ui_key kp)
{
	int rc;

	/* first check if key is handled by callback */
	rc = ui->key_cb(ui, kp);
	if (rc)
		return rc; /* must exit, since key_cb() may reconfigure UI */

	rc = ui->uv->keypad(ui, ui->uv, &ui->ud, kp);
	if (rc < 0)
		ui->beep_cb(ui);

	return rc;
}

