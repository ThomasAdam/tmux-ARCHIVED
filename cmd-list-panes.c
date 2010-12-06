/* $Id: cmd-list-panes.c,v 1.6 2010/12/06 21:56:32 nicm Exp $ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
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

#include <unistd.h>

#include "tmux.h"

/*
 * List panes on given window.
 */

int	cmd_list_panes_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_list_panes_entry = {
	"list-panes", "lsp",
	CMD_TARGET_WINDOW_USAGE,
	0, "",
	cmd_target_init,
	cmd_target_parse,
	cmd_list_panes_exec,
	cmd_target_free,
	cmd_target_print
};

int
cmd_list_panes_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct winlink		*wl;
	struct window_pane	*wp;
	struct grid		*gd;
	struct grid_line	*gl;
	u_int			 i, n;
	unsigned long long	 size;

	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);

	n = 0;
	TAILQ_FOREACH(wp, &wl->window->panes, entry) {
		gd = wp->base.grid;

		size = 0;
		for (i = 0; i < gd->hsize; i++) {
			gl = &gd->linedata[i];
			size += gl->cellsize * sizeof *gl->celldata;
			size += gl->utf8size * sizeof *gl->utf8data;
		}
		size += gd->hsize * sizeof *gd->linedata;

		ctx->print(ctx, "%u: [%ux%u] [history %u/%u, %llu bytes]%s%s",
		    n, wp->sx, wp->sy, gd->hsize, gd->hlimit, size,
		    wp == wp->window->active ? " (active)" : "",
		    wp->fd == -1 ? " (dead)" : "");
		n++;
	}

	return (0);
}
