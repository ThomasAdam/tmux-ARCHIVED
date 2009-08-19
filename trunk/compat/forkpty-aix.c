/* $Id: forkpty-aix.c,v 1.3 2009-08-19 16:06:45 nicm Exp $ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
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
#include <stdlib.h>
#include <stropts.h>
#include <unistd.h>

#include "tmux.h"

pid_t
forkpty(int *master, unused char *name, struct termios *tio, struct winsize *ws)
{
	int	slave, fd;
	char   *path;
	pid_t	pid;

	if ((*master = open("/dev/ptc", O_RDWR|O_NOCTTY)) == -1)
		return (-1);

	if ((path = ttyname(*master)) == NULL)
		goto out;
	if ((slave = open(path, O_RDWR|O_NOCTTY)) == -1)
		goto out;

	switch (pid = fork()) {
	case -1:
		goto out;
	case 0:
		close(*master);

		fd = open(_PATH_TTY, O_RDWR|O_NOCTTY);
		if (fd >= 0) {
			ioctl(fd, TIOCNOTTY, NULL);
			close(fd);
		}
		
		if (setsid() < 0)
			fatal("setsid");
         
		fd = open(_PATH_TTY, O_RDWR|O_NOCTTY);
		if (fd >= 0)
			fatalx("open succeeded (failed to disconnect)");

		fd = open(path, O_RDWR);
		if (fd < 0)
			fatal("open failed");
		close(fd);

		fd = open("/dev/tty", O_WRONLY);
		if (fd < 0)
			fatal("open failed");
		close(fd);

		if (tcsetattr(slave, TCSAFLUSH, tio) == -1)
			fatal("tcsetattr failed");
		if (ioctl(slave, TIOCSWINSZ, ws) == -1)
			fatal("ioctl failed");

		dup2(slave, 0);
		dup2(slave, 1);
		dup2(slave, 2);
		if (slave > 2)
			close(slave);
		return (0);
	}

	close(slave);
	return (pid);

out:
	if (*master != -1)
		close(*master);
	if (slave != -1)
		close(slave);
	return (-1);
}
