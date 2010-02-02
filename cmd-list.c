/* $Id: cmd-list.c,v 1.7 2010/02/02 23:51:04 tcunha Exp $ */

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

#include <string.h>

#include "tmux.h"

struct cmd_list *
cmd_list_parse(int argc, char **argv, char **cause)
{
	struct cmd_list	*cmdlist;
	struct cmd	*cmd;
	int		 i, lastsplit;
	size_t		 arglen, new_argc;
	char	       **new_argv;

	cmdlist = xmalloc(sizeof *cmdlist);
	TAILQ_INIT(cmdlist);

	lastsplit = 0;
	for (i = 0; i < argc; i++) {
		arglen = strlen(argv[i]);
		if (arglen == 0 || argv[i][arglen - 1] != ';')
			continue;
		argv[i][arglen - 1] = '\0';

		if (arglen > 1 && argv[i][arglen - 2] == '\\') {
			argv[i][arglen - 2] = ';';
			continue;
		}

		new_argc = i - lastsplit;
		new_argv = argv + lastsplit;
		if (arglen != 1)
			new_argc++;

		cmd = cmd_parse(new_argc, new_argv, cause);
		if (cmd == NULL)
			goto bad;
		TAILQ_INSERT_TAIL(cmdlist, cmd, qentry);

		lastsplit = i + 1;
	}

	if (lastsplit != argc) {
		cmd = cmd_parse(argc - lastsplit, argv + lastsplit, cause);
		if (cmd == NULL)
			goto bad;
		TAILQ_INSERT_TAIL(cmdlist, cmd, qentry);
	}

	return (cmdlist);

bad:
	cmd_list_free(cmdlist);
	return (NULL);
}

int
cmd_list_exec(struct cmd_list *cmdlist, struct cmd_ctx *ctx)
{
	struct cmd	*cmd;
	int		 n, retval;

	retval = 0;
	TAILQ_FOREACH(cmd, cmdlist, qentry) {
		if ((n = cmd_exec(cmd, ctx)) == -1)
			return (-1);

		/*
		 * A 1 return value means the command client is being attached
		 * (sent MSG_READY).
		 */
		if (n == 1) {
			retval = 1;

			/*
			 * The command client has been attached, so mangle the
			 * context to treat any following commands as if they
			 * were called from inside.
			 */
			if (ctx->curclient == NULL) {
				ctx->curclient = ctx->cmdclient;
				ctx->cmdclient = NULL;
			}
		}
	}
	return (retval);
}

void
cmd_list_free(struct cmd_list *cmdlist)
{
	struct cmd	*cmd;

	while (!TAILQ_EMPTY(cmdlist)) {
		cmd = TAILQ_FIRST(cmdlist);
		TAILQ_REMOVE(cmdlist, cmd, qentry);
		cmd_free(cmd);
	}
	xfree(cmdlist);
}

size_t
cmd_list_print(struct cmd_list *cmdlist, char *buf, size_t len)
{
	struct cmd	*cmd;
	size_t		 off;

	off = 0;
	TAILQ_FOREACH(cmd, cmdlist, qentry) {
		if (off >= len)
			break;
		off += cmd_print(cmd, buf + off, len - off);
		if (off >= len)
			break;
		if (TAILQ_NEXT(cmd, qentry) != NULL)
			off += xsnprintf(buf + off, len - off, " ; ");
	}
	return (off);
}
