VERSION = 0.1
OS != uname -s

-include Makefile.$(OS)

CFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS += -DNAME=\"miki\"
CFLAGS += -Wall -Wextra -std=c99 -pedantic

PREFIX ?= /usr/local
MANDIR ?= /share/man

all: miki

config.h:
	cp config.def.h $@

miki: config.h src/miki.c
	${CC} ${CFLAGS} ${LDFLAGS} -L. -o $@ src/miki.c ${LIBS}
	strip $@

install:
	install miki ${DESTDIR}${PREFIX}/bin/miki
	install miki.rc /etc/rc.d/miki

README.md: README.gmi
	sisyphus -f markdown <README.gmi >README.md

doc: README.md

push:
	got send
	git push github
 
clean:
	rm -f miki

again: clean all

release: push
	git push github --tags
