/* $Id$ */

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

#include <getopt.h>
#include <unistd.h>

#include "tmux.h"

/*
 * List windows on given session.
 */

void	cmd_list_windows_exec(void *, struct cmd_ctx *);

const struct cmd_entry cmd_list_windows_entry = {
	"list-windows", "lsw", 0,
	NULL,
	NULL,
	cmd_list_windows_exec,
	NULL,
	NULL,
	NULL
};

void
cmd_list_windows_exec(unused void *ptr, struct cmd_ctx *ctx)
{
	struct client	*c = ctx->client;
	struct session	*s = ctx->session;
	struct winlink	*wl;
	struct window	*w;

	RB_FOREACH(wl, winlinks, &s->windows) {
		w = wl->window;
		ctx->print(ctx, "%u: %s \"%s\" (%s) [%ux%u]", wl->idx,
		    w->name, w->screen.title, ttyname(w->fd),
		    w->screen.sx, w->screen.sy);
	}

	if (!(ctx->flags & CMD_KEY))
		server_write_client(c, MSG_EXIT, NULL, 0);
}
