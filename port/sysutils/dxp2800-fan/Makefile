PORTNAME=	dxp2800-fan
DISTVERSIONPREFIX=	v
DISTVERSION=	0.3
CATEGORIES=	sysutils

MAINTAINER=	pdillinger@gmail.com
COMMENT=	Fan control for the UGREEN NASync DXP2800 (ITE IT8613E)
WWW=		https://github.com/GH_USER/dxp2800-fan

LICENSE=	BSD2CLAUSE
LICENSE_FILE=	${WRKSRC}/LICENSE

ONLY_FOR_ARCHS=	amd64 i386
ONLY_FOR_ARCHS_REASON=	uses x86 port I/O through /dev/io

USE_GITHUB=	yes
GH_ACCOUNT=	GH_USER

MAKE_ARGS=	PREFIX=${PREFIX}

OPTIONS_DEFINE=		SMART
OPTIONS_DEFAULT=	SMART
SMART_DESC=		Read disk temperatures with smartmontools
SMART_RUN_DEPENDS=	smartctl:sysutils/smartmontools

PLIST_FILES=	etc/rc.d/fancurve \
		sbin/fancurve \
		sbin/itefan \
		share/man/man8/fancurve.8.gz \
		share/man/man8/itefan.8.gz

post-install:
	${STRIP_CMD} ${STAGEDIR}${PREFIX}/sbin/itefan

.include <bsd.port.mk>
