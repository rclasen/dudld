# Process this file with autoconf to produce a configure script.
AC_INIT
AM_INIT_AUTOMAKE(dudld,0.1.20)
AC_CONFIG_HEADER([config.h])

dnl DBDIR=pgdb
dnl AC_SUBST(DBDIR)

# Checks for programs.
AC_PROG_CC
AC_PROG_YACC
AC_PROG_RANLIB
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB([lockfile], [lockfile_create])

AC_PATH_PROGS([PG_CONFIG], [pg_config])
if test "$PG_CONFIG"x = ""x; then
	AC_MSG_ERROR([Cannot locate postgresql pg_config])
else
	PSQL_CFLAGS="-I`pg_config --includedir`"
	PSQL_LIBS="-L`pg_config --libdir` -lpq"
	AC_SUBST(PSQL_CFLAGS)
	AC_SUBST(PSQL_LIBS)
fi

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.12.4])
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

PKG_CHECK_MODULES([GSTREAMER], [gstreamer-0.8 >= 0.8.12])
AC_SUBST(GSTREAMER_LIBS)
AC_SUBST(GSTREAMER_CFLAGS)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stddef.h stdlib.h string.h sys/param.h sys/socket.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([dup2 inet_ntoa memmove select socket strchr strcspn strdup strncasecmp strpbrk strspn strtol])

AM_MAINTAINER_MODE

AC_CONFIG_FILES([Makefile commondb/Makefile pgdb/Makefile ])
AC_OUTPUT
