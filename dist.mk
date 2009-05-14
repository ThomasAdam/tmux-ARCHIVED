# $Id$

VERSION= 0.8

DISTDIR= tmux-${VERSION}
DISTFILES= *.[ch] Makefile GNUmakefile configure tmux.1 \
	NOTES TODO CHANGES FAQ \
	`find examples compat -type f -and ! -path '*CVS*'`

dist:          
		make clean
		grep '^#FDEBUG=' Makefile
		grep '^#FDEBUG=' GNUmakefile
		[ "`(grep '^VERSION' Makefile; grep '^VERSION' GNUmakefile)| \
		        uniq -u`" = "" ]
		tar -zc \
		        -s '/.*/${DISTDIR}\/\0/' \
		        -f ${DISTDIR}.tar.gz ${DISTFILES}

upload-index.html: update-index.html
		scp index.html images/*.png \
		        nicm,tmux@web.sf.net:/home/groups/t/tm/tmux/htdocs
		rm -f images/small-*

update-index.html:
		(cd images && \
		        rm -f small-* && \
		        for i in *.png; do \
		        convert "$$i" -resize 200x150 "small-$$i"; \
		        done \
		)
		sed "s/%%VERSION%%/${VERSION}/g" index.html.in >index.html
