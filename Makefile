# dxp2800-fan - fan control for the UGREEN NASync DXP2800 on FreeBSD
#
# Works with BSD make (FreeBSD's make) and GNU make.
#
#   make
#   make install                       # into /usr/local
#   make install PREFIX=/opt DESTDIR=/tmp/stage
#   make uninstall

PREFIX?=	/usr/local
MANDIR?=	${PREFIX}/share/man/man8
CC?=		cc
CFLAGS?=	-O2
INSTALL?=	install

PROGS=		itefan fancurve fancurve.rc

all: ${PROGS}

itefan: itefan.c
	${CC} ${CFLAGS} -Wall -Wextra ${LDFLAGS} -o itefan itefan.c

fancurve: fancurve.sh.in
	sed 's|%%PREFIX%%|${PREFIX}|g' fancurve.sh.in > fancurve

fancurve.rc: rc.d/fancurve.in
	sed 's|%%PREFIX%%|${PREFIX}|g' rc.d/fancurve.in > fancurve.rc

install: all
	${INSTALL} -d ${DESTDIR}${PREFIX}/sbin ${DESTDIR}${PREFIX}/etc/rc.d \
	    ${DESTDIR}${MANDIR}
	${INSTALL} -m 555 itefan ${DESTDIR}${PREFIX}/sbin/itefan
	${INSTALL} -m 555 fancurve ${DESTDIR}${PREFIX}/sbin/fancurve
	${INSTALL} -m 555 fancurve.rc ${DESTDIR}${PREFIX}/etc/rc.d/fancurve
	${INSTALL} -m 444 man/itefan.8 man/fancurve.8 ${DESTDIR}${MANDIR}/

uninstall:
	rm -f ${DESTDIR}${PREFIX}/sbin/itefan ${DESTDIR}${PREFIX}/sbin/fancurve \
	    ${DESTDIR}${PREFIX}/etc/rc.d/fancurve \
	    ${DESTDIR}${MANDIR}/itefan.8 ${DESTDIR}${MANDIR}/fancurve.8

clean:
	rm -f ${PROGS}

.PHONY: all install uninstall clean
