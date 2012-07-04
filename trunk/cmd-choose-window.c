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

#include <ctype.h>

#include "tmux.h"

/*
 * Enter choice mode to choose a window.
 */

int	cmd_choose_window_exec(struct cmd *, struct cmd_ctx *);

void	cmd_choose_window_callback(struct window_choose_data *);
void	cmd_choose_window_free(struct window_choose_data *);

const struct cmd_entry cmd_choose_window_entry = {
	"choose-window", NULL,
	"F:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE " [-F format] [template]",
	0,
	NULL,
	NULL,
	cmd_choose_window_exec
};

int
cmd_choose_window_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args			*args = self->args;
	struct session			*s;
	struct winlink			*wl, *wm;
	const char			*template;
	char				*action;
	u_int			 	 idx, cur;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (-1);
	}
	s = ctx->curclient->session;

	if ((wl = cmd_find_window(ctx, args_get(args, 't'), NULL)) == NULL)
		return (-1);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (0);

	if ((template = args_get(args, 'F')) == NULL)
		template = DEFAULT_WINDOW_TEMPLATE " \"#{pane_title}\"";

	if (args->argc != 0)
		action = xstrdup(args->argv[0]);
	else
		action = xstrdup("select-window -t '%%'");

	cur = idx = 0;
	RB_FOREACH(wm, winlinks, &s->windows) {
		if (wm == s->curw)
			cur = idx;
		idx++;

		window_choose_add_window(wl->window->active, ctx, s, wm,
		    template, action, idx);
	}
	xfree(action);

	window_choose_ready(wl->window->active,
	    cur, cmd_choose_window_callback, cmd_choose_window_free);

	return (0);
}

void
cmd_choose_window_callback(struct window_choose_data *cdata)
{
	struct session	*s;

	if (cdata == NULL)
		return;
	if (cdata->client->flags & CLIENT_DEAD)
		return;

	s = cdata->session;
	if (!session_alive(s))
		return;

	window_choose_ctx(cdata);
}

void
cmd_choose_window_free(struct window_choose_data *cdata)
{
	if (cdata == NULL)
		return;

	cdata->session->references--;
	cdata->client->references--;

	xfree(cdata->ft_template);
	xfree(cdata->command);
	format_free(cdata->ft);
	xfree(cdata);
}
