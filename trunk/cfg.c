/* $Id: cfg.c,v 1.2 2008-06-02 18:23:37 nicm Exp $ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicm@users.sourceforge.net>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "tmux.h"

/*
 * Config file parser. Pretty quick and simple, each line is parsed into a
 * argv array and executed as a command.
 */

char	 *cfg_string(FILE *, char, int);
void printflike2 cfg_print(struct cmd_ctx *, const char *, ...);
void printflike2 cfg_error(struct cmd_ctx *, const char *, ...);

void printflike2
cfg_print(unused struct cmd_ctx *ctx, unused const char *fmt, ...)
{
}

void printflike2
cfg_error(struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;
	char   *msg;

	va_start(ap, fmt);
	xvasprintf(&msg, fmt, ap);
	va_end(ap);

	*msg = toupper((u_char) *msg);
	// XXX
	log_warnx("%s", msg);
	xfree(msg);
}

int
load_cfg(const char *path, char **cause)
{
	FILE   	       *f;
	int		ch, argc;
	u_int		line;
	char	      **argv, *buf, *s;
	size_t		len;
	struct cmd     *cmd;
	struct cmd_ctx	ctx;

	if ((f = fopen(path, "rb")) == NULL) {
		xasprintf(cause, "%s: %s", path, strerror(errno));
		return (1);
	}

	argv = NULL;
	argc = 0;

	buf = NULL;
	len = 0;

	line = 1;
	while ((ch = getc(f)) != EOF) {
		switch (ch) {
		case '#':
			/* Comment: discard until EOL. */
			while ((ch = getc(f)) != '\n' && ch != EOF)
				;
			line++;
			break;
		case '\'':
			if ((s = cfg_string(f, '\'', 0)) == NULL)
				goto error;
			argv = xrealloc(argv, argc + 1, sizeof (char *));
			argv[argc++] = s;
			break;
		case '"':
			if ((s = cfg_string(f, '"', 1)) == NULL)
				goto error;
			argv = xrealloc(argv, argc + 1, sizeof (char *));
			argv[argc++] = s;
			break;
		case '\n':
		case EOF:
		case ' ':
		case '\t':
			if (len == 0)
				break;
			buf[len] = '\0';
			
			argv = xrealloc(argv, argc + 1, sizeof (char *));
			argv[argc++] = buf;
			
			buf = NULL;
			len = 0;

			if (ch != '\n' && ch != EOF)
				break;
			line++;
			
			if ((cmd = cmd_parse(argc, argv, cause)) == NULL) {
				if (*cause != NULL)
					xfree(*cause); /* XXX */
				goto error;
			}

			ctx.msgdata = NULL;
			ctx.cursession = NULL;
			ctx.curclient = NULL;
			
			ctx.error = cfg_error;
			ctx.print = cfg_print;
			
			ctx.cmdclient = NULL;
			ctx.flags = CMD_KEY;
			
			cmd_exec(cmd, &ctx);			

			cmd_free(cmd);

			while (--argc >= 0)
				xfree(argv[argc]);
			argc = 0;
			break;
		default:
			if (len >= SIZE_MAX - 2)
				goto error;
				
			buf = xrealloc(buf, 1, len + 1);
			buf[len++] = ch;
			break;
		}
	}

	fclose(f);

	return (0);

error:
	while (--argc > 0)
		xfree(argv[argc]);
	xfree(argv);

	if (buf != NULL)
		xfree(buf);
	
	xasprintf(cause, "%s: error at line %u", path, line);
	return (1);
}

char *
cfg_string(FILE *f, char endch, int esc)
{
	int	ch;
	char   *buf;
	size_t	len;

        buf = NULL;
	len = 0;

        while ((ch = getc(f)) != endch) {
                switch (ch) {
		case EOF:
			xfree(buf);
			return (NULL);
                case '\\':
			if (!esc)
				break;
                        switch (ch = getc(f)) {
			case EOF:
				xfree(buf);
				return (NULL);
                        case 'r':
                                ch = '\r';
                                break;
                        case 'n':
                                ch = '\n';
                                break;
                        case 't':
                                ch = '\t';
                                break;
                        }
                        break;
                }

		if (len >= SIZE_MAX - 2) {
			xfree(buf);
			return (NULL);
		}
		buf = xrealloc(buf, 1, len + 1);
                buf[len++] = ch;
        }

        buf[len] = '\0';
	return (buf);
}
