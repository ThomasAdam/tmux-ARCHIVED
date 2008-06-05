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

#include "tmux.h"

/*
 * Cause client to report an error and exit with 1 if session doesn't exist.
 */

void	cmd_has_session_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_has_session_entry = {
	"has-session", "has",
	CMD_SESSIONONLY_USAGE,
	0,
	cmd_sessiononly_parse,
	cmd_has_session_exec,
	cmd_sessiononly_send,
	cmd_sessiononly_recv,
	cmd_sessiononly_free,
	NULL,
	cmd_sessiononly_print
};

void
cmd_has_session_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct session	*s;

	if ((s = cmd_sessiononly_get(self, ctx)) == NULL)
		return;

	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}
