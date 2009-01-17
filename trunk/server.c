/* $Id: server.c,v 1.106 2009-01-17 17:42:10 nicm Exp $ */

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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "tmux.h"

/*
 * Main server functions.
 */

/* Client list. */
struct clients	 clients;

int		 server_main(const char *, int);
void		 server_fill_windows(struct pollfd **);
void		 server_handle_windows(struct pollfd **);
void		 server_fill_clients(struct pollfd **);
void		 server_handle_clients(struct pollfd **);
struct client	*server_accept_client(int);
void		 server_handle_client(struct client *);
void		 server_handle_window(struct window *, struct window_pane *wp);
void		 server_lost_client(struct client *);
void	 	 server_check_window(struct window *);
void		 server_check_redraw(struct client *);
void		 server_redraw_locked(struct client *);
void		 server_check_timers(struct client *);
void		 server_second_timers(void);
int		 server_update_socket(const char *);

int
server_client_index(struct client *c)
{
	u_int	i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (c == ARRAY_ITEM(&clients, i))
			return (i);
	}
	return (-1);
}

/* Fork new server. */
int
server_start(const char *path)
{
	struct sockaddr_un	sa;
	size_t			size;
	mode_t			mask;
	int		   	n, fd, pair[2], mode;
	char		       *cause;
	u_char			ch;

	/* Make a little socketpair to wait for the server to be ready. */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pair) != 0)
		fatal("socketpair failed");

	switch (fork()) {
	case -1:
		fatal("fork failed");
	case 0:
		break;
	default:
		close(pair[1]);

		ch = 0x00;
		if (read(pair[0], &ch, 1) == 1 && ch == 0xff) {
			close(pair[0]);
			return (0);
		}
		ch = 0x00;
		if (write(pair[1], &ch, 1) != 1)
			fatal("write failed");
		close(pair[0]);
		return (1);
	}
	close(pair[0]);

#ifdef DEBUG
	xmalloc_clear();
#endif

	/*
	 * Must daemonise before loading configuration as the PID changes so
	 * $TMUX would be wrong for sessions created in the config file.
	 */
	if (daemon(1, 1) != 0)
		fatal("daemon failed");

	ARRAY_INIT(&windows);
	ARRAY_INIT(&clients);
	ARRAY_INIT(&sessions);
	key_bindings_init();

	server_locked = 0;
	server_password = NULL;
	server_activity = time(NULL);

	if (cfg_file != NULL && load_cfg(cfg_file, &cause) != 0) {
		log_warnx("%s", cause);
		exit(1);
	}

	logfile("server");
#ifndef NO_SETPROCTITLE
	setproctitle("server (%s)", path);
#endif
	log_debug("server started, pid %ld", (long) getpid());
	start_time = time(NULL);
	socket_path = path;
	
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	size = strlcpy(sa.sun_path, path, sizeof sa.sun_path);
	if (size >= sizeof sa.sun_path) {
		errno = ENAMETOOLONG;
		fatal("socket failed");
	}
	unlink(sa.sun_path);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		fatal("socket failed");

	mask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
	if (bind(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) == -1)
		fatal("bind failed");
	umask(mask);

	if (listen(fd, 16) == -1)
		fatal("listen failed");

	if ((mode = fcntl(fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		fatal("fcntl failed");

	ch = 0xff;
	if (write(pair[1], &ch, 1) != 1)
		fatal("write failed");
	read(pair[1], &ch, 1); /* Ignore errors; just to wait before closing. */
	close(pair[1]);

	n = server_main(path, fd);
#ifdef DEBUG
	xmalloc_report(getpid(), "server");
#endif
	exit(n);
}

/* Main server loop. */
int
server_main(const char *srv_path, int srv_fd)
{
	struct window	*w;
	struct pollfd	*pfds, *pfd;
	int		 nfds, xtimeout;
	u_int		 i, n;
	time_t		 now, last;

	siginit();

	last = time(NULL);

	pfds = NULL;
	while (!sigterm) {
		/* Initialise pollfd array. */
		nfds = 1;
		for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
			w = ARRAY_ITEM(&windows, i);
			if (w != NULL)
				nfds += window_count_panes(w);
		}
		nfds += ARRAY_LENGTH(&clients) * 2;
		pfds = xrealloc(pfds, nfds, sizeof *pfds);
		pfd = pfds;

		/* Fill server socket. */
		pfd->fd = srv_fd;
		pfd->events = POLLIN;
		pfd++;

		/* Fill window and client sockets. */
		server_fill_windows(&pfd);
		server_fill_clients(&pfd);

		/* Update socket permissions. */
		xtimeout = INFTIM;
		if (server_update_socket(srv_path) != 0)
			xtimeout = 100;

		/* Do the poll. */
		if ((nfds = poll(pfds, nfds, xtimeout)) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			fatal("poll failed");
		}
		pfd = pfds;

		/* Handle server socket. */
#ifndef BROKEN_POLL
		if (pfd->revents & (POLLERR|POLLNVAL|POLLHUP))
			fatalx("lost server socket");
#endif
		if (pfd->revents & POLLIN) {
			server_accept_client(srv_fd);
			continue;
		}
		pfd++;

		/* Call second-based timers. */
		now = time(NULL);
		if (now != last) {
			last = now;
			server_second_timers();
		}

		/*
		 * Handle window and client sockets. Clients can create
		 * windows, so windows must come first to avoid messing up by
		 * increasing the array size.
		 */
		server_handle_windows(&pfd);
		server_handle_clients(&pfd);

		/*
		 * If we have no sessions and clients left, let's get out
		 * of here...
		 */
		n = 0;
		for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
			if (ARRAY_ITEM(&sessions, i) != NULL)
				n++;
		}
		for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
			if (ARRAY_ITEM(&clients, i) != NULL)
				n++;
		}
		if (n == 0)
			break;
	}
	if (pfds != NULL)
		xfree(pfds);

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		if (ARRAY_ITEM(&sessions, i) != NULL)
			session_destroy(ARRAY_ITEM(&sessions, i));
	}
	ARRAY_FREE(&sessions);

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (ARRAY_ITEM(&clients, i) != NULL)
			server_lost_client(ARRAY_ITEM(&clients, i));
	}
	ARRAY_FREE(&clients);

	key_bindings_free();

	close(srv_fd);
	unlink(srv_path);

	return (0);
}

/* Fill window pollfds. */
void
server_fill_windows(struct pollfd **pfd)
{
	struct window		*w;
	struct window_pane	*wp;
	u_int		 	 i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		w = ARRAY_ITEM(&windows, i);
		if (w == NULL)
			continue;

		TAILQ_FOREACH(wp, &w->panes, entry) {
			(*pfd)->fd = wp->fd;
			if (wp->fd != -1) {
				(*pfd)->events = POLLIN;
				if (BUFFER_USED(wp->out) > 0)
					(*pfd)->events |= POLLOUT;
			}
			(*pfd)++;
		}
	}
}

/* Handle window pollfds. */
void
server_handle_windows(struct pollfd **pfd)
{
	struct window		*w;
	struct window_pane	*wp;
	u_int		 	 i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		w = ARRAY_ITEM(&windows, i);
		if (w == NULL)
			continue;

		TAILQ_FOREACH(wp, &w->panes, entry) {
			if (wp->fd != -1) {
				if (buffer_poll(*pfd, wp->in, wp->out) != 0) {
					close(wp->fd);
					wp->fd = -1;
				} else
					server_handle_window(w, wp);
			}
			(*pfd)++;
		}

		server_check_window(w);
	}
}

/* Check for general redraw on client. */
void
server_check_redraw(struct client *c)
{
	struct session	*s;
	char		*title;
	int		 flags;

	if (c == NULL || c->session == NULL)
		return;
	s = c->session;

	flags = c->tty.flags & TTY_FREEZE;
	c->tty.flags &= ~TTY_FREEZE;

	if (options_get_number(&s->options, "set-titles")) {
		title = s->curw->window->active->screen->title;
		if (c->title == NULL || strcmp(title, c->title) != 0) {
			if (c->title != NULL)
				xfree(c->title);
			c->title = xstrdup(title);
			tty_set_title(&c->tty, c->title);
		}
	}

	if (c->flags & (CLIENT_REDRAW|CLIENT_STATUS)) {
		if (c->message_string != NULL)
			status_message_redraw(c);
		else if (c->prompt_string != NULL)
			status_prompt_redraw(c);
		else
			status_redraw(c);
	}

	if (c->flags & CLIENT_REDRAW) {
		if (server_locked)
			server_redraw_locked(c);
		else
 			screen_redraw_screen(c, NULL);
	}

	if (c->flags & CLIENT_STATUS)
		screen_redraw_status(c);

	c->tty.flags |= flags;

	c->flags &= ~(CLIENT_REDRAW|CLIENT_STATUS);
}

/* Redraw client when locked. */
void
server_redraw_locked(struct client *c)
{
	struct screen_write_ctx	ctx;
	struct screen		screen;
	u_int			colour, xx, yy;
	int    			style;

	xx = c->sx;
	yy = c->sy - 1;
	if (xx == 0 || yy == 0)
		return;
	colour = options_get_number(
	    &global_window_options, "clock-mode-colour");
	style = options_get_number(
	    &global_window_options, "clock-mode-style");
	
	screen_init(&screen, xx, yy, 0);

	screen_write_start(&ctx, NULL, &screen);
	clock_draw(&ctx, colour, style);
	screen_write_stop(&ctx);

	screen_redraw_screen(c, &screen);

	screen_free(&screen);
}

/* Check for timers on client. */
void
server_check_timers(struct client *c)
{
	struct session	*s;
	struct timeval	 tv;
	u_int		 interval;

	if (c == NULL || c->session == NULL)
		return;
	s = c->session;

	if (gettimeofday(&tv, NULL) != 0)
		fatal("gettimeofday");

	if (c->message_string != NULL && timercmp(&tv, &c->message_timer, >))
		server_clear_client_message(c);

	if (c->message_string != NULL || c->prompt_string != NULL) {
		/*
		 * Don't need timed redraw for messages/prompts so bail now.
		 * The status timer isn't reset when they are redrawn anyway.
		 */
		return;
	}
	if (!options_get_number(&s->options, "status"))
		return;

	interval = options_get_number(&s->options, "status-interval");
	if (interval == 0)
		return;

	tv.tv_sec -= interval;
	if (timercmp(&c->status_timer, &tv, <))
		c->flags |= CLIENT_STATUS;
}

/* Fill client pollfds. */
void
server_fill_clients(struct pollfd **pfd)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);

		server_check_timers(c);
		server_check_redraw(c);

		if (c == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = c->fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(c->out) > 0)
				(*pfd)->events |= POLLOUT;
		}
		(*pfd)++;

		if (c == NULL || c->tty.fd == -1 || c->session == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = c->tty.fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(c->tty.out) > 0)
				(*pfd)->events |= POLLOUT;
		}
		(*pfd)++;
	}
}

/* Handle client pollfds. */
void
server_handle_clients(struct pollfd **pfd)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);

		if (c != NULL) {
			if (buffer_poll(*pfd, c->in, c->out) != 0) {
				server_lost_client(c);
				(*pfd) += 2;
				continue;
			} else
				server_msg_dispatch(c);
		}
		(*pfd)++;

		if (c != NULL && c->tty.fd != -1 && c->session != NULL) {
			if (buffer_poll(*pfd, c->tty.in, c->tty.out) != 0)
				server_lost_client(c);
			else
				server_handle_client(c);
		}
		(*pfd)++;
	}
}

/* accept(2) and create new client. */
struct client *
server_accept_client(int srv_fd)
{
	struct client	       *c;
	struct sockaddr_storage	sa;
	socklen_t		slen = sizeof sa;
	int		 	client_fd, mode;
	u_int			i;

	client_fd = accept(srv_fd, (struct sockaddr *) &sa, &slen);
	if (client_fd == -1) {
		if (errno == EAGAIN || errno == EINTR || errno == ECONNABORTED)
			return (NULL);
		fatal("accept failed");
	}
	if ((mode = fcntl(client_fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(client_fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");

	c = xcalloc(1, sizeof *c);
	c->fd = client_fd;
	c->in = buffer_create(BUFSIZ);
	c->out = buffer_create(BUFSIZ);

	ARRAY_INIT(&c->prompt_hdata);

	c->tty.fd = -1;
	c->title = NULL;

	c->session = NULL;
	c->sx = 80;
	c->sy = 25;
	screen_init(&c->status, c->sx, 1, 0);

	c->message_string = NULL;

	c->prompt_string = NULL;
	c->prompt_buffer = NULL;
	c->prompt_index = 0;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (ARRAY_ITEM(&clients, i) == NULL) {
			ARRAY_SET(&clients, i, c);
			return (c);
		}
	}
	ARRAY_ADD(&clients, c);
	return (c);
}

/* Input data from client. */
void
server_handle_client(struct client *c)
{
	struct window_pane	*wp;
	struct timeval	 	 tv;
	struct key_binding	*bd;
	int		 	 key, prefix, status, flags, xtimeout;

	xtimeout = options_get_number(&c->session->options, "repeat-time");
	if (xtimeout != 0 && c->flags & CLIENT_REPEAT) {
		if (gettimeofday(&tv, NULL) != 0)
			fatal("gettimeofday");
		if (timercmp(&tv, &c->repeat_timer, >))
			c->flags &= ~(CLIENT_PREFIX|CLIENT_REPEAT);
	}

	/* Process keys. */
	prefix = options_get_number(&c->session->options, "prefix");
	while (tty_keys_next(&c->tty, &key) == 0) {	
		server_activity = time(NULL);

		if (c->session == NULL)
			return;
		wp = c->session->curw->window->active;	/* could die - do each loop */

		server_clear_client_message(c);
		if (c->prompt_string != NULL) {
			status_prompt_key(c, key);
			continue;
		}
		if (server_locked)
			continue;
		
		/* No previous prefix key. */
		if (!(c->flags & CLIENT_PREFIX)) {
			if (key == prefix)
				c->flags |= CLIENT_PREFIX;
			else
				window_pane_key(wp, c, key);
			continue;
		}

		/* Prefix key already pressed. Reset prefix and lookup key. */
		c->flags &= ~CLIENT_PREFIX;
		if ((bd = key_bindings_lookup(key)) == NULL) {
			/* If repeating, treat this as a key, else ignore. */
			if (c->flags & CLIENT_REPEAT) {
				c->flags &= ~CLIENT_REPEAT;
				if (key == prefix)
					c->flags |= CLIENT_PREFIX;
				else
					window_pane_key(wp, c, key);
			}
			continue;
		}
		flags = bd->cmd->entry->flags;

		/* If already repeating, but this key can't repeat, skip it. */
		if (c->flags & CLIENT_REPEAT && !(flags & CMD_CANREPEAT)) {
			c->flags &= ~CLIENT_REPEAT;
			if (key == prefix)
				c->flags |= CLIENT_PREFIX;
			else
				window_pane_key(wp, c, key);
			continue;
		}
		
		/* If this key can repeat, reset the repeat flags and timer. */
		if (xtimeout != 0 && flags & CMD_CANREPEAT) {
			c->flags |= CLIENT_PREFIX|CLIENT_REPEAT;

			tv.tv_sec = xtimeout / 1000;
			tv.tv_usec = (xtimeout % 1000) * 1000L;
			if (gettimeofday(&c->repeat_timer, NULL) != 0)
				fatal("gettimeofday");
			timeradd(&c->repeat_timer, &tv, &c->repeat_timer);
		}

		/* Dispatch the command. */
		key_bindings_dispatch(bd, c);
	}
	if (c->session == NULL)
		return;
	wp = c->session->curw->window->active;	/* could die - do each loop */
	
	/* Ensure the cursor is in the right place and correctly on or off. */
	status = options_get_number(&c->session->options, "status");
	if (c->prompt_string == NULL && c->message_string == NULL &&
	    !server_locked && wp->screen->mode & MODE_CURSOR &&
	    wp->yoff + wp->screen->cy < c->sy - status) {
		tty_write(&c->tty, wp->screen, 0, TTY_CURSORMODE, 1);
		tty_cursor(&c->tty, wp->screen->cx, wp->screen->cy, wp->yoff);
	} else
		tty_write(&c->tty, wp->screen, 0, TTY_CURSORMODE, 0);
}

/* Lost a client. */
void
server_lost_client(struct client *c)
{
	u_int	i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (ARRAY_ITEM(&clients, i) == c)
			ARRAY_SET(&clients, i, NULL);
	}

	tty_free(&c->tty);

	if (c->title != NULL)
		xfree(c->title);

	if (c->message_string != NULL)
		xfree(c->message_string);

	if (c->prompt_string != NULL)
		xfree(c->prompt_string);
	if (c->prompt_buffer != NULL)
		xfree(c->prompt_buffer);
	for (i = 0; i < ARRAY_LENGTH(&c->prompt_hdata); i++)
		xfree(ARRAY_ITEM(&c->prompt_hdata, i));
	ARRAY_FREE(&c->prompt_hdata);

	if (c->cwd != NULL)
		xfree(c->cwd);

	close(c->fd);
	buffer_destroy(c->in);
	buffer_destroy(c->out);
	xfree(c);

	recalculate_sizes();
}

/* Handle window data. */
void
server_handle_window(struct window *w, struct window_pane *wp)
{
	struct session	*s;
	struct client	*c;
	u_int		 i;
	int		 action, update;

	window_pane_parse(wp);

	if (!(w->flags & WINDOW_BELL) && !(w->flags & WINDOW_ACTIVITY))
		return;

	update = 0;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL || !session_has(s, w))
			continue;

		if (w->flags & WINDOW_BELL &&
		    !session_alert_has_window(s, w, WINDOW_BELL)) {
			session_alert_add(s, w, WINDOW_BELL);

			action = options_get_number(&s->options, "bell-action");
			switch (action) {
			case BELL_ANY:
				if (s->flags & SESSION_UNATTACHED)
					break;
				for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
					c = ARRAY_ITEM(&clients, i);
					if (c != NULL && c->session == s)
						tty_putcode(&c->tty, TTYC_BEL);
				}
				break;
			case BELL_CURRENT:
				if (w->active != wp)
					break;
				for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
					c = ARRAY_ITEM(&clients, i);
					if (c != NULL && c->session == s)
						tty_putcode(&c->tty, TTYC_BEL);
				}
				break;
			}
			update = 1;
		}

		if (options_get_number(&w->options, "monitor-activity") &&
		    (w->flags & WINDOW_ACTIVITY) &&
		    !session_alert_has_window(s, w, WINDOW_ACTIVITY)) {
			session_alert_add(s, w, WINDOW_ACTIVITY);
			update = 1;
		}
	}
	if (update)
		server_status_window(w);

	w->flags &= ~(WINDOW_BELL|WINDOW_ACTIVITY);
}

/* Check if window still exists.. */
void
server_check_window(struct window *w)
{
	struct window_pane	*wp, *wq;
	struct client		*c;
	struct session		*s;
	struct winlink		*wl;
	u_int		 	 i, j;
	int		 	 destroyed, flag;

	flag = options_get_number(&w->options, "remain-on-exit");

	destroyed = 1;

	wp = TAILQ_FIRST(&w->panes);
	while (wp != NULL) {
		wq = TAILQ_NEXT(wp, entry);
		if (wp->fd != -1)
			destroyed = 0;
		else if (!flag) {
			window_remove_pane(w, wp);
			server_redraw_window(w);
		}
		wp = wq;
	} 

	if (!destroyed)
		return;

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL)
			continue;
		if (!session_has(s, w))
			continue;

	restart:
		/* Detach window and either redraw or kill clients. */
		RB_FOREACH(wl, winlinks, &s->windows) {
			if (wl->window != w)
				continue;
			destroyed = session_detach(s, wl);
			for (j = 0; j < ARRAY_LENGTH(&clients); j++) {
				c = ARRAY_ITEM(&clients, j);
				if (c == NULL || c->session != s)
					continue;
				if (!destroyed) {
					server_redraw_client(c);
					continue;
				}
				c->session = NULL;
				server_write_client(c, MSG_EXIT, NULL, 0);
			}
			/* If the session was destroyed, bail now. */
			if (destroyed)
				break;
			goto restart;
		}
	}

	recalculate_sizes();
}

/* Call any once-per-second timers. */
void
server_second_timers(void)
{
	struct window		*w;
	struct window_pane	*wp;
	u_int		 	 i;
	int			 xtimeout;
	struct tm	 	 now, then;
	static time_t	 	 last_t = 0;
	time_t		 	 t;

	t = time(NULL);
	xtimeout = options_get_number(&global_options, "lock-after-time");
	if (xtimeout > 0 && t > server_activity + xtimeout)
		server_lock();

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		w = ARRAY_ITEM(&windows, i);
		if (w == NULL)
			continue;

		TAILQ_FOREACH(wp, &w->panes, entry) {
			if (wp->mode != NULL && wp->mode->timer != NULL)
				wp->mode->timer(wp);
		}
	}

	gmtime_r(&t, &now);
	gmtime_r(&last_t, &then);
	if (now.tm_min == then.tm_min)
		return;
	last_t = t;

	/* If locked, redraw all clients. */
	if (server_locked) {
		for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
			if (ARRAY_ITEM(&clients, i) != NULL)
				server_redraw_client(ARRAY_ITEM(&clients, i));
		}
	}	
}

/* Update socket execute permissions based on whether sessions are attached. */
int
server_update_socket(const char *path)
{
	struct session	*s;
	u_int		 i;
	static int	 last = -1;
	int		 n;

	n = 0;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s != NULL && !(s->flags & SESSION_UNATTACHED)) {
			n++;
			break;
		}
	}

	if (n != last) {
		last = n;
		if (n != 0)
			chmod(path, S_IRWXU);
		else
			chmod(path, S_IRUSR|S_IWUSR);
	}

	return (n);
}
