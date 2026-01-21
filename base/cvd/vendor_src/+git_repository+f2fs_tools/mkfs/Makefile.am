## Makefile.am

AM_CPPFLAGS = ${libuuid_CFLAGS} ${libblkid_CFLAGS} -I$(top_srcdir)/include
AM_CFLAGS = -Wall -DWITH_BLKDISCARD -D_FILE_OFFSET_BITS=64
sbin_PROGRAMS = mkfs.f2fs
noinst_HEADERS = f2fs_format_utils.h
include_HEADERS = $(top_srcdir)/include/f2fs_fs.h
mkfs_f2fs_SOURCES = f2fs_format_main.c f2fs_format.c f2fs_format_utils.c
mkfs_f2fs_LDADD = ${libuuid_LIBS} ${libblkid_LIBS} $(top_builddir)/lib/libf2fs.la

lib_LTLIBRARIES = libf2fs_format.la
libf2fs_format_la_SOURCES = f2fs_format_main.c f2fs_format.c f2fs_format_utils.c
libf2fs_format_la_CFLAGS = -DWITH_BLKDISCARD -D_FILE_OFFSET_BITS=64
libf2fs_format_la_LDFLAGS = ${libblkid_LIBS} ${libuuid_LIBS} -L$(top_builddir)/lib -lf2fs \
	-version-info $(FMT_CURRENT):$(FMT_REVISION):$(FMT_AGE)

install-exec-hook:
	if test -n "$(root_libdir)" -a "$(libdir)" != "$(root_libdir)" -a \
	    -f "$(DESTDIR)$(libdir)/libf2fs_format.so"; then \
		$(MKDIR_P) $(DESTDIR)$(root_libdir); \
		mv $(DESTDIR)$(libdir)/libf2fs_format.so.* $(DESTDIR)$(root_libdir); \
		so_img_name=$$(readlink $(DESTDIR)$(libdir)/libf2fs_format.so); \
		so_img_rel_target=$$(echo $(libdir) | sed 's,\(^/\|\)[^/][^/]*,..,g'); \
		(cd $(DESTDIR)$(libdir) && \
			rm -f libf2fs_format.so && \
			$(LN_S) $$so_img_rel_target$(root_libdir)/$$so_img_name libf2fs_format.so); \
	fi

uninstall-hook:
	rm $(DESTDIR)$(root_libdir)/libf2fs_format.so*
