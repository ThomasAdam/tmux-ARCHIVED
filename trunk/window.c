/* $Id: window.c,v 1.52 2008-12-08 16:19:51 nicm Exp $ */

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
#include <sys/ioctl.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef NO_PATHS_H
#include <paths.h>
#endif

#ifdef USE_LIBUTIL_H
#include <libutil.h>
#else
#ifdef USE_PTY_H
#include <pty.h>
#else
#ifndef NO_FORKPTY
#include <util.h>
#endif
#endif
#endif

#include "tmux.h"

/*
 * Each window is attached to a pty. This file contains code to handle them.
 *
 * A window has two buffers attached, these are filled and emptied by the main
 * server poll loop. Output data is received from pty's in screen format,
 * translated and returned as a series of escape sequences and strings via
 * input_parse (in input.c). Input data is received as key codes and written
 * directly via input_key.
 *
 * Each window also has a "virtual" screen (screen.c) which contains the
 * current state and is redisplayed when the window is reattached to a client.
 *
 * Windows are stored directly on a global array and wrapped in any number of
 * winlink structs to be linked onto local session RB trees. A reference count
 * is maintained and a window removed from the global list and destroyed when
 * it reaches zero.
 */

/* Global window list. */
struct windows windows;

RB_GENERATE(winlinks, winlink, entry, winlink_cmp);

int
winlink_cmp(struct winlink *wl1, struct winlink *wl2)
{
	return (wl1->idx - wl2->idx);
}

struct winlink *
winlink_find_by_index(struct winlinks *wwl, int idx)
{
	struct winlink	wl;

	if (idx < 0)
		fatalx("bad index");

	wl.idx = idx;
	return (RB_FIND(winlinks, wwl, &wl));
}

int
winlink_next_index(struct winlinks *wwl)
{
	u_int	i;

	for (i = 0; i < INT_MAX; i++) {
		if (winlink_find_by_index(wwl, i) == NULL)
			return (i);
	}

	fatalx("no free indexes");
}

struct winlink *
winlink_add(struct winlinks *wwl, struct window *w, int idx)
{
	struct winlink	*wl;

	if (idx == -1)
		idx = winlink_next_index(wwl);
	else if (winlink_find_by_index(wwl, idx) != NULL)
		return (NULL);

	if (idx < 0)
		fatalx("bad index");

	wl = xcalloc(1, sizeof *wl);
	wl->idx = idx;
	wl->window = w;
	RB_INSERT(winlinks, wwl, wl);

	w->references++;

	return (wl);
}

void
winlink_remove(struct winlinks *wwl, struct winlink *wl)
{
	struct window	*w = wl->window;

	RB_REMOVE(winlinks, wwl, wl);
	xfree(wl);

	if (w->references == 0)
		fatal("bad reference count");
	w->references--;
	if (w->references == 0)
		window_destroy(w);
}

struct winlink *
winlink_next(unused struct winlinks *wwl, struct winlink *wl)
{
	return (RB_NEXT(winlinks, wwl, wl));
}

struct winlink *
winlink_previous(unused struct winlinks *wwl, struct winlink *wl)
{
#ifdef RB_PREV
	return (RB_PREV(winlinks, wwl, wl));
#else
	struct winlink	*wk;
	int		 idx = wl->idx;

	wk = NULL;
	wl = RB_MIN(winlinks, wwl);
	while (wl != NULL && wl->idx < idx) {
		wk = wl;
		wl = RB_NEXT(winlinks, wwl, wl);
	}
	if (wl == NULL)
		return (NULL);
	return (wk);
#endif
}

void
winlink_stack_push(struct winlink_stack *stack, struct winlink *wl)
{
	if (wl == NULL)
		return;

	winlink_stack_remove(stack, wl);
	SLIST_INSERT_HEAD(stack, wl, sentry);
}

void
winlink_stack_remove(struct winlink_stack *stack, struct winlink *wl)
{
	struct winlink	*wl2;

	if (wl == NULL)
		return;

	SLIST_FOREACH(wl2, stack, sentry) {
		if (wl2 == wl) {
			SLIST_REMOVE(stack, wl, winlink, sentry);
			return;
		}
	}
}

struct window *
window_create(const char *name,
    const char *cmd, const char **envp, u_int sx, u_int sy, u_int hlimit)
{
	struct window	*w;
	char		*ptr, *copy;

	w = xmalloc(sizeof *w);
	w->cmd = NULL;

	w->fd = -1;
	w->in = buffer_create(BUFSIZ);
	w->out = buffer_create(BUFSIZ);

	w->mode = NULL;
	w->flags = 0;

	screen_init(&w->base, sx, sy, hlimit);
	w->screen = &w->base;

	input_init(w);
	options_init(&w->options, &global_window_options);

	if (name == NULL) {
		/* XXX */
		if (strncmp(cmd, "exec ", (sizeof "exec ") - 1) == 0)
			copy = xstrdup(cmd + (sizeof "exec ") - 1);
		else
			copy = xstrdup(cmd);
		if ((ptr = strchr(copy, ' ')) != NULL) {
			if (ptr != copy && ptr[-1] != '\\')
				*ptr = '\0';
			else {
				while ((ptr = strchr(ptr + 1, ' ')) != NULL) {
					if (ptr[-1] != '\\') {
						*ptr = '\0';
						break;
					}
				}
			}
		}
		w->name = xstrdup(xbasename(copy));
		xfree(copy);
	} else
		w->name = xstrdup(name);

	ARRAY_ADD(&windows, w);
	w->references = 0;

	if (window_spawn(w, cmd, envp) != 0) {
		window_destroy(w);
		return (NULL);
	}
	return (w);
}

int
window_spawn(struct window *w, const char *cmd, const char **envp)
{
	struct winsize	 ws;
	int		 mode;
	const char     **envq;

	if (w->fd != -1)
		close(w->fd);
	if (cmd != NULL) {
		if (w->cmd != NULL)
			xfree(w->cmd);
		w->cmd = xstrdup(cmd);
	}

	memset(&ws, 0, sizeof ws);
	ws.ws_col = screen_size_x(&w->base);
	ws.ws_row = screen_size_y(&w->base);

 	switch (forkpty(&w->fd, NULL, NULL, &ws)) {
	case -1:
		return (1);
	case 0:
		for (envq = envp; *envq != NULL; envq++) {
			if (putenv(*envq) != 0)
				fatal("putenv failed");
		}
		sigreset();
		log_debug("new child: cmd=%s; pid=%ld", w->cmd, (long) getpid());
		log_close();

		execl(_PATH_BSHELL, "sh", "-c", w->cmd, (char *) NULL);
		fatal("execl failed");
	}

	if ((mode = fcntl(w->fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(w->fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");
	if (fcntl(w->fd, F_SETFD, FD_CLOEXEC) == -1)
		fatal("fcntl failed");

	return (0);
}

void
window_destroy(struct window *w)
{
	u_int	i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		if (ARRAY_ITEM(&windows, i) == w)
			break;
	}
	ARRAY_REMOVE(&windows, i);

	if (w->fd != -1)
		close(w->fd);

	input_free(w);

	window_reset_mode(w);
	options_free(&w->options);
	screen_free(&w->base);

	buffer_destroy(w->in);
	buffer_destroy(w->out);

	if (w->cmd != NULL)
		xfree(w->cmd);
	xfree(w->name);
	xfree(w);
}

int
window_resize(struct window *w, u_int sx, u_int sy)
{
	struct winsize	ws;

	if (sx == screen_size_x(&w->base) && sy == screen_size_y(&w->base))
		return (-1);

	memset(&ws, 0, sizeof ws);
	ws.ws_col = sx;
	ws.ws_row = sy;

	screen_resize(&w->base, sx, sy);
	if (w->mode != NULL)
		w->mode->resize(w, sx, sy);

	if (w->fd != -1 && ioctl(w->fd, TIOCSWINSZ, &ws) == -1)
		fatal("ioctl failed");
	return (0);
}

int
window_set_mode(struct window *w, const struct window_mode *mode)
{
	struct screen	*s;

	if (w->mode != NULL || w->mode == mode)
		return (1);

	w->mode = mode;

	if ((s = w->mode->init(w)) != NULL)
		w->screen = s;
	server_redraw_window(w);
	return (0);
}

void
window_reset_mode(struct window *w)
{
	if (w->mode == NULL)
		return;

	w->mode->free(w);
	w->mode = NULL;

	w->screen = &w->base;
	server_redraw_window(w);
}

void
window_parse(struct window *w)
{
	input_parse(w);
}

void
window_key(struct window *w, struct client *c, int key)
{
	if (w->mode != NULL)
		w->mode->key(w, c, key);
	else
		input_key(w, key);
}
