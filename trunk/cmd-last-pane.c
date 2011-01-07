/* $Id: cmd-last-pane.c,v 1.2 2011-01-07 14:45:34 tcunha Exp $ */

/*
 * Copyright (c) 2010 Nicholas Marriott <nicm@users.sourceforge.net>
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
 * Move to last pane.
 */

int	cmd_last_pane_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_last_pane_entry = {
	"last-pane", "lastp",
	"t:", 0, 0,
	CMD_TARGET_WINDOW_USAGE,
	0,
	NULL,
	NULL,
	cmd_last_pane_exec
};

int
cmd_last_pane_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args	*args = self->args;
	struct winlink	*wl;
	struct window	*w;

	if ((wl = cmd_find_window(ctx, args_get(args, 't'), NULL)) == NULL)
		return (-1);
	w = wl->window;

	if (w->last == NULL) {
		ctx->error(ctx, "no last pane");
		return (-1);
	}
	window_set_active_pane(w, w->last);

	return (0);
}
