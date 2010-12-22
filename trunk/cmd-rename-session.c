/* $Id: cmd-rename-session.c,v 1.21 2010-12-22 15:36:44 tcunha Exp $ */

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

#include "tmux.h"

/*
 * Change session name.
 */

int	cmd_rename_session_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_rename_session_entry = {
	"rename-session", "rename",
	CMD_TARGET_SESSION_USAGE " new-name",
	CMD_ARG1, "",
	cmd_target_init,
	cmd_target_parse,
	cmd_rename_session_exec,
	cmd_target_free,
	cmd_target_print
};

int
cmd_rename_session_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_target_data	*data = self->data;
	struct session		*s;

	if (data->arg != NULL && session_find(data->arg) != NULL) {
		ctx->error(ctx, "duplicate session: %s", data->arg);
		return (-1);
	}

	if ((s = cmd_find_session(ctx, data->target)) == NULL)
		return (-1);

	RB_REMOVE(sessions, &sessions, s);
	xfree(s->name);
	s->name = xstrdup(data->arg);
	RB_INSERT(sessions, &sessions, s);

	server_status_session(s);

	return (0);
}
