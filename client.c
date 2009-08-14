/* $Id: client.c,v 1.62 2009/08/14 21:04:04 tcunha Exp $ */

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
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tmux.h"

void	client_send_environ(struct client_ctx *);
void	client_handle_winch(struct client_ctx *);

int
client_init(char *path, struct client_ctx *cctx, int cmdflags, int flags)
{
	struct sockaddr_un		sa;
	struct stat			sb;
	struct msg_identify_data	data;
	struct winsize			ws;
	size_t				size;
	int				fd, mode;
	char			       *name, *term;
#ifdef HAVE_SETPROCTITLE
	char		 		rpathbuf[MAXPATHLEN];
#endif

#ifdef HAVE_SETPROCTITLE
	if (realpath(path, rpathbuf) == NULL)
		strlcpy(rpathbuf, path, sizeof rpathbuf);
	setproctitle("client (%s)", rpathbuf);
#endif

	if (lstat(path, &sb) != 0) {
		if (cmdflags & CMD_STARTSERVER && errno == ENOENT) {
			if ((fd = server_start(path)) == -1)
				goto start_failed;
			goto server_started;
		}
		goto not_found;
	}
	if (!S_ISSOCK(sb.st_mode)) {
		errno = ENOTSOCK;
		goto not_found;
	}

	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	size = strlcpy(sa.sun_path, path, sizeof sa.sun_path);
	if (size >= sizeof sa.sun_path) {
		errno = ENAMETOOLONG;
		goto not_found;
	}

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		fatal("socket");

	if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) == -1) {
		if (errno == ECONNREFUSED) {
			if (unlink(path) != 0 || !(cmdflags & CMD_STARTSERVER))
				goto not_found;
			if ((fd = server_start(path)) == -1)
				goto start_failed;
			goto server_started;
		}
		goto not_found;
	}

server_started:
	if ((mode = fcntl(fd, F_GETFL)) == -1)
		fatal("fcntl failed");
	if (fcntl(fd, F_SETFL, mode|O_NONBLOCK) == -1)
		fatal("fcntl failed");
	imsg_init(&cctx->ibuf, fd);

	if (cmdflags & CMD_SENDENVIRON)
		client_send_environ(cctx);
	if (isatty(STDIN_FILENO)) {
		if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
			fatal("ioctl(TIOCGWINSZ)");
		data.flags = flags;
		data.sx = ws.ws_col;
		data.sy = ws.ws_row;

		if (getcwd(data.cwd, sizeof data.cwd) == NULL)
			*data.cwd = '\0';

		*data.term = '\0';
		if ((term = getenv("TERM")) != NULL) {
			if (strlcpy(data.term,
			    term, sizeof data.term) >= sizeof data.term)
				*data.term = '\0';
		}

		*data.tty = '\0';
		if ((name = ttyname(STDIN_FILENO)) == NULL)
			fatal("ttyname failed");
		if (strlcpy(data.tty, name, sizeof data.tty) >= sizeof data.tty)
			fatalx("ttyname failed");

		client_write_server(cctx, MSG_IDENTIFY, &data, sizeof data);
	}

	return (0);

start_failed:
	log_warnx("server failed to start");
	return (1);

not_found:
	log_warn("server not found");
	return (1);
}

void
client_send_environ(struct client_ctx *cctx)
{
	char		      **var;
	struct msg_environ_data	data;

 	for (var = environ; *var != NULL; var++) {
		if (strlcpy(data.var, *var, sizeof data.var) >= sizeof data.var)
			continue;
		client_write_server(cctx, MSG_ENVIRON, &data, sizeof data);
	}
}

int
client_main(struct client_ctx *cctx)
{
	struct pollfd	 pfd;
	int		 nfds;

	siginit();

	logfile("client");

	for (;;) {
		if (sigterm)
			client_write_server(cctx, MSG_EXITING, NULL, 0);
		if (sigchld) {
			waitpid(WAIT_ANY, NULL, WNOHANG);
			sigchld = 0;
		}
		if (sigwinch)
			client_handle_winch(cctx);
		if (sigcont) {
			siginit();
			client_write_server(cctx, MSG_WAKEUP, NULL, 0);
			sigcont = 0;
		}

		pfd.fd = cctx->ibuf.fd;
		pfd.events = POLLIN;
		if (cctx->ibuf.w.queued > 0)
			pfd.events |= POLLOUT;

		if ((nfds = poll(&pfd, 1, INFTIM)) == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			fatal("poll failed");
		}
		if (nfds == 0)
			continue;

		if (pfd.revents & (POLLERR|POLLHUP|POLLNVAL))
			fatalx("socket error");

		if (pfd.revents & POLLIN) {
			if (client_msg_dispatch(cctx) != 0)
				break;
		}

		if (pfd.revents & POLLOUT) {
			if (msgbuf_write(&cctx->ibuf.w) < 0) {
				cctx->exittype = CCTX_DIED;
				break;
			}
		}
	}

 	if (sigterm) {
 		printf("[terminated]\n");
 		return (1);
 	}
	switch (cctx->exittype) {
	case CCTX_DIED:
		printf("[lost server]\n");
		return (0);
	case CCTX_SHUTDOWN:
		printf("[server exited]\n");
		return (0);
	case CCTX_EXIT:
		printf("[exited]\n");
		return (0);
	case CCTX_DETACH:
		printf("[detached]\n");
		return (0);
	default:
		printf("[error: %s]\n", cctx->errstr);
		return (1);
	}
}

void
client_handle_winch(struct client_ctx *cctx)
{
	struct msg_resize_data	data;
	struct winsize		ws;

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
		fatal("ioctl failed");

	data.sx = ws.ws_col;
	data.sy = ws.ws_row;
	client_write_server(cctx, MSG_RESIZE, &data, sizeof data);

	sigwinch = 0;
}

int
client_msg_dispatch(struct client_ctx *cctx)
{
	struct imsg		 imsg;
	struct msg_print_data	 printdata;
	ssize_t			 n, datalen;

	if ((n = imsg_read(&cctx->ibuf)) == -1 || n == 0) {
		cctx->exittype = CCTX_DIED;
		return (-1);
	}

	for (;;) {
		if ((n = imsg_get(&cctx->ibuf, &imsg)) == -1)
			fatalx("imsg_get failed");
		if (n == 0)
			return (0);
		datalen = imsg.hdr.len - IMSG_HEADER_SIZE;

		switch (imsg.hdr.type) {
		case MSG_DETACH:
			if (datalen != 0)
				fatalx("bad MSG_DETACH size");

			client_write_server(cctx, MSG_EXITING, NULL, 0);
			cctx->exittype = CCTX_DETACH;
			break;
		case MSG_ERROR:
			if (datalen != sizeof printdata)
				fatalx("bad MSG_ERROR size");
			memcpy(&printdata, imsg.data, sizeof printdata);

			printdata.msg[(sizeof printdata.msg) - 1] = '\0';
			cctx->errstr = xstrdup(printdata.msg);
			imsg_free(&imsg);
			return (-1);
		case MSG_EXIT:
			if (datalen != 0)
				fatalx("bad MSG_EXIT size");

			client_write_server(cctx, MSG_EXITING, NULL, 0);
			cctx->exittype = CCTX_EXIT;
			break;
		case MSG_EXITED:
			if (datalen != 0)
				fatalx("bad MSG_EXITED size");

			imsg_free(&imsg);
			return (-1);
		case MSG_SHUTDOWN:
			if (datalen != 0)
				fatalx("bad MSG_SHUTDOWN size");

			client_write_server(cctx, MSG_EXITING, NULL, 0);
			cctx->exittype = CCTX_SHUTDOWN;
			break;
		case MSG_SUSPEND:
			if (datalen != 0)
				fatalx("bad MSG_SUSPEND size");

			client_suspend();
			break;
		default:
			fatalx("unexpected message");
		}

		imsg_free(&imsg);
	}
}
