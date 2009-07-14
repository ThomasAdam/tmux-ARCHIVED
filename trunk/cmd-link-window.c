/* $Id: cmd-link-window.c,v 1.30 2009-07-14 06:43:32 nicm Exp $ */

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
 * Link a window into another session.
 */

int	cmd_link_window_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_link_window_entry = {
	"link-window", "linkw",
	"[-dk] " CMD_SRCDST_WINDOW_USAGE,
	0, CMD_CHFLAG('d')|CMD_CHFLAG('k'),
	cmd_srcdst_init,
	cmd_srcdst_parse,
	cmd_link_window_exec,
	cmd_srcdst_send,
	cmd_srcdst_recv,
	cmd_srcdst_free,
	cmd_srcdst_print
};

int
cmd_link_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_srcdst_data	*data = self->data;
	struct session		*dst;
	struct winlink		*wl_src, *wl_dst;
	char			*cause;
	int			 idx;

	if ((wl_src = cmd_find_window(ctx, data->src, NULL)) == NULL)
		return (-1);
	if ((idx = cmd_find_index(ctx, data->dst, &dst)) == -2)
		return (-1);

	wl_dst = NULL;
	if (idx != -1)
		wl_dst = winlink_find_by_index(&dst->windows, idx);
	if (wl_dst != NULL) {
		if (wl_dst->window == wl_src->window)
			return (0);

		if (data->chflags & CMD_CHFLAG('k')) {
			/*
			 * Can't use session_detach as it will destroy session
			 * if this makes it empty.
			 */
			session_alert_cancel(dst, wl_dst);
			winlink_stack_remove(&dst->lastw, wl_dst);
			winlink_remove(&dst->windows, wl_dst);

			/* Force select/redraw if current. */
			if (wl_dst == dst->curw) {
				data->chflags &= ~CMD_CHFLAG('d');
				dst->curw = NULL;
			}
		}
	}

	wl_dst = session_attach(dst, wl_src->window, idx, &cause);
	if (wl_dst == NULL) {
		ctx->error(ctx, "create session failed: %s", cause);
		xfree(cause);
		return (-1);
	}

	if (data->chflags & CMD_CHFLAG('d'))
		server_status_session(dst);
	else {
		session_select(dst, wl_dst->idx);
		server_redraw_session(dst);
	}
	recalculate_sizes();

	return (0);
}
