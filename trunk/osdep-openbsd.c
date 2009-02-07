/* $Id: osdep-openbsd.c,v 1.8 2009-02-07 19:16:25 nicm Exp $ */

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

#ifdef __OpenBSD__

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))

#define is_runnable(p) \
	((p)->p_stat == SRUN || (p)->p_stat == SIDL || (p)->p_stat == SONPROC)
#define is_stopped(p) \
	((p)->p_stat == SSTOP || (p)->p_stat == SZOMB || (p)->p_stat == SDEAD)

char	*get_argv0(int, char *);
char	*get_proc_argv0(pid_t);

char *
get_argv0(__attribute__ ((unused)) int fd, char *tty)
{
	int		 mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_TTY, 0 };
	struct stat	 sb;
	size_t		 len;
	struct kinfo_proc *buf, *newbuf;
	struct proc	*p, *bestp;
	char		*procname;
	u_int		 i;

	buf = NULL;

	if (stat(tty, &sb) == -1)
		return (NULL);
	mib[3] = sb.st_rdev;

retry:
	if (sysctl(mib, nitems(mib), NULL, &len, NULL, 0) == -1)
		return (NULL);
	len = (len * 5) / 4;

	if ((newbuf = realloc(buf, len)) == NULL) {
		free(buf);
		return (NULL);
	}
	buf = newbuf;

	if (sysctl(mib, nitems(mib), buf, &len, NULL, 0) == -1) {
		if (errno == ENOMEM)
			goto retry;
		free(buf);
		return (NULL);
	}

	bestp = NULL;
	for (i = 0; i < len / sizeof (struct kinfo_proc); i++) {
		if (buf[i].kp_eproc.e_tdev != sb.st_rdev)
			continue;
		p = &buf[i].kp_proc;
		if (bestp == NULL)
			bestp = p;

		if (is_runnable(p) && !is_runnable(bestp))
			bestp = p;
		if (!is_stopped(p) && is_stopped(bestp))
			bestp = p;

		if (p->p_estcpu < bestp->p_estcpu)
			continue;
		if (p->p_slptime > bestp->p_slptime)
			continue;
		if (!(p->p_flag & P_SINTR) && bestp->p_flag & P_SINTR)
			continue;
		if (LIST_FIRST(&p->p_children) != NULL)
			continue;
		bestp = p;
	}	
	if (bestp != NULL) {
		procname = get_proc_argv0(bestp->p_pid);
		if (procname == NULL || *procname == '\0') {
			free(procname);
			procname = strdup(bestp->p_comm);
		}
	} else
		procname = NULL;

	free(buf);
	return (procname);
}

char *
get_proc_argv0(pid_t pid)
{
	int	mib[4] = { CTL_KERN, KERN_PROC_ARGS, 0, KERN_PROC_ARGV };
        size_t	size;
	char  **args, **args2, *procname;

	procname = NULL;

	mib[2] = pid;

	args = NULL;
	size = 128;
	while (size < SIZE_MAX / 2) {
		size *= 2;
		if ((args2 = realloc(args, size)) == NULL)
			break;
		args = args2;
		if (sysctl(mib, 4, args, &size, NULL, 0) == -1) {
			if (errno == ENOMEM)
				continue;
			break;
		}
		if (*args != NULL)
			procname = strdup(*args);
		break;
	}
	free(args);

	return (procname);
}

#endif
