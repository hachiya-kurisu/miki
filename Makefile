VERSION = 0.4
OS != uname -s

-include Makefile.$(OS)

CFLAGS += -DVERSION=\"${VERSION}\"
CFLAGS += -DNAME=\"miki\"
CFLAGS += -Wall -Wextra -std=c99 -pedantic -Wformat=2
CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=2
CFLAGS += -Wshadow -Wcast-align -Wstrict-prototypes
CFLAGS += -Wwrite-strings -Wconversion -Wformat-security

LINTFLAGS += --enable=all --inconclusive --language=c --library=posix
LINTFLAGS += --quiet --suppress=missingIncludeSystem
LINTFLAGS += --suppress=getpwnamCalled --suppress=getgrnamCalled

LIBS += -lm

.PHONY: all install lint doc push clean again release

PREFIX ?= /usr/local
MANDIR ?= /share/man

all: miki

miki: src/miki.c Makefile
	${CC} ${CFLAGS} -o $@ src/miki.c ${LDFLAGS} ${LIBS}
	strip $@

install:
	install miki -m 755 ${DESTDIR}${PREFIX}/bin/miki
	install miki.rc -m 644 /etc/rc.d/miki

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/miki
	rm -f /etc/rc.d/miki

lint:
	cppcheck ${LINTFLAGS} src/*.c

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
