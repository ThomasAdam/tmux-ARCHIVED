/* $Id: cmd-show-options.c,v 1.22 2011/01/03 23:52:38 tcunha Exp $ */

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

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Show options.
 */

int	cmd_show_options_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_show_options_entry = {
	"show-options", "show",
	"[-gsw] [-t target-session|target-window]",
	0, "gsw",
	cmd_target_init,
	cmd_target_parse,
	cmd_show_options_exec,
	cmd_target_free,
	cmd_target_print
};

int
cmd_show_options_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data			*data = self->data;
	const struct options_table_entry	*table, *oe;
	struct session				*s;
	struct winlink				*wl;
	struct options				*oo;
	struct options_entry			*o;
	const char				*optval;

	if (cmd_check_flag(data->chflags, 's')) {
		oo = &global_options;
		table = server_options_table;
	} else if (cmd_check_flag(data->chflags, 'w')) {
		table = window_options_table;
		if (cmd_check_flag(data->chflags, 'g'))
			oo = &global_w_options;
		else {
			wl = cmd_find_window(ctx, data->target, NULL);
			if (wl == NULL)
				return (-1);
			oo = &wl->window->options;
		}
	} else {
		table = session_options_table;
		if (cmd_check_flag(data->chflags, 'g'))
			oo = &global_s_options;
		else {
			s = cmd_find_session(ctx, data->target);
			if (s == NULL)
				return (-1);
			oo = &s->options;
		}
	}

	for (oe = table; oe->name != NULL; oe++) {
		if ((o = options_find1(oo, oe->name)) == NULL)
			continue;
		optval = options_table_print_entry(oe, o);
		ctx->print(ctx, "%s %s", oe->name, optval);
	}

	return (0);
}
