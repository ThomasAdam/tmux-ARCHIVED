/* $Id: window.c,v 1.59 2009/01/18 18:31:45 nicm Exp $ */

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
 * Each window is attached to one or two panes, each of which is a pty. This
 * file contains code to handle them.
 *
 * A pane has two buffers attached, these are filled and emptied by the main
 * server poll loop. Output data is received from pty's in screen format,
 * translated and returned as a series of escape sequences and strings via
 * input_parse (in input.c). Input data is received as key codes and written
 * directly via input_key.
 *
 * Each pane also has a "virtual" screen (screen.c) which contains the current
 * state and is redisplayed when the window is reattached to a client.
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

u_int
winlink_count(struct winlinks *wwl)
{
	struct winlink	*wl;
	u_int		 n;

	n = 0;
	RB_FOREACH(wl, winlinks, wwl)
		n++;

	return (n);
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
	return (RB_PREV(winlinks, wwl, wl));
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

int
window_index(struct window *s, u_int *i)
{
	for (*i = 0; *i < ARRAY_LENGTH(&windows); (*i)++) {
		if (s == ARRAY_ITEM(&windows, *i))
			return (0);
	}
	return (-1);
}

struct window *
window_create(const char *name, const char *cmd,
    const char *cwd, const char **envp, u_int sx, u_int sy, u_int hlimit)
{
	struct window	*w;
	u_int		 i;
	char		*ptr, *copy;

	w = xmalloc(sizeof *w);
	w->flags = 0;

	TAILQ_INIT(&w->panes);
	w->active = NULL;

	w->sx = sx;
	w->sy = sy;

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

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		if (ARRAY_ITEM(&windows, i) == NULL) {
			ARRAY_SET(&windows, i, w);
			break;
		}
	}
	if (i == ARRAY_LENGTH(&windows))
		ARRAY_ADD(&windows, w);
	w->references = 0;

	if (window_add_pane(w, cmd, cwd, envp, hlimit) == NULL) {
		window_destroy(w);
		return (NULL);
	}
	w->active = TAILQ_FIRST(&w->panes);
	return (w);
}

void
window_destroy(struct window *w)
{
	u_int	i;

	if (window_index(w, &i) != 0)
		fatalx("index not found");
	ARRAY_SET(&windows, i, NULL);
	while (!ARRAY_EMPTY(&windows) && ARRAY_LAST(&windows) == NULL)
		ARRAY_TRUNC(&windows, 1);

	options_free(&w->options);

	window_destroy_panes(w);
	
	xfree(w->name);
	xfree(w);
}

int
window_resize(struct window *w, u_int sx, u_int sy)
{
	w->sx = sx;
	w->sy = sy;

	window_fit_panes(w);
	return (0);
}

void
window_fit_panes(struct window *w)
{
	struct window_pane	*wp;
	u_int			 npanes, canfit, total;
	int			 left;
	
	if (TAILQ_EMPTY(&w->panes))
		return;

	/* Clear hidden flags. */
	TAILQ_FOREACH(wp, &w->panes, entry)
	    	wp->flags &= ~PANE_HIDDEN;

	/* Check the new size. */
	npanes = window_count_panes(w);
	if (w->sy <= PANE_MINIMUM * npanes) {
		/* How many can we fit? */
		canfit = w->sy / PANE_MINIMUM;
		if (canfit == 0) {
			/* None. Just use this size for the first. */
			TAILQ_FOREACH(wp, &w->panes, entry) {
				if (wp == TAILQ_FIRST(&w->panes))
					wp->sy = w->sy;
				else
					wp->flags |= PANE_HIDDEN;
			}
		} else {
			/* >=1, set minimum for them all. */
			TAILQ_FOREACH(wp, &w->panes, entry) {
				if (canfit-- > 0)
					wp->sy = PANE_MINIMUM - 1;
				else
					wp->flags |= PANE_HIDDEN;
			}
			/* And increase the first by the rest. */
			TAILQ_FIRST(&w->panes)->sy += 1 + w->sy % PANE_MINIMUM;
		}
	} else {
		/* In theory they will all fit. Find the current total. */
		total = 0;
		TAILQ_FOREACH(wp, &w->panes, entry)
			total += wp->sy;
		total += npanes - 1;

		/* Growing or shrinking? */
		left = w->sy - total;
		if (left > 0) {
			/* Growing. Expand evenly. */
			while (left > 0) {
				TAILQ_FOREACH(wp, &w->panes, entry) {
					wp->sy++;
					if (--left == 0)
						break;
				}
			}
		} else {
			/* Shrinking. Reduce evenly down to minimum. */
			while (left < 0) {
				TAILQ_FOREACH(wp, &w->panes, entry) {
					if (wp->sy <= PANE_MINIMUM - 1)
						continue;
					wp->sy--;
					if (++left == 0)
						break;
				}
			}
		}
	}
			
	/* Now do the resize. */
	TAILQ_FOREACH(wp, &w->panes, entry) {
		wp->sy--;
	    	window_pane_resize(wp, w->sx, wp->sy + 1);
	}

	/* Fill in the offsets. */
	window_update_panes(w);

	/* Switch the active window if necessary. */
	window_set_active_pane(w, w->active);
}

void
window_update_panes(struct window *w)
{
	struct window_pane     *wp;
	u_int			yoff;

	yoff = 0;
	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (wp->flags & PANE_HIDDEN)
			continue;
		wp->yoff = yoff;
		yoff += wp->sy + 1;
	}
}

void
window_set_active_pane(struct window *w, struct window_pane *wp)
{
	w->active = wp;
	while (w->active->flags & PANE_HIDDEN)
		w->active = TAILQ_PREV(w->active, window_panes, entry);
}

struct window_pane *
window_add_pane(struct window *w, 
    const char *cmd, const char *cwd, const char **envp, u_int hlimit)
{
	struct window_pane	*wp;
	u_int			 wanty;

	if (TAILQ_EMPTY(&w->panes))
		wanty = w->sy;
	else {
		if (w->active->sy < PANE_MINIMUM * 2)
			return (NULL);
		wanty = (w->active->sy / 2 + w->active->sy % 2) - 1;
		window_pane_resize(w->active, w->sx, w->active->sy / 2);
	}

	wp = window_pane_create(w, w->sx, wanty, hlimit);
	if (TAILQ_EMPTY(&w->panes))
		TAILQ_INSERT_HEAD(&w->panes, wp, entry);
	else
		TAILQ_INSERT_AFTER(&w->panes, w->active, wp, entry);
	window_update_panes(w);
	if (window_pane_spawn(wp, cmd, cwd, envp) != 0) {
		window_remove_pane(w, wp);
		return (NULL);
	}
	return (wp);
}

void
window_remove_pane(struct window *w, struct window_pane *wp)
{
	w->active = TAILQ_PREV(wp, window_panes, entry);
	if (w->active == NULL)
		w->active = TAILQ_NEXT(wp, entry);

	TAILQ_REMOVE(&w->panes, wp, entry);
	window_pane_destroy(wp);

	window_fit_panes(w);
}

u_int
window_index_of_pane(struct window *w, struct window_pane *find)
{
	struct window_pane	*wp;
	u_int			 n;

	n = 0;
	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (wp == find)
			return (n);
		n++;
	}
	fatalx("unknown pane");
}

struct window_pane *
window_pane_at_index(struct window *w, u_int idx)
{
	struct window_pane	*wp;
	u_int			 n;

	n = 0;
	TAILQ_FOREACH(wp, &w->panes, entry) {
		if (n == idx)
			return (wp);
		n++;
	}
	return (NULL);
}

u_int
window_count_panes(struct window *w)
{
	struct window_pane	*wp;
	u_int			 n;

	n = 0;
	TAILQ_FOREACH(wp, &w->panes, entry)
		n++;
	return (n);
}

void
window_destroy_panes(struct window *w)
{
	struct window_pane	*wp;

	while (!TAILQ_EMPTY(&w->panes)) {
		wp = TAILQ_FIRST(&w->panes);
		TAILQ_REMOVE(&w->panes, wp, entry);
		window_pane_destroy(wp);
	}
}

struct window_pane *
window_pane_create(struct window *w, u_int sx, u_int sy, u_int hlimit)
{
	struct window_pane	*wp;

	wp = xcalloc(1, sizeof *wp);
	wp->window = w;

	wp->cmd = NULL;
	wp->cwd = NULL;

	wp->fd = -1;
	wp->in = buffer_create(BUFSIZ);
	wp->out = buffer_create(BUFSIZ);

	wp->mode = NULL;

	wp->sx = sx;
	wp->sy = sy;

 	wp->yoff = 0;

	screen_init(&wp->base, sx, sy, hlimit);
	wp->screen = &wp->base;

	input_init(wp);

	return (wp);
}

void
window_pane_destroy(struct window_pane *wp)
{
	if (wp->fd != -1)
		close(wp->fd);

	input_free(wp);

	window_pane_reset_mode(wp);
	screen_free(&wp->base);

	buffer_destroy(wp->in);
	buffer_destroy(wp->out);

	if (wp->cwd != NULL)
		xfree(wp->cwd);
	if (wp->cmd != NULL)
		xfree(wp->cmd);
	xfree(wp);
}

int
window_pane_spawn(struct window_pane *wp,
    const char *cmd, const char *cwd, const char **envp)
{
	struct winsize	 ws;
	int		 mode;
	const char     **envq;

	if (wp->fd != -1)
		close(wp->fd);
	if (cmd != NULL) {
		if (wp->cmd != NULL)
			xfree(wp->cmd);
		wp->cmd = xstrdup(cmd);
	}
	if (cwd != NULL) {
		if (wp->cwd != NULL)
			xfree(wp->cwd);
		wp->cwd = xstrdup(cwd);
	}

	memset(&ws, 0, sizeof ws);
	ws.ws_col = screen_size_x(&wp->base);
	ws.ws_row = screen_size_y(&wp->base);

 	switch (forkpty(&wp->fd, NULL, NULL, &ws)) {
	case -1:
		return (1);
	case 0:
		if (chdir(wp->cwd) != 0)
			chdir("/");
		for (envq = envp; *envq != NULL; envq++) {
			if (putenv(*envq) != 0)
				fatal("putenv failed");
		}
		sigreset();
		log_close();

		execl(_PATH_BSHELL, "sh", "-c", wp->cmd, (char *) NULL);
		fatal("execl failed");
	}

	if ((mode = fcntl(wp->fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(wp->fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");
	if (fcntl(wp->fd, F_SETFD, FD_CLOEXEC) == -1)
		fatal("fcntl failed");

	return (0);
}

int
window_pane_resize(struct window_pane *wp, u_int sx, u_int sy)
{
	struct winsize	ws;

	if (sx == wp->sx && sy == wp->sy)
		return (-1);
	wp->sx = sx;
	wp->sy = sy;

	memset(&ws, 0, sizeof ws);
	ws.ws_col = sx;
	ws.ws_row = sy;

	screen_resize(&wp->base, sx, sy);
	if (wp->mode != NULL)
		wp->mode->resize(wp, sx, sy);

	if (wp->fd != -1 && ioctl(wp->fd, TIOCSWINSZ, &ws) == -1)
		fatal("ioctl failed");
	return (0);
}

int
window_pane_set_mode(struct window_pane *wp, const struct window_mode *mode)
{
	struct screen	*s;

	if (wp->mode != NULL || wp->mode == mode)
		return (1);

	wp->mode = mode;

	if ((s = wp->mode->init(wp)) != NULL)
		wp->screen = s;
	server_redraw_window(wp->window);
	return (0);
}

void
window_pane_reset_mode(struct window_pane *wp)
{
	if (wp->mode == NULL)
		return;

	wp->mode->free(wp);
	wp->mode = NULL;

	wp->screen = &wp->base;
	server_redraw_window(wp->window);
}

void
window_pane_parse(struct window_pane *wp)
{
	input_parse(wp);
}

void
window_pane_key(struct window_pane *wp, struct client *c, int key)
{
	if (wp->mode != NULL)
		wp->mode->key(wp, c, key);
	else
		input_key(wp, key);
}
