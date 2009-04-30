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
 * Increase or decrease pane size.
 */

void	cmd_resize_pane_init(struct cmd *, int);
int	cmd_resize_pane_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_resize_pane_entry = {
	"resize-pane", "resizep",
	CMD_PANE_WINDOW_USAGE "[-DU] [adjustment]",
	CMD_ARG01|CMD_UPPERUFLAG|CMD_UPPERDFLAG,
	cmd_resize_pane_init,
	cmd_pane_parse,
	cmd_resize_pane_exec,
       	cmd_pane_send,
	cmd_pane_recv,
	cmd_pane_free,
	cmd_pane_print
};

void
cmd_resize_pane_init(struct cmd *self, int key)
{
	struct cmd_pane_data	*data;

	cmd_pane_init(self, key);
	data = self->data;

	if (key == KEYC_ADDCTL(KEYC_DOWN))
		data->flags |= CMD_UPPERDFLAG;

	if (key == KEYC_ADDESC(KEYC_UP))
		data->arg = xstrdup("5");
	if (key == KEYC_ADDESC(KEYC_DOWN)) {
		data->flags |= CMD_UPPERDFLAG;
		data->arg = xstrdup("5");
	}
}

int
cmd_resize_pane_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_pane_data	*data = self->data;
	struct winlink		*wl;
	const char	       	*errstr;
	struct window_pane	*wp, *wq;
	u_int			 adjust;
	
	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);
	if (wl->window->layout != 0) {
		ctx->error(ctx, "window not in manual layout");
		return (-1);
	}
	if (data->pane == -1)
		wp = wl->window->active;
	else {
		wp = window_pane_at_index(wl->window, data->pane);
		if (wp == NULL) {
			ctx->error(ctx, "no pane: %d", data->pane);
			return (-1);
		}
	}

	if (data->arg == NULL)
		adjust = 1;
	else {
		adjust = strtonum(data->arg, 1, INT_MAX, &errstr);
		if (errstr != NULL) {
			ctx->error(ctx, "adjustment %s: %s", errstr, data->arg);
			return (-1);
		}
	}

	if (data->flags & CMD_UPPERDFLAG) {
		/*
		 * If this is not the last window, keep trying to increase size
		 * and remove it from the next windows. If it is the last, do
		 * so on the previous window.
		 */
		if (TAILQ_NEXT(wp, entry) == NULL) {
			if (wp == TAILQ_FIRST(&wl->window->panes)) {
				/* Only one pane. */
				return (0);
			}
			wp = TAILQ_PREV(wp, window_panes, entry);
		}
		while (adjust-- > 0) {
			wq = wp;
			while ((wq = TAILQ_NEXT(wq, entry)) != NULL) {
				if (wq->sy <= PANE_MINIMUM)
					continue;
				window_pane_resize(wq, wq->sx, wq->sy - 1);
				break;
			}
			if (wq == NULL)
				break;
		window_pane_resize(wp, wp->sx, wp->sy + 1);
		}
	} else {
		/*
		 * If this is not the last window, keep trying to reduce size
		 * and add to the following window. If it is the last, do so on
		 * the previous window.
		 */
		wq = TAILQ_NEXT(wp, entry);
		if (wq == NULL) {
			if (wp == TAILQ_FIRST(&wl->window->panes)) {
				/* Only one pane. */
				return (0);
			}
			wq = wp;
			wp = TAILQ_PREV(wq, window_panes, entry);
		}
		while (adjust-- > 0) {
			if (wp->sy <= PANE_MINIMUM)
				break;
			window_pane_resize(wq, wq->sx, wq->sy + 1);
			window_pane_resize(wp, wp->sx, wp->sy - 1);
		}
	}
	window_update_panes(wl->window);

	server_redraw_window(wl->window);

	return (0);
}
