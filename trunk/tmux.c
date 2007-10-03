/* $Id: tmux.c,v 1.22 2007-10-03 12:34:16 nicm Exp $ */

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
#include <sys/wait.h>

#include <paths.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tmux.h"

#ifdef DEBUG
const char	*malloc_options = "AFGJPX";
#endif

volatile sig_atomic_t sigwinch;
volatile sig_atomic_t sigterm;
int		 debug_level;
u_int		 status_lines;
char		*default_command;

void		 sighandler(int);

struct op {
	const char     *cmd;
	const char     *alias;
	int		(*fn)(char *, int, char **);
};
const struct op op_table[] = {
	{ "attach", NULL, op_attach },
	{ "list-sessions", "ls", op_list_sessions },
	{ "list-windows", "lsw", op_list_windows },
	{ "new-session", "new", op_new/*_session*/ },
	{ "rename-window", "renw", op_rename },
	{ "bind-key", "bind", op_bind_key },
	{ "unbind-key", "unbind", op_unbind_key },
};
#define NOP (sizeof op_table / sizeof op_table[0])

int
usage(const char *fmt, ...)
{
	char	*msg;
	va_list	 ap;

	if (fmt == NULL) {
		fprintf(stderr,
		    "usage: %s [-v] [-S path] command [flags]\n", __progname);
		return (1);
	}

	va_start(ap, fmt);
	xvasprintf(&msg, fmt, ap);
	va_end(ap);
	fprintf(stderr, "usage: %s [-v] [-S path] %s\n", __progname, msg);
	xfree(msg);
	return (1);
}

void
logfile(const char *name)
{
	FILE	*f;
	char	*path;

	log_close();
	if (debug_level > 0) {
		xasprintf(
		    &path, "%s-%s-%ld.log", __progname, name, (long) getpid());
		f = fopen(path, "w");
		log_open(f, LOG_DAEMON, debug_level);
		xfree(path);
	}
}

void
sighandler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		sigwinch = 1;
		break;
	case SIGTERM:
		sigterm = 1;
		break;
	case SIGCHLD:
		waitpid(WAIT_ANY, NULL, WNOHANG);
		break;
	}
}

void
siginit(void)
{
	struct sigaction	 act;

	memset(&act, 0, sizeof act);
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;

	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR1, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR2, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGINT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTSTP, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGQUIT, &act, NULL) != 0)
		fatal("sigaction failed");

	act.sa_handler = sighandler;
	if (sigaction(SIGWINCH, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		fatal("sigaction failed");
}

void
sigreset(void)
{
	struct sigaction act;
	
	memset(&act, 0, sizeof act);
	sigemptyset(&act.sa_mask);
	
	act.sa_handler = SIG_DFL;
	if (sigaction(SIGPIPE, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR1, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGUSR2, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGINT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTSTP, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGQUIT, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGWINCH, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGTERM, &act, NULL) != 0)
		fatal("sigaction failed");
	if (sigaction(SIGCHLD, &act, NULL) != 0)
		fatal("sigaction failed");
}

int
main(int argc, char **argv)
{
	const struct op		*op, *found;
	const char		*shell;
	char			*path;
	int	 		 opt;
	u_int			 i;

	path = NULL;
        while ((opt = getopt(argc, argv, "S:v?")) != EOF) {
                switch (opt) {
		case 'S':
			path = xstrdup(optarg);
			break;
		case 'v':
			debug_level++;
			break;
                case '?':
                default:
                        exit(usage(NULL));
                }
        }
	argc -= optind;
	argv += optind;
	if (argc == 0)
		exit(usage(NULL));

	log_open(stderr, LOG_USER, debug_level);

	status_lines = 1;

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/ksh";
	xasprintf(&default_command, "%s -l", shell);

	found = NULL;
	for (i = 0; i < NOP; i++) {
		op = op_table + i;
		if (op->alias != NULL && strcmp(argv[0], op->alias) == 0)
			exit(op->fn(path, argc, argv));
		if (strncmp(argv[0], op->cmd, strlen(argv[0])) == 0) {
			if (found != NULL) {
				log_warnx("ambiguous command: %s", argv[0]);
				exit(1);
			}
			found = op;
		}
	}
	if (found != NULL)
		exit(found->fn(path, argc, argv));

	exit(usage(NULL));
}
