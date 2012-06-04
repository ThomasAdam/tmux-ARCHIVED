/* $Id$ */

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

#include <ctype.h>

#include "tmux.h"

/*
 * Enter choice mode to choose a buffer.
 */

int	cmd_choose_buffer_exec(struct cmd *, struct cmd_ctx *);

void	cmd_choose_buffer_callback(struct window_choose_data *);
void	cmd_choose_buffer_free(struct window_choose_data *);

const struct cmd_entry cmd_choose_buffer_entry = {
	"choose-buffer", NULL,
	"F:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE " [-F format] [template]",
	0,
	NULL,
	NULL,
	cmd_choose_buffer_exec
};

int
cmd_choose_buffer_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args			*args = self->args;
	struct window_choose_data	*cdata;
	struct winlink			*wl;
	struct paste_buffer		*pb;
	u_int				 idx;
	const char			*template;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (-1);
	}

	if ((template = args_get(args, 'F')) == NULL)
		template = DEFAULT_BUFFER_LIST_TEMPLATE;

	if ((wl = cmd_find_window(ctx, args_get(args, 't'), NULL)) == NULL)
		return (-1);

	if (paste_get_top(&global_buffers) == NULL)
		return (0);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (0);

	idx = 0;
	while ((pb = paste_walk_stack(&global_buffers, &idx)) != NULL) {
		cdata = window_choose_data_create(ctx);
		if (args->argc != 0)
			cdata->action = xstrdup(args->argv[0]);
		else
			cdata->action = xstrdup("paste-buffer -b '%%'");

		cdata->idx = idx - 1;
		cdata->client->references++;

		cdata->ft_template = xstrdup(template);
		format_add(cdata->ft, "line", "%u", idx - 1);
		format_paste_buffer(cdata->ft, pb);

		window_choose_add(wl->window->active, cdata);
	}

	window_choose_ready(wl->window->active,
	    0, cmd_choose_buffer_callback, cmd_choose_buffer_free);

	return (0);
}

void
cmd_choose_buffer_callback(struct window_choose_data *cdata)
{
	u_int				 idx = cdata->idx;

	if (cdata == NULL)
		return;
	if (cdata->client->flags & CLIENT_DEAD)
		return;

	xasprintf(&cdata->raw_format, "%u", idx);
	window_choose_ctx(cdata);
}

void
cmd_choose_buffer_free(struct window_choose_data *data)
{
	struct window_choose_data	*cdata = data;

	if (cdata == NULL)
		return;

	cdata->client->references--;

	xfree(cdata->ft_template);
	xfree(cdata->action);
	xfree(cdata->raw_format);
	format_free(cdata->ft);
	xfree(cdata);
}
