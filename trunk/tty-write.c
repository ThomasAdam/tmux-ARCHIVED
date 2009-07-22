/* $Id: tty-write.c,v 1.20 2009-07-22 18:08:56 tcunha Exp $ */

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

#include <string.h>

#include "tmux.h"

void
tty_write0(struct window_pane *wp, enum tty_cmd cmd)
{
	struct tty_ctx	ctx;

	memset(&ctx, 0, sizeof ctx);
	ctx.wp = wp;
	tty_write_cmd(cmd, &ctx);
}

void
tty_writenum(struct window_pane *wp, enum tty_cmd cmd, u_int num)
{
	struct tty_ctx	ctx;

	memset(&ctx, 0, sizeof ctx);
	ctx.wp = wp;
	ctx.num = num;
	tty_write_cmd(cmd, &ctx);
}

void
tty_writeptr(struct window_pane *wp, enum tty_cmd cmd, void *ptr)
{
	struct tty_ctx	ctx;

	memset(&ctx, 0, sizeof ctx);
	ctx.wp = wp;
	ctx.ptr = ptr;
	tty_write_cmd(cmd, &ctx);
}

void
tty_write_cmd(enum tty_cmd cmd, struct tty_ctx *ctx)
{
	struct window_pane	*wp = ctx->wp;
	struct client		*c;
	u_int		 	 i;

	if (wp == NULL)
		return;

	if (wp->window->flags & WINDOW_REDRAW || wp->flags & PANE_REDRAW)
		return;
	if (wp->window->flags & WINDOW_HIDDEN || !window_pane_visible(wp))
		return;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session == NULL)
			continue;
		if (c->flags & CLIENT_SUSPENDED)
			continue;

		if (c->session->curw->window == wp->window) {
			tty_update_mode(&c->tty, c->tty.mode & ~MODE_CURSOR);

			tty_write(&c->tty, cmd, ctx);
		}
	}
}
