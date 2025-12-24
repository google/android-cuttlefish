#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])

# Get version from file VERSION
m4_define([f2fs_tools_version], m4_esyscmd([sed -n '1p' VERSION | tr -d '\n']))
m4_define([f2fs_tools_date], m4_esyscmd([sed -n '2p' VERSION | tr -d '\n']))
m4_define([f2fs_tools_gitdate],
		m4_esyscmd([git log -1 --pretty=format:%ci 2> /dev/null]))

AC_INIT([F2FS tools], [f2fs_tools_version],
			[linux-f2fs-devel@lists.sourceforge.net])

AM_SILENT_RULES([yes])

AC_DEFINE([F2FS_TOOLS_VERSION], "f2fs_tools_version", [f2fs-tools version])
AC_DEFINE([F2FS_MAJOR_VERSION], m4_bpatsubst(f2fs_tools_version,
				[\([0-9]*\)\(\w\|\W\)*], [\1]),
				[Major version for f2fs-tools])
AC_DEFINE([F2FS_MINOR_VERSION], m4_bpatsubst(f2fs_tools_version,
				[\([0-9]*\).\([0-9]*\)\(\w\|\W\)*], [\2]),
				[Minor version for f2fs-tools])

AS_IF([test -d .git],[
	AC_DEFINE([F2FS_TOOLS_DATE],
		"m4_bpatsubst(f2fs_tools_gitdate,
		[\([0-9-]*\)\(\w\|\W\)*], [\1])",
		[f2fs-tools date based on Git commits])],[
	AC_DEFINE([F2FS_TOOLS_DATE],
		"f2fs_tools_date",
		[f2fs-tools date based on Source releases])])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign tar-pax dist-xz])

# Test configure options.
AC_ARG_WITH([selinux],
	[AS_HELP_STRING([--without-selinux],
	  [Ignore presence of libselinux and disable selinux support])],
	[],
	[with_selinux=check])

AC_ARG_WITH([blkid],
	[AS_HELP_STRING([--without-blkid],
	  [Ignore presence of libblkid and disable blkid support])],
	[],
	[with_blkid=check])

AC_ARG_WITH([lzo2],
	[AS_HELP_STRING([--without-lzo2],
	  [Ignore presence of liblzo2 and disable lzo2 support])],
	[],
	[with_lzo2=check])

AC_ARG_WITH([lz4],
	[AS_HELP_STRING([--without-lz4],
	  [Ignore presence of liblz4 and disable lz4 support])],
	[],
	[with_lz4=check])

# Checks for programs.
AC_PROG_CC
AM_PROG_AR
LT_INIT
AC_PATH_PROG([LDCONFIG], [ldconfig],
       [AC_MSG_ERROR([ldconfig not found])],
       [$PATH:/sbin])

# Checks for libraries.
AS_IF([test "x$with_blkid" != xno],
	[AC_CHECK_LIB([blkid], [blkid_probe_all],
		[AC_SUBST([libblkid_LIBS], ["-lblkid"])
			AC_DEFINE([HAVE_LIBBLKID], [1],
			[Define if you have libblkid])
		],
		[if test "x$with_blkid" != xcheck; then
			AC_MSG_FAILURE(
                [--with-blkid was given, but test for blkid failed])
        fi
	], -lblkid)])

AS_IF([test "x$with_lzo2" != xno],
	[AC_CHECK_LIB([lzo2], [main],
		[AC_SUBST([liblzo2_LIBS], ["-llzo2"])
			AC_DEFINE([HAVE_LIBLZO2], [1],
			[Define if you have liblzo2])
		],
		[if test "x$with_lzo2" != xcheck; then
			AC_MSG_FAILURE(
                [--with-lzo2 was given, but test for lzo2 failed])
        fi
	], -llzo2)])

AS_IF([test "x$with_lz4" != xno],
	[AC_CHECK_LIB([lz4], [main],
		[AC_SUBST([liblz4_LIBS], ["-llz4"])
			AC_DEFINE([HAVE_LIBLZ4], [1],
			[Define if you have liblz4])
		],
		[if test "x$with_lz4" != xcheck; then
			AC_MSG_FAILURE(
                [--with-lz4 was given, but test for lz4 failed])
        fi
	], -llz4)])

AS_IF([test "x$with_selinux" != xno],
	[AC_CHECK_LIB([selinux], [getcon],
		[AC_SUBST([libselinux_LIBS], ["-lselinux"])
			AC_DEFINE([HAVE_LIBSELINUX], [1],
			[Define if you have libselinux])
		],
		[if test "x$with_selinux" != xcheck; then
			AC_MSG_FAILURE(
				[--with-selinux was given, but test for selinux failed])
		fi
	], -lselinux)])

AC_CHECK_LIB([uuid], [uuid_clear],
	[AC_SUBST([libuuid_LIBS], ["-luuid"])
		AC_DEFINE([HAVE_LIBUUID], [1],
		[Define if you have libuuid])
	], [], [])

AC_CHECK_LIB([winpthread], [clock_gettime],
	[AC_SUBST([libwinpthread_LIBS], ["-lwinpthread"])
		AC_DEFINE([HAVE_LIBWINPTHREAD], [1],
		[Define if you have libwinpthread])
	], [], [])

# Checks for header files.
AC_CHECK_HEADERS(m4_flatten([
	attr/xattr.h
	blkid/blkid.h
	byteswap.h
	fcntl.h
	kernel/uapi/linux/blkzoned.h
	linux/blkzoned.h
	linux/rw_hint.h
	linux/fcntl.h
	linux/falloc.h
	linux/fiemap.h
	linux/fs.h
	linux/hdreg.h
	linux/limits.h
	linux/loop.h
	linux/major.h
	linux/posix_acl.h
	linux/types.h
	linux/xattr.h
	mach/mach_time.h
	mntent.h
	pthread_time.h
	scsi/sg.h
	selinux/android.h
	selinux/selinux.h
	sparse/sparse.h
	stdlib.h
	string.h
	sys/acl.h
	sys/ioctl.h
	sys/mount.h
	sys/stat.h
	sys/syscall.h
	sys/sysmacros.h
	sys/utsname.h
	sys/xattr.h
	unistd.h
	uuid/uuid.h
]))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_GETMNTENT
AC_CHECK_FUNCS_ONCE([
	add_key
	clock_gettime
	fallocate
	fsetxattr
	fstat
	fstat64
	fsync
	getgid
	getmntent
	getuid
	keyctl
	memset
	pread
	pwrite
	setmntent
])

AS_IF([test "$ac_cv_header_byteswap_h" = "yes"],
      [AC_CHECK_DECLS([bswap_64],,,[#include <byteswap.h>])])

AC_MSG_CHECKING([for CLOCK_BOOTIME])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#include <time.h>
#ifdef HAVE_PTHREAD_TIME_H
#include <pthread_time.h>
#endif
],[return CLOCK_BOOTTIME])],
		  [AC_MSG_RESULT([yes])
		  AC_DEFINE([HAVE_CLOCK_BOOTTIME], [1],
		  [Define if CLOCK_BOOTTIME is available])],
		  [AC_MSG_RESULT([no])])

# AC_CANONICAL_HOST is needed to access the 'host_os' variable
AC_CANONICAL_HOST

build_linux=no
build_windows=no
build_mac=no

# Detect the target system
case "${host_os}" in
linux*|uclinux*)
	build_linux=yes
	;;
cygwin*|mingw*)
	build_windows=yes
	;;
darwin*)
	build_mac=yes
	;;
*)
	AC_MSG_ERROR(["OS $host_os is not supported"])
	;;
esac

# Pass the conditionals to automake
AM_CONDITIONAL([LINUX], [test "$build_linux" = "yes"])
AM_CONDITIONAL([WINDOWS], [test "$build_windows" = "yes"])
AM_CONDITIONAL([OSX], [test "$build_mac" = "yes"])

# Install directories
#AC_PREFIX_DEFAULT([/usr])
#AC_SUBST([sbindir], [/sbin])
#AC_SUBST([sysconfdir], [/etc])
#AC_SUBST([localstatedir], [/var])

AC_ARG_WITH([root-libdir],
[  --with-root-libdir=DIR override location for /lib/libf2fs.so],
root_libdir=$withval,
root_libdir=NONE)dnl

if test "$root_libdir" = NONE ; then
   root_libdir="$libdir"
fi
AC_SUBST(root_libdir)

AC_CONFIG_FILES([
	Makefile
	man/Makefile
	lib/Makefile
	mkfs/Makefile
	fsck/Makefile
	tools/Makefile
	tools/f2fs_io/Makefile
])

AC_CHECK_MEMBER([struct blk_zone.capacity],
		[AC_DEFINE(HAVE_BLK_ZONE_REP_V2, [1], [report zones includes zone capacity])],
		[], [[
#ifdef HAVE_KERNEL_UAPI_LINUX_BLKZONED_H
#include <kernel/uapi/linux/blkzoned.h>
#elif defined(HAVE_LINUX_BLKZONED_H)
#include <linux/blkzoned.h>
#endif
		]])

# export library version info for mkfs/libf2fs_format_la
AC_SUBST(FMT_CURRENT, 9)
AC_SUBST(FMT_REVISION, 0)
AC_SUBST(FMT_AGE, 0)

# export library version info for lib/libf2fs_la
AC_SUBST(LIBF2FS_CURRENT, 10)
AC_SUBST(LIBF2FS_REVISION, 0)
AC_SUBST(LIBF2FS_AGE, 0)

AH_BOTTOM([
#ifndef _CONFIG_H_
#define _CONFIG_H_

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifndef HAVE_GETUID
static inline unsigned int getuid(void) { return -1; }
#endif
#ifndef HAVE_GETGID
static inline unsigned int getgid(void) { return -1; }
#endif

#ifndef S_ISLNK
#define S_ISLNK(mode) false
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(mode) false
#endif

#endif
])

AC_OUTPUT
