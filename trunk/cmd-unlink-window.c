/* $Id: cmd-unlink-window.c,v 1.15 2009-07-19 00:39:37 tcunha Exp $ */

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

#include "tmux.h"

/*
 * Unlink a window, unless it would be destroyed by doing so (only one link).
 */

int	cmd_unlink_window_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_unlink_window_entry = {
	"unlink-window", "unlinkw",
	"[-k] " CMD_TARGET_WINDOW_USAGE,
	0, CMD_CHFLAG('k'),
	cmd_target_init,
	cmd_target_parse,
	cmd_unlink_window_exec,
	cmd_target_send,
	cmd_target_recv,
	cmd_target_free,
	cmd_target_print
};

int
cmd_unlink_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct winlink		*wl;
	struct session		*s;
	struct client		*c;
	u_int			 i;
	int			 destroyed;

	if ((wl = cmd_find_window(ctx, data->target, &s)) == NULL)
		return (-1);

	if (!(data->chflags & CMD_CHFLAG('k')) && wl->window->references == 1) {
		ctx->error(ctx, "window is only linked to one session");
		return (-1);
	}

 	destroyed = session_detach(s, wl);
	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c == NULL || c->session != s)
			continue;
		if (destroyed) {
			c->session = NULL;
			server_write_client(c, MSG_EXIT, NULL, 0);
		} else
			server_redraw_client(c);
	}
	recalculate_sizes();

	return (0);
}
