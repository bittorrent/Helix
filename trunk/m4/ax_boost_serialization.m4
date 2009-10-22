dnl @synopsis AX_BOOST_SERIALIZATION
dnl
dnl This macro checks to see if the Boost.Serialization library is
dnl installed. It also attempts to guess the currect library name using
dnl several attempts. It tries to build the library name using a user
dnl supplied name or suffix and then just the raw library.
dnl
dnl If the library is found, HAVE_BOOST_SERIALIZATION is defined and
dnl BOOST_SERIALIZATION_LIB is set to the name of the library.
dnl
dnl This macro calls AC_SUBST(BOOST_SERIALIZATION_LIB).
dnl
dnl @category InstalledPackages
dnl @author Michael Tindal <mtindal@paradoxpoint.com>
dnl @version 2004-09-20
dnl @license GPLWithACException

AC_DEFUN([AX_BOOST_SERIALIZATION],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::Serialization library is available,
ax_cv_boost_serialization,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <boost/serialization/vector.hpp>]],
			           [[boost::serialization::access tst; return 0;]]),
  	           ax_cv_boost_serialization=yes, ax_cv_boost_serialization=no)
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_serialization" = yes; then
  AC_DEFINE(HAVE_BOOST_SERIALIZATION,,[define if the Boost::Serialization library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-serialization],AS_HELP_STRING([--with-boost-serialization],
  [specify the boost serialization library or suffix to use]),
  [if test "x$with_boost_serialization" != "xno"; then
    ax_serialization_lib=$with_boost_serialization
    ax_boost_serialization_lib=boost_serialization-$with_boost_serialization
  fi])
  for ax_lib in $ax_serialization_lib $ax_boost_serialization_lib boost_serialization; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_SERIALIZATION_LIB=$ax_lib
break])
  done
  AC_SUBST(BOOST_SERIALIZATION_LIB)
fi
])dnl
