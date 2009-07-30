/* $Id$ */

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

#include <stdlib.h>

#include "tmux.h"

/*
 * Break pane off into a window.
 */

int	cmd_break_pane_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_break_pane_entry = {
	"break-pane", "breakp",
	CMD_TARGET_PANE_USAGE " [-d]",
	0, CMD_CHFLAG('d'),
	cmd_target_init,
	cmd_target_parse,
	cmd_break_pane_exec,
	cmd_target_free,
	cmd_target_print
};

int
cmd_break_pane_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct winlink		*wl;
	struct session		*s;
	struct window_pane	*wp;
	struct window		*w;
	char			*cause;

	if ((wl = cmd_find_pane(ctx, data->target, &s, &wp)) == NULL)
		return (-1);

	if (window_count_panes(wl->window) == 1) {
		ctx->error(ctx, "can't break with only one pane");
		return (-1);
	}

	TAILQ_REMOVE(&wl->window->panes, wp, entry);
	if (wl->window->active == wp) {
		wl->window->active = TAILQ_PREV(wp, window_panes, entry);
		if (wl->window->active == NULL)
			wl->window->active = TAILQ_NEXT(wp, entry);
	}
 	layout_close_pane(wp);

 	w = wp->window = window_create1(s->sx, s->sy);
 	TAILQ_INSERT_HEAD(&w->panes, wp, entry);
 	w->active = wp;
 	w->name = default_window_name(w);
	layout_init(w);

 	wl = session_attach(s, w, -1, &cause); /* can't fail */
 	if (!(data->chflags & CMD_CHFLAG('d')))
 		session_select(s, wl->idx);

	server_redraw_session(s);

	return (0);
}
