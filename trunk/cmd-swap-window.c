/* $Id: cmd-swap-window.c,v 1.14 2008-12-10 20:25:41 nicm Exp $ */

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

#include <stdlib.h>

#include "tmux.h"

/*
 * Swap one window with another.
 */

void	cmd_swap_window_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_swap_window_entry = {
	"swap-window", "swapw",
	"[-d] " CMD_SRCDST_WINDOW_USAGE,
	CMD_DFLAG,
	cmd_srcdst_init,
	cmd_srcdst_parse,
	cmd_swap_window_exec,
	cmd_srcdst_send,
	cmd_srcdst_recv,
	cmd_srcdst_free,
	cmd_srcdst_print
};

void
cmd_swap_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_srcdst_data	*data = self->data;
	struct session		*src, *dst;
	struct winlink		*wl_src, *wl_dst;
	struct window		*w;

	if ((wl_src = cmd_find_window(ctx, data->src, &src)) == NULL)
		return;
	if ((wl_dst = cmd_find_window(ctx, data->dst, &dst)) == NULL)
		return;

	if (wl_dst->window == wl_src->window)
		goto out;

	w = wl_dst->window;
	wl_dst->window = wl_src->window;
	wl_src->window = w;

	if (!(data->flags & CMD_DFLAG)) {
		session_select(dst, wl_dst->idx);
		if (src != dst)
			session_select(src, wl_src->idx);
	}
	server_redraw_session(src);
	if (src != dst)
		server_redraw_session(dst);
	recalculate_sizes();

out:
	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}
