/* $Id: cmd-switch-client.c,v 1.23 2010-12-22 15:31:00 tcunha Exp $ */

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
 * Switch client to a different session.
 */

void	cmd_switch_client_init(struct cmd *, int);
int	cmd_switch_client_parse(struct cmd *, int, char **, char **);
int	cmd_switch_client_exec(struct cmd *, struct cmd_ctx *);
void	cmd_switch_client_free(struct cmd *);
size_t	cmd_switch_client_print(struct cmd *, char *, size_t);

struct cmd_switch_client_data {
	char	*name;
	char	*target;
	int      flag_last;
	int	 flag_next;
	int	 flag_previous;
};

const struct cmd_entry cmd_switch_client_entry = {
	"switch-client", "switchc",
	"[-lnp] [-c target-client] [-t target-session]",
	0, "",
	cmd_switch_client_init,
	cmd_switch_client_parse,
	cmd_switch_client_exec,
	cmd_switch_client_free,
	cmd_switch_client_print
};

void
cmd_switch_client_init(struct cmd *self, int key)
{
	struct cmd_switch_client_data	*data;

	self->data = data = xmalloc(sizeof *data);
	data->name = NULL;
	data->target = NULL;
	data->flag_last = 0;
	data->flag_next = 0;
	data->flag_previous = 0;

	switch (key) {
	case '(':
		data->flag_previous = 1;
		break;
	case ')':
		data->flag_next = 1;
		break;
	case 'L':
		data->flag_last = 1;
		break;
	}
}

int
cmd_switch_client_parse(struct cmd *self, int argc, char **argv, char **cause)
{
	struct cmd_switch_client_data	*data;
	int				 opt;

	self->entry->init(self, KEYC_NONE);
	data = self->data;

	while ((opt = getopt(argc, argv, "c:lnpt:")) != -1) {
		switch (opt) {
		case 'c':
			if (data->name == NULL)
				data->name = xstrdup(optarg);
			break;
		case 'l':
			if (data->flag_next || data->flag_previous ||
			    data->target != NULL)
				goto usage;
			data->flag_last = 1;
			break;
		case 'n':
			if (data->flag_previous || data->flag_last ||
			    data->target != NULL)
				goto usage;
			data->flag_next = 1;
			break;
		case 'p':
			if (data->flag_next || data->flag_last ||
			    data->target != NULL)
				goto usage;
			data->flag_next = 1;
			break;
		case 't':
			if (data->flag_next || data->flag_previous)
				goto usage;
			if (data->target == NULL)
				data->target = xstrdup(optarg);
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
	xasprintf(cause, "usage: %s %s", self->entry->name, self->entry->usage);

	self->entry->free(self);
	return (-1);
}

int
cmd_switch_client_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_switch_client_data	*data = self->data;
	struct client			*c;
	struct session			*s;

	if (data == NULL)
		return (0);

	if ((c = cmd_find_client(ctx, data->name)) == NULL)
		return (-1);

	s = NULL;
	if (data->flag_next) {
		if ((s = session_next_session(c->session)) == NULL) {
			ctx->error(ctx, "can't find next session");
			return (-1);
		}
	} else if (data->flag_previous) {
		if ((s = session_previous_session(c->session)) == NULL) {
			ctx->error(ctx, "can't find previous session");
			return (-1);
		}
	} else if (data->flag_last) {
		if (c->last_session != NULL && session_alive(c->last_session))
			s = c->last_session;
		if (s == NULL) {
			ctx->error(ctx, "can't find last session");
			return (-1);
		}
	} else
		s = cmd_find_session(ctx, data->target);
	if (s == NULL)
		return (-1);

	if (c->session != NULL)
		c->last_session = c->session;
	c->session = s;

	recalculate_sizes();
	server_check_unattached();
	server_redraw_client(c);

	return (0);
}

void
cmd_switch_client_free(struct cmd *self)
{
	struct cmd_switch_client_data	*data = self->data;

	if (data->name != NULL)
		xfree(data->name);
	if (data->target != NULL)
		xfree(data->target);
	xfree(data);
}

size_t
cmd_switch_client_print(struct cmd *self, char *buf, size_t len)
{
	struct cmd_switch_client_data	*data = self->data;
	size_t				 off = 0;

	off += xsnprintf(buf, len, "%s", self->entry->name);
	if (data == NULL)
		return (off);
	if (off < len && data->flag_last)
		off += xsnprintf(buf + off, len - off, "%s", " -l");
	if (off < len && data->flag_next)
		off += xsnprintf(buf + off, len - off, "%s", " -n");
	if (off < len && data->flag_previous)
		off += xsnprintf(buf + off, len - off, "%s", " -p");
	if (off < len && data->name != NULL)
		off += cmd_prarg(buf + off, len - off, " -c ", data->name);
	if (off < len && data->target != NULL)
		off += cmd_prarg(buf + off, len - off, " -t ", data->target);
	return (off);
}
