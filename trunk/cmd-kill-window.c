/* $Id: cmd-kill-window.c,v 1.17 2009-07-15 17:45:09 nicm Exp $ */

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
 * Destroy window.
 */

int	cmd_kill_window_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_kill_window_entry = {
	"kill-window", "killw",
	CMD_TARGET_WINDOW_USAGE,
	0, 0,
	cmd_target_init,
	cmd_target_parse,
	cmd_kill_window_exec,
	cmd_target_send,
	cmd_target_recv,
	cmd_target_free,
	cmd_target_print
};

int
cmd_kill_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct winlink		*wl;
	struct window		*w;
	struct session		*s;
	struct client		*c;
	u_int		 	 i, j;
	int		 	 destroyed;

	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);
	w = wl->window;

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL || !session_has(s, w))
			continue;
		if ((wl = winlink_find_by_window(&s->windows, w)) == NULL)
			continue;

		destroyed = session_detach(s, wl);
		for (j = 0; j < ARRAY_LENGTH(&clients); j++) {
			c = ARRAY_ITEM(&clients, j);
			if (c == NULL || c->session != s)
				continue;

			if (destroyed) {
				c->session = NULL;
				server_write_client(c, MSG_EXIT, NULL, 0);
			} else
				server_redraw_client(c);
		}
	}
	recalculate_sizes();

	return (0);
}
