## Makefile.am

AM_CPPFLAGS = ${libuuid_CFLAGS} -I$(top_srcdir)/include
AM_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
sbin_PROGRAMS =
if !WINDOWS
sbin_PROGRAMS += fibmap.f2fs parse.f2fs
endif
fibmap_f2fs_SOURCES = fibmap.c
parse_f2fs_SOURCES = f2fs_io_parse.c

if LINUX
sbin_PROGRAMS += f2fscrypt
f2fscrypt_SOURCES = f2fscrypt.c sha512.c
f2fscrypt_LDFLAGS = ${libuuid_LIBS}
dist_man_MANS = f2fscrypt.8
endif

SUBDIRS = f2fs_io
