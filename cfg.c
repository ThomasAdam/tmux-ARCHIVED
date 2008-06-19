/* $Id$ */

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

char	 *cfg_cause;

void printflike2
cfg_print(unused struct cmd_ctx *ctx, unused const char *fmt, ...)
{
}

void printflike2
cfg_error(unused struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	xvasprintf(&cfg_cause, fmt, ap);
	va_end(ap);
}

int
load_cfg(const char *path, char **cause)
{
	FILE   	       *f;
	u_int		n;
	char	       *buf, *line, *ptr;
	size_t		len;
	struct cmd     *cmd;
	struct cmd_ctx	ctx;

	if ((f = fopen(path, "rb")) == NULL) {
		xasprintf(cause, "%s: %s", path, strerror(errno));
		return (1);
	}
	n = 0;

	line = NULL;
	while ((buf = fgetln(f, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			line = xrealloc(line, 1, len + 1);
			memcpy(line, buf, len);
			line[len] = '\0';
			buf = line;
		}
		n++;

		/* Trim spaces from start and end. */
		while (*buf != '\0' && (*buf == ' ' || *buf == '\t'))
			*buf++ = '\0';
		len = strlen(buf);
		while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
			buf[--len] = '\0';
		if (*buf == '\0')
			continue;

		if ((cmd = cmd_string_parse(buf, cause)) == NULL)
			goto error;
		cfg_cause = NULL;

		ctx.msgdata = NULL;
		ctx.cursession = NULL;
		ctx.curclient = NULL;

		ctx.error = cfg_error;
		ctx.print = cfg_print;
		ctx.info = cfg_print;

		ctx.cmdclient = NULL;
		ctx.flags = 0;

		cfg_cause = NULL;
		cmd_exec(cmd, &ctx);
		cmd_free(cmd);
		if (cfg_cause != NULL) {
			*cause = cfg_cause;
			goto error;
		}
	}
	if (line != NULL)
		xfree(line);

	return (0);

error:
	xasprintf(&ptr, "%s: %s at line %u", path, *cause, n);
	xfree(*cause);
	*cause = ptr;
	return (1);
}
