#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])

AC_INIT([bctoolbox],[0.5.1],[jehan.monnier@linphone.org])

BCTOOLBOX_SO_CURRENT=1 dnl increment this number when you add/change/remove an interface
BCTOOLBOX_SO_REVISION=1 dnl increment this number when you change source code, without changing interfaces; set to 0 when incrementing CURRENT
BCTOOLBOX_SO_AGE=0 dnl increment this number when you add an interface, set to 0 if you remove an interface

BCTOOLBOX_SO_VERSION=$BCTOOLBOX_SO_CURRENT:$BCTOOLBOX_SO_REVISION:$BCTOOLBOX_SO_AGE

AC_SUBST(BCTOOLBOX_SO_CURRENT, $BCTOOLBOX_SO_CURRENT)
AC_SUBST(BCTOOLBOX_SO_VERSION)

AC_CONFIG_SRCDIR([src/crypto/mbedtls.c])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_SYSTEM

LT_INIT([shared disable-static])

dnl initialize pkg-config so that we can use it within if else fi statements.
PKG_PROG_PKG_CONFIG()

AM_INIT_AUTOMAKE([subdir-objects foreign])
AM_SILENT_RULES(yes)

# Checks for programs.
dnl do not put anythingelse before AC_PROG_CC unless checking if macro still work for clang
dnl because of tunnel library wrapper, C++ is required.
AC_PROG_CXX(["xcrun clang++" g++])
AC_PROG_CC(["xcrun clang" gcc])
AC_PROG_OBJC(["xcrun clang" gcc])

AM_PROG_CC_C_O
case $INSTALL in
	*ginstall*)
		INSTALL="$INSTALL -C"
	;;
esac
dnl AC_PROG_CC_C99


CFLAGS="$CFLAGS "
if test "$debug" = "no" ; then
	CFLAGS="$CFLAGS -g -O2"
else
	CFLAGS="$CFLAGS -g"
fi

CXXFLAGS="$CXXFLAGS -std=c++11"

AC_ARG_ENABLE(strict,
		[  --enable-strict    Turn on strict mode compilation, no warnings allowed (default=yes)],
		[case "${enableval}" in
		yes) strict=true ;;
		no)  strict=false ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --enable-strict) ;;
		esac],[strict=true])

STRICT_OPTIONS="-Wall"

if test "$strict" = "true" ; then
	STRICT_OPTIONS="$STRICT_OPTIONS -Werror -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
fi
AC_SUBST(STRICT_OPTIONS)


# Checks for libraries.

# Checks for header files.
dnl check for argument supplied. If value is yes, no arg was supplied so set path to default value.
AC_ARG_WITH( mbedtls,
      [  --with-mbedtls      Set prefix where mbedtls can be found (ex:/usr or /usr/local)[default=PREFIX] ],
      [case "${withval}" in
		yes)	mbedtls_prefix="/usr" ;;
		*)		mbedtls_prefix=${withval} ;;
	esac],
	[ mbedtls_prefix=${prefix} ])


AC_ARG_WITH( polarssl,
      [  --with-polarssl      Set prefix where polarssl can be found (ex:/usr or /usr/local)[default=PREFIX] ],
      [case "${withval}" in
		yes)	polarssl_prefix="/usr" ;;
		*)		polarssl_prefix=${withval} ;;
	esac],
	[ polarssl_prefix=${prefix} ])

AC_ARG_WITH( bcunit,
      [  --with-bcunit      Set prefix where bcunit can be found (ex:/usr or /usr/local)[default=PREFIX] ],
      [case "${withval}" in
		yes)	bcunit_prefix="/usr/local" ;;
		*)		bcunit_prefix=${withval} ;;
	esac],
	[ bcunit_prefix=${prefix} ])

if test "$mbedtls_prefix" = "NONE" ; then
	mbedtls_prefix=/usr
fi

if test "$polarssl_prefix" = "NONE" ; then
	polarssl_prefix=/usr
fi

if test "$bcunit_prefix" = "NONE" ; then
	bcunit_prefix=/usr/local
fi


if test "$mbedtls_prefix" != "/usr" ; then
	MBEDTLS_CFLAGS="-I${mbedtls_prefix}/include"
	MBEDTLS_LIBS="-L${mbedtls_prefix}/lib"
fi

if test "$bcunit_prefix" != "/usr/local" ; then
	BCUNIT_CFLAGS="-I${bcunit_prefix}/include"
	BCUNIT_LIBS="-L${bcunit_prefix}/lib"
fi


CPPFLAGS_save=$CPPFLAGS
LIBS_save=$LIBS
CPPFLAGS="$CPPFLAGS $BCUNIT_CFLAGS"
LIBS="$LIBS $BCUNIT_LIBS"

AC_CHECK_HEADER(BCUnit/BCUnit.h, [
	AC_CHECK_LIB(bcunit, CU_get_error, [BCUNIT_LIBS="$BCUNIT_LIBS -lbcunit"; bcunit_found=true],
	[bcunit_found=false]) ],
	[bcunit_found=false]
)

if test "$bcunit_found" != "true" ; then
	AC_MSG_ERROR("BCUnit not found")
else

	AC_CHECK_LIB(bcunit, CU_get_suite,
		[ AC_DEFINE(HAVE_CU_GET_SUITE, 1, [Defined when CU_get_suite is available])]
	)
	AC_CHECK_LIB(bcunit, CU_set_trace_handler,
		[ AC_DEFINE(HAVE_CU_SET_TRACE_HANDLER, 1, [Defined when CU_set_trace_handler is available])]
	)
	AC_CHECK_HEADERS(BCUnit/CUCurses.h)
fi

CPPFLAGS=$CPPFLAGS_save
LIBS=$LIBS_save
AC_SUBST(BCUNIT_CFLAGS)
AC_SUBST(BCUNIT_LIBS)

CPPFLAGS_save=$CPPFLAGS
LIBS_save=$LIBS
CPPFLAGS="$CPPFLAGS $MBEDTLS_CFLAGS"
LIBS="$LIBS $MBEDTLS_LIBS"

AC_CHECK_HEADER(mbedtls/ssl.h, [
	AC_CHECK_LIB(mbedtls, mbedtls_pk_parse_key, [mbedtls_found=true; MBEDTLS_LIBS="$MBEDTLS_LIBS -lmbedtls -lmbedcrypto -lmbedx509"], [mbedtls_found=false], [-lmbedcrypto]) ],
	[mbedtls_found=false]
)

CPPFLAGS=$CPPFLAGS_save
LIBS=$LIBS_save

AC_CHECK_LIB(dl, dladdr)


AM_CONDITIONAL(ENABLE_MBEDTLS, test "$mbdetls_found" = "true")

if test "$mbedtls_found" = "false" ; then

	MBEDTLS_CFLAGS=
	MBEDTLS_LIBS=
	if test "$polarssl_prefix" != "/usr" ; then
		POLARSSL_CFLAGS="-I${polarssl_prefix}"
		POLARSSL_LIBS="-L${polarssl_prefix}/lib"
	fi
	CPPFLAGS_save=$CPPFLAGS
	LIBS_save=$LIBS
	CPPFLAGS="$CPPFLAGS $POLARSSL_CFLAGS"
	LIBS="$LIBS $POLARSSL_LIBS"

	AC_CHECK_HEADER(polarssl/ssl.h, [
		AC_CHECK_LIB(polarssl, ssl_read, [polarssl_found=true; POLARSSL_LIBS="$POLARSSL_LIBS -lpolarssl"], [polarssl_found=false]) ],
			[polarssl_found=false]
	)

	if test "$polarssl_found" = "false" ; then
		AC_MSG_ERROR("Neither polarssl nor mbedtls were found.")
	fi
	AC_CHECK_HEADER(polarssl/compat-1.2.h, [polarssl_post_12_found=true], [polarssl_post_12_found=false])

	CPPFLAGS=$CPPFLAGS_save
	LIBS=$LIBS_save
fi

dnl add thread flags
case $host in
	i386-apple*|armv6-apple*|armv7-apple*|armv7s-apple*|aarch64-apple*|arm64-apple*|*-darwin.ios)
		ios_found=yes
	;;
	x86_64-apple*|i686-apple*)
		macosx_found=yes
	;;
	*nto-qnx*)
	;;
	*)
		CFLAGS="$CFLAGS -pthread -D_REENTRANT"
		CXXFLAGS="$CXXFLAGS -pthread -D_REENTRANT"
		LIBS="$LIBS -pthread -lpthread"
	;;
esac


AC_SUBST(MBEDTLS_CFLAGS)
AC_SUBST(MBEDTLS_LIBS)
AC_SUBST(POLARSSL_CFLAGS)
AC_SUBST(POLARSSL_LIBS)


AM_CONDITIONAL(ENABLE_POLARSSL12, test "$polarssl_post_12_found" = "false")
AM_CONDITIONAL(ENABLE_POLARSSL, test "$polarssl_post_12_found" = "true")
AM_CONDITIONAL(ENABLE_MBEDTLS, test "$mbedtls_found" = "true")


dnl substitute variables used by .pc files

if test "$prefix" = "NONE" ; then
	install_prefix=/usr/local
else
	install_prefix=$prefix
fi

PROJECT_VERSION=$VERSION
CMAKE_INSTALL_PREFIX=$install_prefix
CMAKE_INSTALL_FULL_LIBDIR=${libdir}
CMAKE_INSTALL_FULL_INCLUDEDIR=${install_prefix}/include

AC_SUBST(PROJECT_VERSION)
AC_SUBST(CMAKE_INSTALL_PREFIX)
AC_SUBST(CMAKE_INSTALL_FULL_LIBDIR)
AC_SUBST(CMAKE_INSTALL_FULL_INCLUDEDIR)
AC_SUBST(TESTER_REQUIRES_PRIVATE)

LIBS_PRIVATE="$LIBS_PRIVATE $MBEDTLS_LIBS $POLARSSL_LIBS"
AC_SUBST(LIBS_PRIVATE)

AC_CONFIG_FILES(
[
Makefile
include/Makefile
include/bctoolbox/Makefile
src/Makefile
bctoolbox-tester.pc
bctoolbox.pc
])
AC_OUTPUT
