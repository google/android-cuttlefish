## Makefile.am

AM_CPPFLAGS = ${libuuid_CFLAGS} -I$(top_srcdir)/include
AM_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
sbin_PROGRAMS = fsck.f2fs
noinst_HEADERS = common.h dict.h dqblk_v2.h f2fs.h fsck.h node.h quotaio.h \
		quotaio_tree.h quotaio_v2.h xattr.h compress.h inject.h
include_HEADERS = $(top_srcdir)/include/quota.h
fsck_f2fs_SOURCES = main.c fsck.c dump.c mount.c defrag.c resize.c \
		node.c segment.c dir.c sload.c xattr.c compress.c \
		dict.c mkquota.c quotaio.c quotaio_tree.c quotaio_v2.c \
		inject.c
fsck_f2fs_LDADD = ${libselinux_LIBS} ${libuuid_LIBS} \
	${liblzo2_LIBS} ${liblz4_LIBS} ${libwinpthread_LIBS} \
	$(top_builddir)/lib/libf2fs.la

install-data-hook:
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/dump.f2fs
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/defrag.f2fs
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/resize.f2fs
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/sload.f2fs
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/f2fslabel
	ln -sf fsck.f2fs $(DESTDIR)/$(sbindir)/inject.f2fs
