AC_INIT(include/boost/asio.hpp)
AM_MAINTAINER_MODE
AM_INIT_AUTOMAKE(helix, [0.1])

AC_CANONICAL_HOST
AM_PROG_CC_C_O
AC_PROG_CXX
AC_LANG(C++)
AC_PROG_RANLIB

AC_DEFINE(_REENTRANT, [1], [Define this])

#CFLAGS="$CFLAGS -g -D_GLIBCXX_DEBUG -DDISABLE_DNADB"
#CXXFLAGS="$CXXFLAGS -g -D_GLIBCXX_DEBUG -DDISABLE_DNADB"

CFLAGS="$CFLAGS -O2 -DNDEBUG"
CXXFLAGS="$CXXFLAGS -O2 -DNDEBUG"

dnl Check for pthreads and boost libraries.
ACX_PTHREAD

AX_BOOST_DATE_TIME

dnl check that Boost.DateTime was found:
if test -z "$BOOST_DATE_TIME_LIB"; then
   AC_MSG_ERROR([Unable to find Boost.DateTime library, currently this is required.])
fi

AX_BOOST_THREAD

dnl check that Boost.Thread was found:
if test -z "$BOOST_THREAD_LIB"; then
   AC_MSG_ERROR([Unable to find Boost.Thread library, currently this is required.])
fi

AX_BOOST_PROGRAM_OPTIONS

dnl check that Boost.Program_options was found:
if test -z "$BOOST_PROGRAM_OPTIONS_LIB"; then
   AC_MSG_ERROR([Unable to find Boost.Program_options library, currently this is required.])
fi


AX_BOOST_REGEX

dnl check that Boost.regex was found:
if test -z "$BOOST_REGEX_LIB"; then
   AC_MSG_ERROR([Unable to find Boost.regex library, currently this is required.])
fi


AC_ARG_WITH(epoll,
  AC_HELP_STRING([--without-epoll],[disable support for epoll]),
[
  CPPFLAGS="$CPPFLAGS -DBOOST_ASIO_DISABLE_EPOLL"
],
[])


AC_ARG_WITH(mysql,
  AC_HELP_STRING([--with-mysql=DIR],[location of mysql]),
[
  CPPFLAGS="$CPPFLAGS -I${withval}"
],
[
  CPPFLAGS="$CPPFLAGS -I/usr/include/mysql"
])

AC_CHECK_HEADER([mysql/mysql.h],,
[
  echo "Can't find mysql headers. Please check the location of the"
  echo "mysql distribution and rerun configure using the"
  echo "--with-mysql=DIR option."
  exit 1
],[])

AC_ARG_WITH(mysql++,
  AC_HELP_STRING([--with-mysql++=DIR],[location of mysql++]),
[
  CPPFLAGS="$CPPFLAGS -I${withval}"
],
[
  CPPFLAGS="$CPPFLAGS -I/usr/include/mysql++"
])

AC_CHECK_HEADER([mysql++/mysql++.h],,
[
  echo "Can't find mysql++ headers. Please check the location of the"
  echo "mysql++ distribution and rerun configure using the"
  echo "--with-mysql++=DIR option."
  exit 1
],[])

LIBS="$LIBS -lmysqlpp"

AC_ARG_WITH(openssl,
  AC_HELP_STRING([--with-openssl=DIR],[location of openssl]),
[
  CPPFLAGS="$CPPFLAGS -I${withval}"
],[])

AC_CHECK_HEADER([openssl/ssl.h],,
[
  OPENSSL_FOUND=no
],[])

if test x$OPENSSL_FOUND != xno; then
  LIBS="$LIBS -lssl -lcrypto"
fi

AM_CONDITIONAL(HAVE_OPENSSL,test x$OPENSSL_FOUND != xno)

WINDOWS=no
case $host in
  *-*-linux*)
    CXXFLAGS="$CXXFLAGS -pthread"
    LDFLAGS="$LDFLAGS -pthread"
    ;;
  *-*-solaris*)
    if test "$GXX" = yes; then
      CXXFLAGS="$CXXFLAGS"
    else
      # We'll assume Sun's CC.
      CXXFLAGS="$CXXFLAGS -mt"
    fi
    LIBS="$LIBS -lsocket -lnsl -lpthread"
    ;;
  *-*-mingw32*)
    CXXFLAGS="$CXXFLAGS -mthreads"
    LDFLAGS="$LDFLAGS -mthreads"
    LIBS="$LIBS -lws2_32 -lmswsock"
    WINDOWS=yes
    ;;
  *-pc-cygwin*)
    CXXFLAGS="$CXXFLAGS -D__USE_W32_SOCKETS -D_WIN32_WINNT=0x0500"
    LIBS="$LIBS -lws2_32 -lmswsock"
    WINDOWS=yes
    ;;
  *-apple-darwin*)
    CXXFLAGS="$CXXFLAGS"
    LDFLAGS="$LDFLAGS"
    ;;
  *-*-freebsd*)
    CXXFLAGS="$CXXFLAGS -pthread"
    LDFLAGS="$LDFLAGS -pthread"
    ;;
esac

if test "$GXX" = yes; then
  CXXFLAGS="$CXXFLAGS -ftemplate-depth-256"
fi

AC_CHECK_PROG(DOXYGEN_FOUND,doxygen,yes)
AM_CONDITIONAL(HAVE_DOXYGEN,test x$DOXYGEN_FOUND = xyes)
AC_CHECK_PROG(HAVE_DOT,dot,YES,NO)
AC_SUBST(HAVE_DOT)
AM_CONDITIONAL(WINDOWS_TARGET,test x$WINDOWS != xno)

AC_OUTPUT([
  Makefile
  include/Makefile
  src/Makefile
  helix/Makefile
  gattaca/Makefile])
