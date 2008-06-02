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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
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
void		 server_handle_window(struct window *);
void		 server_lost_client(struct client *);
void	 	 server_lost_window(struct window *);

/* Fork new server. */
int
server_start(const char *path)
{
	struct sockaddr_un	sa;
	size_t			size;
	mode_t			mask;
	int		   	n, fd, mode;
	char		       *cause;

	switch (fork()) {
	case -1:
		fatal("fork");
	case 0:
		break;
	default:
		return (0);
	}

#ifdef DEBUG
	xmalloc_clear();
#endif

	logfile("server");
#ifndef NO_SETPROCTITLE
	setproctitle("server (%s)", path);
#endif
	log_debug("server started, pid %ld", (long) getpid());

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

 	/* Load configuration. */
	if (cfg_file != NULL && load_cfg(cfg_file, &cause) != 0) {
		log_warnx("%s", cause);
		xfree(cause);
		exit(1);
	}

	if (daemon(1, 1) != 0)
		fatal("daemon failed");
	log_debug("server daemonised, pid now %ld", (long) getpid());

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
	struct pollfd	*pfds, *pfd;
	int		 nfds;
	u_int		 i;

	siginit();

	ARRAY_INIT(&windows);
	ARRAY_INIT(&clients);
	ARRAY_INIT(&sessions);

	key_bindings_init();
 
	pfds = NULL;
	while (!sigterm) {
		/* Initialise pollfd array. */
		nfds = 1 + ARRAY_LENGTH(&windows) + ARRAY_LENGTH(&clients) * 2;
		pfds = xrealloc(pfds, nfds, sizeof *pfds);
		pfd = pfds;

		/* Fill server socket. */
		pfd->fd = srv_fd;
		pfd->events = POLLIN;
		pfd++;

		/* Fill window and client sockets. */
		server_fill_windows(&pfd);
		server_fill_clients(&pfd);

		/* Do the poll. */
		log_debug("polling %d fds", nfds);
		if ((nfds = poll(pfds, nfds, INFTIM)) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			fatal("poll failed");
		}
		pfd = pfds;
		log_debug("poll returned %d", nfds);

		/* Handle server socket. */
		if (pfd->revents & (POLLERR|POLLNVAL|POLLHUP))
			fatalx("lost server socket");
		if (pfd->revents & POLLIN) {
			server_accept_client(srv_fd);
			continue;
		}
		pfd++;

		/*
		 * Handle window and client sockets. Clients can create
		 * windows, so windows must come first to avoid messing up by
		 * increasing the array size.
		 */
		server_handle_windows(&pfd);
		server_handle_clients(&pfd);
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
	struct window	*w;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		if ((w = ARRAY_ITEM(&windows, i)) == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = w->fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(w->out) > 0)
				(*pfd)->events |= POLLOUT;
			log_debug("adding window %d (%d)", (*pfd)->fd, w->fd);
		}
		(*pfd)++;
	}
}

/* Handle window pollfds. */
void
server_handle_windows(struct pollfd **pfd)
{
	struct window	*w;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		if ((w = ARRAY_ITEM(&windows, i)) != NULL) {
			log_debug("testing window %d (%d)", (*pfd)->fd, w->fd);
			if (buffer_poll(*pfd, w->in, w->out) != 0)
				server_lost_window(w);
			else
				server_handle_window(w);
		}
		(*pfd)++;
	}
}

/* Fill client pollfds. */
void
server_fill_clients(struct pollfd **pfd)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);

		if (c == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = c->fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(c->out) > 0)
				(*pfd)->events |= POLLOUT;
			log_debug("adding client %d (%d)", (*pfd)->fd, c->fd);
		}
		(*pfd)++;

		if (c == NULL || c->tty.fd == -1 || c->session == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = c->tty.fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(c->tty.out) > 0)
				(*pfd)->events |= POLLOUT;
			log_debug("adding tty %d (%d)", (*pfd)->fd, c->tty.fd);
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
			log_debug("testing client %d (%d)", (*pfd)->fd, c->fd);
			if (buffer_poll(*pfd, c->in, c->out) != 0) {
				server_lost_client(c);
				(*pfd) += 2;
				continue;
			} else
				server_msg_dispatch(c);
		}
		(*pfd)++;

		if (c != NULL && c->tty.fd != -1 && c->session != NULL) {
			log_debug("testing tty %d (%d)", (*pfd)->fd, c->tty.fd);
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

	c->tty.fd = -1;

	c->session = NULL;
	c->sx = 80;
	c->sy = 25;

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
	struct window	*w = c->session->curw->window;
	int		 key;

	while (tty_keys_next(&c->tty, &key) == 0) {
		if (c->flags & CLIENT_PREFIX) {
			key_bindings_dispatch(key, c);
			c->flags &= ~CLIENT_PREFIX;
			continue;
		} else if (key == prefix_key)
			c->flags |= CLIENT_PREFIX;
		else
			window_key(w, key);
	}
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

	close(c->fd);
	buffer_destroy(c->in);
	buffer_destroy(c->out);
	xfree(c);

	recalculate_sizes();
}

/* Handle window data. */
void
server_handle_window(struct window *w)
{
	struct session	*s;
	u_int		 i;

	window_parse(w);

	if (!(w->flags & WINDOW_BELL))
		return;

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s != NULL)
			session_addbell(s, w);
	}

	switch (bell_action) {
	case BELL_ANY:
		tty_write_window(w, TTY_CHARACTER, '\007');
		break;
	case BELL_CURRENT:
		for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
			s = ARRAY_ITEM(&sessions, i);
			if (s != NULL && s->curw->window == w)
				tty_write_session(s, TTY_CHARACTER, '\007');
		}
		break;
	}
	server_status_window(w);

	w->flags &= ~WINDOW_BELL;
}

/* Lost window: move clients on to next window. */
void
server_lost_window(struct window *w)
{
	struct client	*c;
	struct session	*s;
	struct winlink	*wl;
	u_int		 i, j;
	int		 destroyed;

	log_debug("lost window %d", w->fd);

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

