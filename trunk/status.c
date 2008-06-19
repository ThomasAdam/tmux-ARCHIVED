/* $Id: status.c,v 1.34 2008-06-19 20:53:04 nicm Exp $ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "tmux.h"

size_t	status_width(struct winlink *);
char   *status_print(struct session *, struct winlink *, u_char *);

/* Draw status for client on the last lines of given context. */
void
status_redraw(struct client *c)
{
	struct screen_redraw_ctx	ctx;
	struct session		       *s = c->session;
	struct winlink		       *wl;
	char		 		*left, *right, *text, *ptr;
	char				lbuf[BUFSIZ], rbuf[BUFSIZ];
	size_t				llen, rlen, offset, xx, yy;
	size_t				size, start, width;
	u_char		 		attr, colr;
	struct tm		       *tm;
	int				larrow, rarrow;

	yy = options_get_number(&s->options, "status-lines");
	if (c->sy == 0 || yy == 0)
		goto off;
	larrow = rarrow = 0;

	if (clock_gettime(CLOCK_REALTIME, &c->status_timer) != 0)
		fatal("clock_gettime failed");
	colr = options_get_colours(&s->options, "status-colour");

	yy = c->sy - yy;
	if (yy == 0)
		goto blank;

	tm = localtime(&(c->status_timer.tv_sec));
	left = options_get_string(&s->options, "status-left");
	strftime(lbuf, sizeof lbuf, left, tm);
	llen = strlen(lbuf);
	right = options_get_string(&s->options, "status-right");
	strftime(rbuf, sizeof rbuf, right, tm);
	rlen = strlen(rbuf);

	/*
	 * Figure out how much space we have for the window list. If there isn't
	 * enough space, just wimp out.
	 */
	xx = 0;
	if (llen != 0)
		xx += llen + 1;
	if (rlen != 0)
		xx += rlen + 1;
	if (c->sx == 0 || c->sx <= xx)
		goto blank;
	xx = c->sx - xx;

	/*
	 * Right. We have xx characters to fill. Find out how much is to go in
	 * them and the offset of the current window (it must be on screen).
	 */
	width = offset = 0;
	RB_FOREACH(wl, winlinks, &s->windows) {
		size = status_width(wl) + 1;
		if (wl == s->curw)
			offset = width;
		width += size;
	}
	start = 0;

	/* If there is enough space for the total width, all is gravy. */
	if (width <= xx)
		goto draw;

	/* Find size of current window text. */
	size = status_width(s->curw);

	/*
	 * If the offset is already on screen, we're good to draw from the
	 * start and just leave off the end.
	 */
	if (offset + size < xx) {
		if (xx > 0) {
			rarrow = 1;
			xx--;
		}

		width = xx;
		goto draw;
	}

	/*
	 * Work out how many characters we need to omit from the start. There
	 * are xx characters to fill, and offset + size must be the last. So,
	 * the start character is offset + size - xx.
	 */
	if (xx > 0) {
		larrow = 1;
		xx--;
	}

	start = offset + size - xx;
 	if (xx > 0 && width > start + xx + 1) { /* + 1, eh? */
 		rarrow = 1;
 		start++;
 		xx--;
 	}
 	width = xx;

draw:
	/* Bail here if anything is too small too. XXX. */
	if (width == 0 || xx == 0)
		goto blank;

 	/* Begin drawing and move to the starting position. */
	screen_redraw_start_client(&ctx, c);
	screen_redraw_set_attributes(&ctx, 0, colr);
	if (llen != 0) {
 		screen_redraw_move_cursor(&ctx, 0, yy);
		screen_redraw_write_string(&ctx, "%s ", lbuf);
		if (larrow)
			ctx.write(ctx.data, TTY_CHARACTER, ' ');
	} else {
		if (larrow)
			screen_redraw_move_cursor(&ctx, 1, yy);
		else
			screen_redraw_move_cursor(&ctx, 0, yy);
	}

	/* Draw each character in succession. */
	offset = 0;
	RB_FOREACH(wl, winlinks, &s->windows) {
		text = status_print(s, wl, &attr);
		screen_redraw_set_attributes(&ctx, attr, colr);

		if (larrow == 1 && offset < start) {
			if (session_alert_has(s, wl, WINDOW_ACTIVITY))
				larrow = -1;
			if (session_alert_has(s, wl, WINDOW_BELL))
				larrow = -1;
		}

 		for (ptr = text; *ptr != '\0'; ptr++) {
			if (offset >= start && offset < start + width)
				ctx.write(ctx.data, TTY_CHARACTER, *ptr);
			offset++;
		}

		if (rarrow == 1 && offset > start + width) {
			if (session_alert_has(s, wl, WINDOW_ACTIVITY))
				rarrow = -1;
			if (session_alert_has(s, wl, WINDOW_BELL))
				rarrow = -1;
		}

		if (offset < start + width) {
			if (offset >= start) {
				screen_redraw_set_attributes(&ctx, 0, colr);
				ctx.write(ctx.data, TTY_CHARACTER, ' ');
			}
			offset++;
		}

		xfree(text);
	}

	/* Fill the remaining space if any. */
	screen_redraw_set_attributes(&ctx, 0, colr);
 	while (offset++ < xx)
		ctx.write(ctx.data, TTY_CHARACTER, ' ');

	/* Draw the last item. */
	if (rlen != 0) {
		screen_redraw_move_cursor(&ctx, c->sx - rlen - 1, yy);
		screen_redraw_write_string(&ctx, " %s", rbuf);
	}

	/* Draw the arrows. */
	if (larrow != 0) {
		if (larrow == -1)
			screen_redraw_set_attributes(&ctx, ATTR_REVERSE, colr);
		else
			screen_redraw_set_attributes(&ctx, 0, colr);
		if (llen != 0)
			screen_redraw_move_cursor(&ctx, llen + 1, yy);
		else
			screen_redraw_move_cursor(&ctx, 0, yy);
 		ctx.write(ctx.data, TTY_CHARACTER, '<');
	}
	if (rarrow != 0) {
		if (rarrow == -1)
			screen_redraw_set_attributes(&ctx, ATTR_REVERSE, colr);
		else
			screen_redraw_set_attributes(&ctx, 0, colr);
		if (rlen != 0)
			screen_redraw_move_cursor(&ctx, c->sx - rlen - 2, yy);
		else
			screen_redraw_move_cursor(&ctx, c->sx - 1, yy);
 		ctx.write(ctx.data, TTY_CHARACTER, '>');
	}

	screen_redraw_stop(&ctx);
	return;

blank:
 	/* Just draw the whole line as blank. */
	screen_redraw_start_client(&ctx, c);
	screen_redraw_set_attributes(&ctx, 0, colr);
	screen_redraw_move_cursor(&ctx, 0, yy);
	for (offset = 0; offset < c->sx; offset++)
		ctx.write(ctx.data, TTY_CHARACTER, ' ');
	screen_redraw_stop(&ctx);

	return;

off:
	/*
	 * Draw the real window last line. Necessary to wipe over message if
	 * status is off. Not sure this is the right place for this.
	 */
	screen_redraw_start_client(&ctx, c);
	/* If the screen is too small, use blank. */
	if (screen_size_y(c->session->curw->window->screen) < c->sy) {
		screen_redraw_move_cursor(&ctx, 0, c->sy - 1);
		screen_redraw_set_attributes(&ctx, 0, 0x88);
		for (offset = 0; offset < c->sx; offset++)
			ctx.write(ctx.data, TTY_CHARACTER, ' ');
	} else
		screen_redraw_lines(&ctx, c->sy - 1, 1);
	screen_redraw_stop(&ctx);

	return;
}

size_t
status_width(struct winlink *wl)
{
	return (xsnprintf(NULL, 0, "%d:%s ", wl->idx, wl->window->name));
}

char *
status_print(struct session *s, struct winlink *wl, u_char *attr)
{
	char   *text, flag;

	flag = ' ';
 	if (wl == s->lastw)
		flag = '-';
	if (wl == s->curw)
		flag = '*';

	*attr = 0;
	if (session_alert_has(s, wl, WINDOW_ACTIVITY)) {
		flag = '#';
		*attr = ATTR_REVERSE;
	}
	if (session_alert_has(s, wl, WINDOW_BELL)) {
		flag = '!';
		*attr = ATTR_REVERSE;
	}

	xasprintf(&text, "%d:%s%c", wl->idx, wl->window->name, flag);
	return (text);
}

/* Draw client message on status line of present else on last line. */
void
status_message_redraw(struct client *c)
{
	struct screen_redraw_ctx	ctx;
	size_t			        xx, yy;

	if (c->sx == 0 || c->sy == 0)
		return;

	xx = strlen(c->message_string);
	if (xx > c->sx)
		xx = c->sx;
	yy = c->sy - 1;		

	screen_redraw_start_client(&ctx, c);
	screen_redraw_set_attributes(&ctx, ATTR_REVERSE, 0x88);

	screen_redraw_move_cursor(&ctx, 0, yy);
	screen_redraw_write_string(&ctx, "%.*s", (int) xx, c->message_string);
	for (; xx < c->sx; xx++)
		ctx.write(ctx.data, TTY_CHARACTER, ' ');
	screen_redraw_stop(&ctx);

	/* Force cursor off. */
	tty_write_client(c, TTY_CURSOROFF);
}

/* Draw client prompt on status line of present else on last line. */
void
status_prompt_redraw(struct client *c)
{
	struct screen_redraw_ctx	ctx;
	size_t			        i, xx, yy, left, size, offset;

	if (c->sx == 0 || c->sy == 0)
		return;
	offset = 0;

	xx = strlen(c->prompt_string);
	if (xx > c->sx)
		xx = c->sx;
	yy = c->sy - 1;		

	screen_redraw_start_client(&ctx, c);
	screen_redraw_set_attributes(&ctx, ATTR_REVERSE, 0x88);

	screen_redraw_move_cursor(&ctx, 0, yy);
	screen_redraw_write_string(&ctx, "%.*s", (int) xx, c->prompt_string);

	left = c->sx - xx;
	if (left != 0) {
		if (c->prompt_index < left)
			size = strlen(c->prompt_buffer);
		else {
			offset = c->prompt_index - left;
			if (c->prompt_index == strlen(c->prompt_buffer))
				left--;
			size = left;
		}
		screen_redraw_write_string(
		    &ctx, "%.*s", (int) left, c->prompt_buffer + offset);

		for (i = xx + size; i < c->sx; i++)
			ctx.write(ctx.data, TTY_CHARACTER, ' ');
	}

	screen_redraw_stop(&ctx);

	/* Force cursor on. */
	tty_write_client(c, TTY_CURSORMOVE, yy, xx + c->prompt_index - offset);
	tty_write_client(c, TTY_CURSORON);
}

/* Handle keys in prompt. */
void
status_prompt_key(struct client *c, int key)
{
	size_t	size;

	size = strlen(c->prompt_buffer);
	switch (key) {
	case KEYC_LEFT:
		if (c->prompt_index > 0) {
			c->prompt_index--;
			c->flags |= CLIENT_STATUS;
		}
		break;
	case KEYC_RIGHT:
		if (c->prompt_index < size) {
			c->prompt_index++;
			c->flags |= CLIENT_STATUS;
		}
		break;
	case '\001':	/* C-a */
		if (c->prompt_index != 0) {
			c->prompt_index = 0;
			c->flags |= CLIENT_STATUS;
		}
		break;
	case '\005':	/* C-e */
		if (c->prompt_index != size) {
			c->prompt_index = size;
			c->flags |= CLIENT_STATUS;
		}
		break;
	case '\010':
	case '\177':
		if (c->prompt_index != 0) {
			if (c->prompt_index == size)
				c->prompt_buffer[--c->prompt_index] = '\0';
			else {
				memmove(c->prompt_buffer + c->prompt_index - 1, 
				    c->prompt_buffer + c->prompt_index,
				    size + 1 - c->prompt_index);
				c->prompt_index--;
			}
			c->flags |= CLIENT_STATUS;
		}
		break;
	case KEYC_DC:
		if (c->prompt_index != size) {
			memmove(c->prompt_buffer + c->prompt_index, 
			    c->prompt_buffer + c->prompt_index + 1,
			    size + 1 - c->prompt_index);
			c->flags |= CLIENT_STATUS;
		}
		break;
 	case '\r':	/* enter */
		if (*c->prompt_buffer != '\0') {
			c->prompt_callback(c->prompt_data, c->prompt_buffer);
			server_clear_client_prompt(c);
			break;
		}
		/* FALLTHROUGH */
	case '\e':	/* escape */
		c->prompt_callback(c->prompt_data, NULL);
		server_clear_client_prompt(c);
		break;
	default:
		if (key < 32)
			break;
		c->prompt_buffer = xrealloc(c->prompt_buffer, 1, size + 2);

		if (c->prompt_index == size) {
			c->prompt_buffer[c->prompt_index++] = key;
			c->prompt_buffer[c->prompt_index] = '\0';
		} else {
			memmove(c->prompt_buffer + c->prompt_index + 1, 
			    c->prompt_buffer + c->prompt_index,
			    size + 1 - c->prompt_index);
			c->prompt_buffer[c->prompt_index++] = key;
		}

		c->flags |= CLIENT_STATUS;
		break;
	}
}
