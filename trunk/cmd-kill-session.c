/* $Id: cmd-kill-session.c,v 1.5 2007-12-06 09:46:21 nicm Exp $ */

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
#include <stdlib.h>

#include "tmux.h"

/*
 * Destroy session, detaching all clients attached to it and destroying any
 * windows linked only to this session.
 *
 * Note this deliberately has no alias to make it hard to hit by accident.
 */

void	cmd_kill_session_exec(void *, struct cmd_ctx *);

const struct cmd_entry cmd_kill_session_entry = {
	"kill-session", NULL, "",
	CMD_NOCLIENT,
	NULL,
	cmd_kill_session_exec,
	NULL,
	NULL,
	NULL,
};

void
cmd_kill_session_exec(unused void *ptr, struct cmd_ctx *ctx)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c->session == ctx->session) {
			c->session = NULL;
			server_write_client(c, MSG_EXIT, NULL, 0);
		}
	}

	session_destroy(ctx->session);

	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}
