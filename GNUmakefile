# $Id$

.PHONY: clean

VERSION= 0.9

FDEBUG= 1

CC?= gcc
CFLAGS+= -DBUILD="\"$(VERSION)\""
LDFLAGS+= -L/usr/local/lib
LIBS+= -lncurses

# This sort of sucks but gets rid of the stupid warning and should work on
# most platforms...
ifeq ($(shell ($(CC) -v 2>&1|awk '/gcc version 4/') || true), )
CPPFLAGS:= -I. -I- $(CPPFLAGS)
else
CPPFLAGS:= -iquote. $(CPPFLAGS)
endif

ifdef FDEBUG
LDFLAGS+= -rdynamic
CFLAGS+= -g -ggdb -DDEBUG
LIBS+= -ldl
CFLAGS+= -Wno-long-long -Wall -W -Wnested-externs -Wformat=2
CFLAGS+= -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations
CFLAGS+= -Wwrite-strings -Wshadow -Wpointer-arith -Wcast-qual -Wsign-compare
CFLAGS+= -Wundef -Wbad-function-cast -Winline -Wcast-align
endif

PREFIX?= /usr/local
INSTALLDIR= install -d
INSTALLBIN= install -g bin -o root -m 555
INSTALLMAN= install -g bin -o root -m 444

SRCS= $(shell echo *.c|sed 's|osdep-[a-z0-9]*.c||g')
include config.mk
OBJS= $(patsubst %.c,%.o,$(SRCS))

all:		$(OBJS)
		$(CC) $(LDFLAGS) -o tmux $+ $(LIBS)

depend: 	$(SRCS)
		$(CC) $(CPPFLAGS) $(CFLAGS) -MM $(SRCS) > .depend

clean:
		rm -f tmux *.o .depend *~ *.core *.log compat/*.o

install:	all
		$(INSTALLDIR) $(DESTDIR)$(PREFIX)/bin
		$(INSTALLBIN) tmux $(DESTDIR)$(PREFIX)/bin/tmux
		$(INSTALLDIR) $(DESTDIR)$(PREFIX)/man/man1
		$(INSTALLMAN) tmux.1 $(DESTDIR)$(PREFIX)/man/man1/tmux.1
