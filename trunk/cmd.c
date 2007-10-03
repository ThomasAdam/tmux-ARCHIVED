/* $Id: cmd.c,v 1.1 2007-10-03 10:18:32 nicm Exp $ */

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

int	cmd_prefix = META;

void	cmd_fn_create(struct client *, int);
void	cmd_fn_detach(struct client *, int);
void	cmd_fn_last(struct client *, int);
void	cmd_fn_meta(struct client *, int);
void	cmd_fn_next(struct client *, int);
void	cmd_fn_previous(struct client *, int);
void	cmd_fn_refresh(struct client *, int);
void	cmd_fn_select(struct client *, int);
void	cmd_fn_windowinfo(struct client *, int);

struct cmd {
	int	key;
	void	(*fn)(struct client *, int);
	int	arg;
};

struct cmd cmd_table[] = {
	{ '0', cmd_fn_select, 0 },
	{ '1', cmd_fn_select, 1 },
	{ '2', cmd_fn_select, 2 },
	{ '3', cmd_fn_select, 3 },
	{ '4', cmd_fn_select, 4 },
	{ '5', cmd_fn_select, 5 },
	{ '6', cmd_fn_select, 6 },
	{ '7', cmd_fn_select, 7 },
	{ '8', cmd_fn_select, 8 },
	{ '9', cmd_fn_select, 9 },
	{ 'C', cmd_fn_create, 0 },
	{ 'c', cmd_fn_create, 0 },
	{ 'D', cmd_fn_detach, 0 },
	{ 'd', cmd_fn_detach, 0 },
	{ 'N', cmd_fn_next, 0 },
	{ 'n', cmd_fn_next, 0 },
	{ 'P', cmd_fn_previous, 0 },
	{ 'p', cmd_fn_previous, 0 },
	{ 'R', cmd_fn_refresh, 0 },
	{ 'r', cmd_fn_refresh, 0 },
	{ 'L', cmd_fn_last, 0 },
	{ 'l', cmd_fn_last, 0 },
	{ 'I', cmd_fn_windowinfo, 0 },
	{ 'i', cmd_fn_windowinfo, 0 },
	{ META, cmd_fn_meta, 0 },
};
#define NCMD (sizeof cmd_table / sizeof cmd_table[0])

void
cmd_dispatch(struct client *c, int key)
{
	struct cmd	*cmd;
	u_int		 i;

	for (i = 0; i < NCMD; i++) {
		cmd = cmd_table + i;
		if (cmd->key == key)
			cmd->fn(c, cmd->arg);
	}
}

void
cmd_fn_create(struct client *c, unused int arg)
{
	const char	*shell;
	char		*cmd;

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/ksh";
	xasprintf(&cmd, "%s -l", shell);
	if (session_new(c->session, cmd, c->sx, c->sy) != 0)
		fatalx("session_new failed");
	xfree(cmd);

	server_draw_client(c, 0, c->sy - 1);
}

void
cmd_fn_detach(struct client *c, unused int arg)
{
	server_write_client(c, MSG_DETACH, NULL, 0);
}

void
cmd_fn_last(struct client *c, unused int arg)
{
	if (session_last(c->session) == 0)
		server_window_changed(c);
	else
		server_write_message(c, "No last window"); 
}

void
cmd_fn_meta(struct client *c, unused int arg)
{
	window_key(c->session->window, cmd_prefix);
}

void
cmd_fn_next(struct client *c, unused int arg)
{
	if (session_next(c->session) == 0)
		server_window_changed(c);
	else
		server_write_message(c, "No next window"); 
}

void
cmd_fn_previous(struct client *c, unused int arg)
{
	if (session_previous(c->session) == 0)
		server_window_changed(c);
	else
		server_write_message(c, "No previous window"); 
}

void
cmd_fn_refresh(struct client *c, unused int arg)
{
	server_draw_client(c, 0, c->sy - 1);
}

void
cmd_fn_select(struct client *c, int arg)
{
	if (session_select(c->session, arg) == 0)
		server_window_changed(c);
	else
		server_write_message(c, "Window %u not present", arg); 
}

void
cmd_fn_windowinfo(struct client *c, unused int arg)
{
	struct window	*w;
	char 		*buf;
	size_t		 len;
	u_int		 i;

	len = c->sx + 1;
	buf = xmalloc(len);

	w = c->session->window;
	window_index(&c->session->windows, w, &i);
	xsnprintf(buf, len, "%u:%s \"%s\" (size %u,%u) (cursor %u,%u) "
	    "(region %u,%u)", i, w->name, w->screen.title, w->screen.sx,
	    w->screen.sy, w->screen.cx, w->screen.cy, w->screen.ry_upper,
	    w->screen.ry_lower);

	server_write_message(c, "%s", buf);
	xfree(buf);
}
