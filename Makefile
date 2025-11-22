VERSION = 0.5
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

.PHONY: all install lint doc push clean again release pkg

PREFIX ?= /usr/local
MANDIR ?= /usr/local/man

all: miki

miki: src/miki.c Makefile
	${CC} ${CFLAGS} -o $@ src/miki.c ${LDFLAGS} ${LIBS}
	strip $@

install:
	install -d ${DESTDIR}${PREFIX}/bin
	install -d ${DESTDIR}/etc/rc.d
	install -d ${DESTDIR}${MANDIR}/man8
	install -m 644 miki.8 ${DESTDIR}${MANDIR}/man8/miki.8
	install -m 755 miki ${DESTDIR}${PREFIX}/bin/miki
	install -m 555 miki.rc ${DESTDIR}/etc/rc.d/miki

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/miki
	rm -f ${DESTDIR}${MANDIR}/man8/miki.8
	rm -f ${DESTDIR}/etc/rc.d/miki

pkg:
	@[ `uname` = OpenBSD ] || { echo "requires openbsd"; exit 1; }
	rm -rf /tmp/pkg
	make install DESTDIR=/tmp/pkg PREFIX=/usr/local
	pkg_create \
		-D COMMENT="nex server for lonely late hours in the city" \
		-D MAINTAINER="kurisu@blekksprut.net" \
		-D HOMEPAGE="https://blekksprut.net/miki" \
		-D FULLPKGPATH=net/miki \
		-D FULLPKGNAME=miki-${VERSION} \
		-d pkg/DESCR \
		-f pkg/PLIST \
		-B /tmp/pkg \
		-p /usr/local \
		miki-${VERSION}.tgz

lint:
	cppcheck ${LINTFLAGS} src/*.c

README.md: README.gmi
	sisyphus -f markdown <README.gmi >README.md

doc: README.md

test:
	@${CC} ${CFLAGS} ${LDFLAGS} -o daytime_test src/daytime_test.c ${LIBS}
	@./daytime_test
	@rm -f daytime_test

push:
	got send
	git push github

clean:
	rm -f miki

again: clean all

release:
	if [ `uname` = OpenBSD ]; then \
		$(MAKE) pkg && \
		cp miki-${VERSION}.tgz /var/www/blekksprut.net/miki/ && \
		ln -sf miki-${VERSION}.tgz /var/www/blekksprut.net/miki/miki-latest.tgz; \
	fi
	git push github --tags
