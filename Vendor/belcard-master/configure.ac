#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([belcard], [1.0.1], [sylvain.berfini@belledonne-communications.com])

BELCARD_SO_CURRENT=0 dnl increment this number when you add/change/remove an interface
BELCARD_SO_REVISION=0 dnl increment this number when you change source code, without changing interfaces; set to 0 when incrementing CURRENT
BELCARD_SO_AGE=1 dnl increment this number when you add an interface, set to 0 if you remove an interface

BELCARD_SO_VERSION=$BELCARD_SO_CURRENT:$BELCARD_SO_REVISION:$BELCARD_SO_AGE

AC_SUBST(BELCARD_SO_CURRENT, $BELCARD_SO_CURRENT)
AC_SUBST(BELCARD_SO_VERSION)

AC_CONFIG_SRCDIR([src/])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_SYSTEM

LT_INIT([shared disable-static])

dnl initialize pkg-config so that we can use it within if else fi statements.
PKG_PROG_PKG_CONFIG()

AM_INIT_AUTOMAKE(subdir-objects foreign)
AM_SILENT_RULES(yes)

# Checks for programs.
AC_PROG_CXX

CXXFLAGS="$CXXFLAGS -std=c++11 -Wall -Werror -Wextra -Wno-unused-parameter"

# Checks for libraries.
dnl ##################################################
dnl # Check for XXD
dnl ##################################################

AC_CHECK_PROG(xxd_found, xxd, yes)
if test "$xxd_found" != yes ;then
        AC_MSG_ERROR("xxd is required (provided by vim package)")
fi

dnl ##################################################
dnl # Check for BcToolbox
dnl ##################################################

dnl check bctoolbox
PKG_CHECK_MODULES(BCTOOLBOX, [bctoolbox],[libbctoolbox_found=yes],foo=bar)
if test "$libbctoolbox_found" != "yes" ; then
        dnl Check the lib presence in case the PKG-CONFIG version is not found
        AC_LANG_CPLUSPLUS
        AC_CHECK_LIB(bctoolbox, main, [BCTOOLBOX_LIBS+=" -lbctoolbox"; libbctoolbox_found=yes], [foo=bar])
        AC_LANG_C
fi
if test "$libbctoolbox_found" != "yes" ; then
        AC_MSG_ERROR([libbctoolbox not found. Install it and try again])
fi

AC_SUBST(BCTOOLBOX_LIBS)

dnl check bctoolbox-tester
PKG_CHECK_MODULES(BCTOOLBOXTESTER, [bctoolbox-tester],[libbctoolbox_tester_found=yes],foo=bar)
if test "$libbctoolbox_tester_found" != "yes" ; then
        dnl Check the lib presence in case the PKG-CONFIG version is not found
        AC_LANG_CPLUSPLUS
        AC_CHECK_LIB(bctoolbox-tester, main, [BCTOOLBOXTESTER_LIBS+=" -lbctoolbox-tester"; libbctoolbox_tester_found=yes], [foo=bar])
        AC_LANG_C
fi
if test "$libbctoolbox_tester_found" != "yes" ; then
        AC_MSG_ERROR([libbctoolbox tester not found. Install it and try again])
fi

AC_SUBST(BCTOOLBOXTESTER_LIBS)

dnl ##################################################
dnl # Check for Belr
dnl ##################################################

dnl check belr
PKG_CHECK_MODULES(BELR, [belr],[libbelr_found=yes],foo=bar)
if test "$libbelr_found" != "yes" ; then
	dnl Check the lib presence in case the PKG-CONFIG version is not found
	AC_LANG_CPLUSPLUS
	AC_CHECK_LIB(belr, main, [BELR_LIBS+=" -lbelr"; libbelr_found=yes], [foo=bar])
	AC_LANG_C
fi
if test "$libbelr_found" != "yes" ; then
	AC_MSG_ERROR([libbelr not found. Install it and try again])
fi

AC_SUBST(BELR_CFLAGS)
AC_SUBST(BELR_LIBS)

dnl ##################################################
dnl # Check for BCUnit
dnl ##################################################

PKG_CHECK_MODULES(BCUNIT, bcunit, [found_bcunit=yes],[found_bcunit=no])

if test "$found_bcunit" = "no" ; then
	AC_CHECK_HEADERS(BCUnit/BCUnit.h,
		[
			AC_CHECK_LIB(bcunit,CU_add_suite,[
				found_bcunit=yes
				BCUNIT_LIBS+=" -lbcunit"
			])
		])
fi
case "$target_os" in
	*darwin*)
		#hack for macport
		BCUNIT_LIBS+=" -lncurses"
	;;
	*mingw*)
		CPPFLAGS="$CPPFLAGS -D_WIN32_WINNT=0x0501"
		LIBS="$LIBS -lws2_32 -liphlpapi"
		LDFLAGS="$LDFLAGS -Wl,--export-all-symbols"
	;;
esac

if test "$found_bcunit" = "no" ; then
	AC_MSG_WARN([Could not find bcunit framework, tests are not compiled.])
else
	AC_CHECK_LIB(bcunit,CU_get_suite,[
					AC_DEFINE(HAVE_CU_GET_SUITE,1,[defined when CU_get_suite is available])
					],[foo=bar],[$BCUNIT_LIBS])
	AC_CHECK_LIB(bcunit,CU_curses_run_tests,[
					AC_DEFINE(HAVE_CU_CURSES,1,[defined when CU_curses_run_tests is available])
					],[foo=bar],[$BCUNIT_LIBS])
fi

AM_CONDITIONAL(BUILD_TESTS,test x$found_bcunit = xyes)

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SSIZE_T

dnl substitute variables used by .pc files
if test "$prefix" = "NONE" ; then
	install_prefix=/usr
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

# Checks for library functions.
AC_CONFIG_FILES(
Makefile
src/Makefile
tester/Makefile
tools/Makefile
include/Makefile
include/belcard/Makefile
cmake/BelcardConfig.cmake
belcard.pc
)

AC_OUTPUT
