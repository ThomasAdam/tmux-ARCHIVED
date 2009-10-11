/* $Id: cmd-move-window.c,v 1.12 2009/10/11 23:38:16 tcunha Exp $ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicm@users.sourceforge.net>
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
 * Move a window.
 */

int	cmd_move_window_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_move_window_entry = {
	"move-window", "movew",
	"[-dk] " CMD_SRCDST_WINDOW_USAGE,
	0, CMD_CHFLAG('d')|CMD_CHFLAG('k'),
	cmd_srcdst_init,
	cmd_srcdst_parse,
	cmd_move_window_exec,
	cmd_srcdst_free,
	cmd_srcdst_print
};

int
cmd_move_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_srcdst_data	*data = self->data;
	struct session		*src, *dst;
	struct winlink		*wl;
	char			*cause;
	int			 idx, kflag, dflag;

	if ((wl = cmd_find_window(ctx, data->src, &src)) == NULL)
		return (-1);
	if ((idx = cmd_find_index(ctx, data->dst, &dst)) == -2)
		return (-1);

	kflag = data->chflags & CMD_CHFLAG('k');
	dflag = data->chflags & CMD_CHFLAG('d');
	if (server_link_window(src, wl, dst, idx, kflag, !dflag, &cause) != 0) {
		ctx->error(ctx, "can't move window: %s", cause);
		xfree(cause);
		return (-1);
	}
	server_unlink_window(src, wl);
	recalculate_sizes();

	return (0);
}
