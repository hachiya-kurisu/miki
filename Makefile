VERSION = 0.4
OS != uname -s

-include Makefile.$(OS)

CFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS += -DNAME=\"miki\"
CFLAGS += -Wall -Wextra -std=c99 -pedantic -O2

LDFLAGS += -lm

PREFIX ?= /usr/local
MANDIR ?= /share/man

all: miki

config.h:
	@test -f $@ || cp config.def.h $@

miki: config.h src/miki.c Makefile
	${CC} ${CFLAGS} -o $@ src/miki.c ${LDFLAGS} ${LIBS}
	strip $@

install:
	install miki -m 755 ${DESTDIR}${PREFIX}/bin/miki
	install miki.rc -m 644 /etc/rc.d/miki

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/miki
	rm -f /etc/rc.d/miki

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
