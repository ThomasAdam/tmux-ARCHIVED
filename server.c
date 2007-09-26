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
#include <util.h>

#include "tmux.h"

/*
 * Main server functions.
 */

/* Client list. */
struct clients	 clients;

int		 server_main(char *, int);
void		 fill_windows(struct pollfd **);
void		 handle_windows(struct pollfd **);
void		 fill_clients(struct pollfd **);
void		 handle_clients(struct pollfd **);
struct client	*accept_client(int);
void		 lost_client(struct client *);
void	 	 lost_window(struct window *);

int
server_start(char *path)
{
	struct sockaddr_un	sa;
	size_t			sz;
	pid_t			pid;
	mode_t			mode;
	int		   	fd;

	switch (pid = fork()) {
	case -1:
		log_warn("fork");
		return (-1);
	case 0:
		break;
	default:
		return (0);
	}

	logfile("server");
	setproctitle("server (%s)", path);

	log_debug("server started, pid %ld", (long) getpid());

	/* Create the socket. */
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	sz = strlcpy(sa.sun_path, path, sizeof sa.sun_path);
	if (sz >= sizeof sa.sun_path) {
		errno = ENAMETOOLONG;
		fatal("socket failed");
	}
	unlink(sa.sun_path);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		fatal("socket failed");

	mode = umask(S_IXUSR|S_IRWXG|S_IRWXO);
	if (bind(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) == -1)
		fatal("bind failed");
	umask(mode);

	if (listen(fd, 16) == -1)
		fatal("listen failed");

	/*
	 * Detach into the background. This means the PID changes which will
	 * have to be fixed in some way at some point... XXX
	 */
	if (daemon(1, 1) != 0)
		fatal("daemon failed");
	log_debug("server daemonised, pid now %ld", (long) getpid());

	exit(server_main(path, fd));
}

/* Main server loop. */
int
server_main(char *srv_path, int srv_fd)
{
	struct pollfd  		*pfds, *pfd;
	int			 nfds, mode;

	siginit();

	ARRAY_INIT(&windows);
	ARRAY_INIT(&clients);
	ARRAY_INIT(&sessions);

	if ((mode = fcntl(srv_fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(srv_fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");

	pfds = NULL;
	while (!sigterm) {
		/* Initialise pollfd array. */
		nfds = 1 + ARRAY_LENGTH(&windows) + ARRAY_LENGTH(&clients);
		pfds = xrealloc(pfds, nfds, sizeof *pfds);
		pfd = pfds;

		/* Fill server socket. */
		pfd->fd = srv_fd;
		pfd->events = POLLIN;
		pfd++;

		/* Fill window and client sockets. */
		fill_windows(&pfd);
		fill_clients(&pfd);

		/* Do the poll. */
		if (poll(pfds, nfds, INFTIM) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			fatal("poll failed");
		}
		pfd = pfds;

		/* Handle server socket. */
		if (pfd->revents & (POLLERR|POLLNVAL|POLLHUP))
			fatalx("lost server socket");
		if (pfd->revents & POLLIN) {
			accept_client(srv_fd);
			continue;
		}
		pfd++;

		/*
		 * Handle window and client sockets. Clients can create
		 * windows, so windows must come first to avoid messing up by
		 * increasing the array size.
		 */
		handle_windows(&pfd);
		handle_clients(&pfd);
	}

	close(srv_fd);
	unlink(srv_path);

	return (0);
}

/* Fill window pollfds. */
void
fill_windows(struct pollfd **pfd)
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
		}
		(*pfd)++;
	}
}

/* Handle window pollfds. */
void
handle_windows(struct pollfd **pfd)
{
	struct window	*w;
	u_int		 i;
	struct buffer	*b;

	for (i = 0; i < ARRAY_LENGTH(&windows); i++) {
		if ((w = ARRAY_ITEM(&windows, i)) != NULL) {
			if (window_poll(w, *pfd) != 0)
				lost_window(w);
			else {
				b = buffer_create(BUFSIZ);
				window_output(w, b);
				if (BUFFER_USED(b) != 0) {
					write_clients(w, MSG_OUTPUT,
					    BUFFER_OUT(b), BUFFER_USED(b));
				}
				buffer_destroy(b);
			}
		}
		(*pfd)++;
	}
}

/* Fill client pollfds. */
void
fill_clients(struct pollfd **pfd)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if ((c = ARRAY_ITEM(&clients, i)) == NULL)
			(*pfd)->fd = -1;
		else {
			(*pfd)->fd = c->fd;
			(*pfd)->events = POLLIN;
			if (BUFFER_USED(c->out) > 0)
				(*pfd)->events |= POLLOUT;
		}
		(*pfd)++;
	}
}

/* Handle client pollfds. */
void
handle_clients(struct pollfd *(*pfd))
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if ((c = ARRAY_ITEM(&clients, i)) != NULL) {
			if (buffer_poll((*pfd), c->in, c->out) != 0)
				lost_client(c);
			else
				server_msg_dispatch(c);
		}
		(*pfd)++;
	}
}

/* accept(2) and create new client. */
struct client *
accept_client(int srv_fd)
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

	c = xmalloc(sizeof *c);
	c->fd = client_fd;
	c->in = buffer_create(BUFSIZ);
	c->out = buffer_create(BUFSIZ);
	c->session = NULL;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (ARRAY_ITEM(&clients, i) == NULL) {
			ARRAY_SET(&clients, i, c);
			return (c);
		}
	}
	ARRAY_ADD(&clients, c);
	return (c);
}

/* Lost a client. */
void
lost_client(struct client *c)
{
	u_int	i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		if (ARRAY_ITEM(&clients, i) == c)
			ARRAY_SET(&clients, i, NULL);
	}
	
	close(c->fd);
	buffer_destroy(c->in);
	buffer_destroy(c->out);
	xfree(c);
}

/* Lost window: move clients on to next window. */
void
lost_window(struct window *w)
{
	struct client	*c;
	struct session	*s;
	u_int		 i, j;
	int		 destroyed;

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL)
			continue;
		if (!session_has(s, w))
			continue;

		/* Detach window and either redraw or kill clients. */
		destroyed = session_detach(s, w);
		for (j = 0; j < ARRAY_LENGTH(&clients); j++) {
			c = ARRAY_ITEM(&clients, j);
			if (c == NULL || c->session != s)
				continue;
			if (destroyed) {
				c->session = NULL;
				write_client(c, MSG_EXIT, NULL, 0);
			} else
				changed_window(c);
		}
	}
}

