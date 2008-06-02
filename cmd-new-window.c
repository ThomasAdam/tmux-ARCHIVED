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
#include <stdlib.h>

#include "tmux.h"

/*
 * Create a new window.
 */

int	cmd_new_window_parse(struct cmd *, void **, int, char **, char **);
void	cmd_new_window_exec(void *, struct cmd_ctx *);
void	cmd_new_window_send(void *, struct buffer *);
void	cmd_new_window_recv(void **, struct buffer *);
void	cmd_new_window_free(void *);

struct cmd_new_window_data {
	char	*sname;
	char	*name;
	char	*cmd;
	int	 idx;
	int	 flag_detached;
};

const struct cmd_entry cmd_new_window_entry = {
	"new-window", "neww",
	"[-d] [-i index] [-n name] [-s session-name] [command]",
	0,
	cmd_new_window_parse,
	cmd_new_window_exec,
	cmd_new_window_send,
	cmd_new_window_recv,
	cmd_new_window_free
};

int
cmd_new_window_parse(
    struct cmd *self, void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_new_window_data	*data;
	const char			*errstr;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->sname = NULL;
	data->idx = -1;
	data->flag_detached = 0;
	data->name = NULL;
	data->cmd = NULL;

	while ((opt = getopt(argc, argv, "di:n:s:")) != EOF) {
		switch (opt) {
		case 'i':
			data->idx = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL) {
				xasprintf(cause, "index %s", errstr);
				goto error;
			}
			break;
		case 'n':
			data->name = xstrdup(optarg);
			break;
		case 'd':
			data->flag_detached = 1;
			break;
		case 's':
			data->sname = xstrdup(optarg);
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

error:
	cmd_new_window_free(data);
	return (-1);
}

void
cmd_new_window_exec(void *ptr, struct cmd_ctx *ctx)
{
	struct cmd_new_window_data	*data = ptr;
	struct cmd_new_window_data	 std = { NULL, NULL, NULL, -1, 0 };
	struct session			*s;
	struct winlink			*wl;
	char				*cmd;

	if (data == NULL)
		data = &std;

	cmd = data->cmd;
	if (cmd == NULL)
		cmd = default_command;

	if ((s = cmd_find_session(ctx, data->sname)) == NULL)
		return;

	if (data->idx < 0)
		data->idx = -1;
	wl = session_new(s, data->name, cmd, data->idx);
	if (wl == NULL) {
		ctx->error(ctx, "command failed: %s", cmd);
		return;
	}
	if (!data->flag_detached) {
		session_select(s, wl->idx);
		server_redraw_session(s);
	} else
		server_status_session(s);

	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}

void
cmd_new_window_send(void *ptr, struct buffer *b)
{
	struct cmd_new_window_data	*data = ptr;

	buffer_write(b, data, sizeof *data);
	cmd_send_string(b, data->sname);
	cmd_send_string(b, data->name);
	cmd_send_string(b, data->cmd);
}

void
cmd_new_window_recv(void **ptr, struct buffer *b)
{
	struct cmd_new_window_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	buffer_read(b, data, sizeof *data);
	data->sname = cmd_recv_string(b);
	data->name = cmd_recv_string(b);
	data->cmd = cmd_recv_string(b);
}

void
cmd_new_window_free(void *ptr)
{
	struct cmd_new_window_data	*data = ptr;

	if (data->sname != NULL)
		xfree(data->sname);
	if (data->name != NULL)
		xfree(data->name);
	if (data->cmd != NULL)
		xfree(data->cmd);
	xfree(data);
}
