/* $Id: status.c,v 1.17 2008-01-03 20:01:47 nicm Exp $ */

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

#include "tmux.h"

void printflike3 status_print(struct buffer *, size_t *, const char *, ...);

void
status_write_client(struct client *c)
{
	struct screen_redraw_ctx	ctx;
	struct winlink	       	       *wl;
	char				flag;

	if (status_lines == 0 || c->sy <= status_lines)
		return;

	screen_redraw_start_client(&ctx, c);
	screen_redraw_move_cursor(&ctx, 0, c->sy - status_lines);
	screen_redraw_set_attributes(&ctx, 0, status_colour);

	RB_FOREACH(wl, winlinks, &c->session->windows) {
		flag = ' ';
		if (wl == c->session->lastw)
			flag = '-';
		if (wl == c->session->curw)
			flag = '*';
		if (session_hasbell(c->session, wl))
			flag = '!';
		screen_redraw_write_string(
		    &ctx, "%d:%s%c ", wl->idx, wl->window->name, flag);

		if (ctx.s->cx > screen_last_x(ctx.s))
			break;
	}
	screen_redraw_clear_end_of_line(&ctx);

	screen_redraw_stop(&ctx);
}

void
status_write_window(struct window *w)
{
	struct client	*c;
	u_int		 i;

	if (w->flags & WINDOW_HIDDEN)
		return;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->session->curw->window != w)
			continue;

		status_write_client(c);
	}
}

void
status_write_session(struct session *s)
{
	struct client	*c;
	u_int		 i;

	if (s->flags & SESSION_UNATTACHED)
		return;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session != s)
			continue;
		status_write_client(c);
	}
}
