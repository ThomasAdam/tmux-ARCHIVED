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

#include "tmux.h"

/*
 * Create a new session and attach to the current terminal unless -d is given.
 */

int	cmd_new_session_parse(struct cmd *, void **, int, char **, char **);
void	cmd_new_session_exec(void *, struct cmd_ctx *);
void	cmd_new_session_send(void *, struct buffer *);
void	cmd_new_session_recv(void **, struct buffer *);
void	cmd_new_session_free(void *);

struct cmd_new_session_data {
	char	*name;
	char	*winname;
	char	*cmd;
	int	 flag_detached;
};

const struct cmd_entry cmd_new_session_entry = {
	"new-session", "new",
	"[-d] [-s session-name] [-n window-name] [command]",
	CMD_STARTSERVER|CMD_CANTNEST,
	cmd_new_session_parse,
	cmd_new_session_exec,
	cmd_new_session_send,
	cmd_new_session_recv,
	cmd_new_session_free
};

int
cmd_new_session_parse(
    struct cmd *self, void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_new_session_data	*data;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->flag_detached = 0;
	data->name = NULL;
	data->winname = NULL;
	data->cmd = NULL;

	while ((opt = getopt(argc, argv, "ds:n:")) != EOF) {
		switch (opt) {
		case 'd':
			data->flag_detached = 1;
			break;
		case 's':
			data->name = xstrdup(optarg);
			break;
		case 'n':
			data->winname = xstrdup(optarg);
			break;
		default:
			goto usage;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0 && argc != 1)
		goto usage;

	if (argc == 1)
		data->cmd = xstrdup(argv[0]);

	return (0);

usage:
	xasprintf(cause, "usage: %s %s", self->entry->name, self->entry->usage);

	cmd_new_session_free(data);
	return (-1);
}

void
cmd_new_session_exec(void *ptr, struct cmd_ctx *ctx)
{
	struct cmd_new_session_data	*data = ptr;
	struct cmd_new_session_data	 std = { NULL, NULL, NULL, 0 };
	struct client			*c = ctx->cmdclient;
	struct session			*s;
	char				*cmd, *cause;
	u_int				 sx, sy;

	if (data == NULL)
		data = &std;

	if (ctx->flags & CMD_KEY)
		return;

	if (!data->flag_detached) {
		if (c == NULL) {
			ctx->error(ctx, "no client to attach to");
			return;
		}
		if (!(c->flags & CLIENT_TERMINAL)) {
			ctx->error(ctx, "not a terminal");
			return;
		}
	}

	if (data->name != NULL && session_find(data->name) != NULL) {
		ctx->error(ctx, "duplicate session: %s", data->name);
		return;
	}

	cmd = data->cmd;
	if (cmd == NULL)
		cmd = default_command;

	sx = 80;
	sy = 25;
	if (!data->flag_detached) {
		sx = c->sx;
		sy = c->sy;
	}
	if (sy < status_lines)
		sy = status_lines + 1;
	sy -= status_lines;

	if (!data->flag_detached && tty_open(&c->tty, &cause) != 0) {
		ctx->error(ctx, "%s", cause);
		xfree(cause);
		return;
	}


	if ((s = session_create(data->name, cmd, sx, sy)) == NULL)
		fatalx("session_create failed");
	if (data->winname != NULL) {
		xfree(s->curw->window->name);
		s->curw->window->name = xstrdup(data->winname);
	}

	if (data->flag_detached) {
		if (c != NULL)
			server_write_client(c, MSG_EXIT, NULL, 0);
	} else {
		c->session = s;
		server_write_client(c, MSG_READY, NULL, 0);
		server_redraw_client(c);
	}
}

void
cmd_new_session_send(void *ptr, struct buffer *b)
{
	struct cmd_new_session_data	*data = ptr;

	buffer_write(b, data, sizeof *data);
	cmd_send_string(b, data->name);
	cmd_send_string(b, data->winname);
	cmd_send_string(b, data->cmd);
}

void
cmd_new_session_recv(void **ptr, struct buffer *b)
{
	struct cmd_new_session_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	buffer_read(b, data, sizeof *data);
	data->name = cmd_recv_string(b);
	data->winname = cmd_recv_string(b);
	data->cmd = cmd_recv_string(b);
}

void
cmd_new_session_free(void *ptr)
{
	struct cmd_new_session_data	*data = ptr;

	if (data->name != NULL)
		xfree(data->name);
	if (data->winname != NULL)
		xfree(data->winname);
	if (data->cmd != NULL)
		xfree(data->cmd);
	xfree(data);
}
