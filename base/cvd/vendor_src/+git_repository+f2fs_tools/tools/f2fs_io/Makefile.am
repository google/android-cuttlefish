## Makefile.am

if LINUX
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
sbin_PROGRAMS = f2fs_io
f2fs_io_SOURCES = f2fs_io.c
endif
