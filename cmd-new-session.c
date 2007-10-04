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

int		 cmd_new_session_parse(void **, int, char **, char **);
const char	*cmd_new_session_usage(void);
void		 cmd_new_session_exec(void *, struct cmd_ctx *);
void		 cmd_new_session_send(void *, struct buffer *);
void		 cmd_new_session_recv(void **, struct buffer *);
void		 cmd_new_session_free(void *);

struct cmd_new_session_data {
	char	*name;
	int	 flag_detached;
};

const struct cmd_entry cmd_new_session_entry = {
	CMD_NEWSESSION, "new-session", "new", CMD_STARTSERVER|CMD_NOSESSION,
	cmd_new_session_parse,
	cmd_new_session_usage,
	cmd_new_session_exec, 
	cmd_new_session_send,
	cmd_new_session_recv,
	cmd_new_session_free
};

int
cmd_new_session_parse(void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_new_session_data	*data;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->flag_detached = 0;
	data->name = NULL;

	while ((opt = getopt(argc, argv, "dn:")) != EOF) {
		switch (opt) {
		case 'd':
			data->flag_detached = 1;
			break;
		case 'n':
			data->name = xstrdup(optarg);
			break;
		default:
			goto usage;
		}
	}	
	argc -= optind;
	argv += optind;
	if (argc != 0)
		goto usage;

	return (0);

usage:
	usage(cause, "%s", cmd_new_session_usage());
	
	cmd_new_session_free(data);
	return (-1);
}

const char *
cmd_new_session_usage(void)
{
	return ("new-session [-d] [-n session name]");
}

void
cmd_new_session_exec(void *ptr, struct cmd_ctx *ctx)
{
	struct cmd_new_session_data	*data = ptr, std = { NULL, 0 };
	struct client			*c = ctx->client;
	u_int				 sy;
	
	if (data == NULL)
		data = &std;

	if (ctx->flags & CMD_KEY)
		return;

	if (!data->flag_detached && !(c->flags & CLIENT_TERMINAL)) {
		ctx->error(ctx, "not a terminal");
		return;
	}

	if (data->name != NULL && session_find(data->name) != NULL) {
		ctx->error(ctx, "duplicate session: %s", data->name);
		return;
	}

	sy = c->sy;
	if (sy < status_lines)
		sy = status_lines + 1;
	sy -= status_lines;
	
	c->session = session_create(data->name, default_command, c->sx, sy);
	if (c->session == NULL)
		fatalx("session_create failed");

	if (data->flag_detached)
		server_write_client(c, MSG_EXIT, NULL, 0);
	else {
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
}

void
cmd_new_session_recv(void **ptr, struct buffer *b)
{
	struct cmd_new_session_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	buffer_read(b, data, sizeof *data);
	data->name = cmd_recv_string(b);
}

void
cmd_new_session_free(void *ptr)
{
	struct cmd_new_session_data	*data = ptr;

	if (data->name != NULL)
		xfree(data->name);
	xfree(data);
}
