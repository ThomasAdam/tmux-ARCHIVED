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
 * Rename window by index.
 */

int	cmd_rename_window_parse(struct cmd *, void **, int, char **, char **);
void	cmd_rename_window_exec(void *, struct cmd_ctx *);
void	cmd_rename_window_send(void *, struct buffer *);
void	cmd_rename_window_recv(void **, struct buffer *);
void	cmd_rename_window_free(void *);

struct cmd_rename_window_data {
	char	*cname;
	char	*sname;
	int	 idx;
	char	*newname;
};

const struct cmd_entry cmd_rename_window_entry = {
	"rename-window", "renamew",
	"[-c client-tty|-s session-name] [-i index] new-name",
	0,
	cmd_rename_window_parse,
	cmd_rename_window_exec,
	cmd_rename_window_send,
	cmd_rename_window_recv,
	cmd_rename_window_free,
	NULL
};

int
cmd_rename_window_parse(
    struct cmd *self, void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_rename_window_data	*data;
	const char			*errstr;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->cname = NULL;
	data->sname = NULL;
	data->idx = -1;
	data->newname = NULL;

	while ((opt = getopt(argc, argv, "c:i:s:")) != EOF) {
		switch (opt) {
		case 'c':
			if (data->sname != NULL)
				goto usage;
			if (data->cname == NULL)
				data->cname = xstrdup(optarg);
			break;
		case 'i':
			data->idx = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL) {
				xasprintf(cause, "index %s", errstr);
				goto error;
			}
			break;
		case 's':
			if (data->cname != NULL)
				goto usage;
			if (data->sname == NULL)
				data->sname = xstrdup(optarg);
			break;
		default:
			goto usage;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		goto usage;

	data->newname = xstrdup(argv[0]);

	return (0);

usage:
	xasprintf(cause, "usage: %s %s", self->entry->name, self->entry->usage);

error:
	cmd_rename_window_free(data);
	return (-1);
}

void
cmd_rename_window_exec(void *ptr, struct cmd_ctx *ctx)
{
	struct cmd_rename_window_data	*data = ptr;
	struct session			*s;
	struct winlink			*wl;

	if (data == NULL)
		return;

	wl = cmd_find_window(ctx, data->cname, data->sname, data->idx, &s);
	if (wl == NULL)
		return;

	xfree(wl->window->name);
	wl->window->name = xstrdup(data->newname);

	server_status_session(s);

	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}

void
cmd_rename_window_send(void *ptr, struct buffer *b)
{
	struct cmd_rename_window_data	*data = ptr;

	buffer_write(b, data, sizeof *data);
	cmd_send_string(b, data->cname);
	cmd_send_string(b, data->sname);
	cmd_send_string(b, data->newname);
}

void
cmd_rename_window_recv(void **ptr, struct buffer *b)
{
	struct cmd_rename_window_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	buffer_read(b, data, sizeof *data);
	data->cname = cmd_recv_string(b);
	data->sname = cmd_recv_string(b);
	data->newname = cmd_recv_string(b);
}

void
cmd_rename_window_free(void *ptr)
{
	struct cmd_rename_window_data	*data = ptr;

	if (data->cname != NULL)
		xfree(data->cname);
	if (data->sname != NULL)
		xfree(data->sname);
	if (data->newname != NULL)
		xfree(data->newname);
	xfree(data);
}
