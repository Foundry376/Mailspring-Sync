

lib_LTLIBRARIES=libbctoolbox.la libbctoolbox-tester.la

libbctoolbox_la_SOURCES= vconnect.c \
					vfs.c \
					utils/port.c \
					utils/exception.cc \
					logging/logging.c \
					containers/list.c \
					containers/map.cc \
					parser.c

if ENABLE_POLARSSL

libbctoolbox_la_SOURCES += crypto/polarssl.c

endif

if ENABLE_POLARSSL12

libbctoolbox_la_SOURCES += crypto/polarssl1.2.c

endif

if ENABLE_MBEDTLS

libbctoolbox_la_SOURCES += crypto/mbedtls.c

endif

AM_CFLAGS = $(STRICT_OPTIONS) $(POLARSSL_CFLAGS) $(MBEDTLS_CFLAGS)
AM_CXXFLAGS = $(STRICT_OPTIONS)

libbctoolbox_la_LIBADD = $(POLARSSL_LIBS) $(MBEDTLS_LIBS)

libbctoolbox_la_LDFLAGS = -version-info $(BCTOOLBOX_SO_VERSION) -no-undefined



libbctoolbox_tester_la_SOURCES = tester.c utils.h
libbctoolbox_tester_la_LIBADD = libbctoolbox.la $(BCUNIT_LIBS)
libbctoolbox_tester_la_LDFLAGS= -version-info $(BCTOOLBOX_SO_VERSION) -no-undefined
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src $(BCUNIT_CFLAGS)
