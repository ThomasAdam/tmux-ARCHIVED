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
 * Link a window into another session.
 */

int	cmd_link_window_parse(void **, int, char **, char **);
void	cmd_link_window_exec(void *, struct cmd_ctx *);
void	cmd_link_window_send(void *, struct buffer *);
void	cmd_link_window_recv(void **, struct buffer *);
void	cmd_link_window_free(void *);

struct cmd_link_window_data {
	int	 flag_detached;
	int	 flag_kill;
	int	 dstidx;
	int	 srcidx;
	char	*srcname;
};

const struct cmd_entry cmd_link_window_entry = {
	"link-window", "linkw", "[-dk] [-i index] name index",
	CMD_NOCLIENT,
	cmd_link_window_parse,
	cmd_link_window_exec,
	cmd_link_window_send,
	cmd_link_window_recv,
	cmd_link_window_free
};

int
cmd_link_window_parse(void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_link_window_data	*data;
	const char			*errstr;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->flag_detached = 0;
	data->flag_kill = 0;
	data->dstidx = -1;
	data->srcidx = -1;
	data->srcname = NULL;

	while ((opt = getopt(argc, argv, "dki:")) != EOF) {
		switch (opt) {
		case 'i':
			data->dstidx = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL) {
				xasprintf(cause, "index %s", errstr);
				goto error;
			}
			break;
		case 'd':
			data->flag_detached = 1;
			break;
		case 'k':
			data->flag_kill = 1;
			break;
		default:
			goto usage;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		goto usage;

	data->srcname = xstrdup(argv[0]);
	data->srcidx = strtonum(argv[1], 0, INT_MAX, &errstr);
	if (errstr != NULL) {
		xasprintf(cause, "index %s", errstr);
		goto error;
	}

	return (0);

usage:
	usage(cause, "%s %s",
	    cmd_link_window_entry.name, cmd_link_window_entry.usage);

error:
	cmd_link_window_free(data);
	return (-1);
}

void
cmd_link_window_exec(void *ptr, struct cmd_ctx *ctx)
{
	struct cmd_link_window_data	*data = ptr;
	struct session			*dst = ctx->session, *src;
	struct winlink			*wl, *wl2;

	if (data == NULL)
		return;

	if ((src = session_find(data->srcname)) == NULL) {
		ctx->error(ctx, "session not found: %s", data->srcname);
		return;
	}

	if (data->srcidx < 0)
		data->srcidx = -1;
	if (data->srcidx == -1)
		wl = src->curw;
	else {
		wl = winlink_find_by_index(&src->windows, data->srcidx);
		if (wl == NULL) {
			ctx->error(ctx, "no window %d", data->srcidx);
			return;
		}
	}

	if (data->dstidx < 0)
		data->dstidx = -1;
	if (data->flag_kill && data->dstidx != -1) {
		wl2 = winlink_find_by_index(&dst->windows, data->dstidx);
		if (wl2 == NULL) {
			ctx->error(ctx, "no window %d", data->dstidx);
			return;
		}

		/*
		 * Can't use session_detach as it will destroy session if this
		 * makes it empty.
		 */
		session_cancelbell(dst, wl2);
		winlink_remove(&dst->windows, wl2);

		/* Force select/redraw if current. */
		if (wl2 == dst->curw) {
			data->flag_detached = 0;
			dst->curw = NULL;
		}
		if (wl2 == dst->lastw)
			dst->lastw = NULL;

		/*
		 * Can't error out after this or there could be an empty
		 * session!
		 */
	}

	wl = session_attach(dst, wl->window, data->dstidx);
	if (wl == NULL) {
		ctx->error(ctx, "index in use: %d", data->dstidx);
		return;
	}

	if (!data->flag_detached) {
		session_select(dst, wl->idx);
		server_redraw_session(dst);
	} else
		server_status_session(dst);

	if (ctx->cmdclient != NULL)
		server_write_client(ctx->cmdclient, MSG_EXIT, NULL, 0);
}

void
cmd_link_window_send(void *ptr, struct buffer *b)
{
	struct cmd_link_window_data	*data = ptr;

	buffer_write(b, data, sizeof *data);
	cmd_send_string(b, data->srcname);
}

void
cmd_link_window_recv(void **ptr, struct buffer *b)
{
	struct cmd_link_window_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	buffer_read(b, data, sizeof *data);
	data->srcname = cmd_recv_string(b);
}

void
cmd_link_window_free(void *ptr)
{
	struct cmd_link_window_data	*data = ptr;

	if (data->srcname != NULL)
		xfree(data->srcname);
	xfree(data);
}
