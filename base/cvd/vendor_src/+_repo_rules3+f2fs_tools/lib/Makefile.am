## Makefile.am

lib_LTLIBRARIES = libf2fs.la

libf2fs_la_SOURCES = libf2fs.c libf2fs_io.c libf2fs_zoned.c nls_utf8.c
libf2fs_la_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64
libf2fs_la_CPPFLAGS = -I$(top_srcdir)/include
libf2fs_la_LDFLAGS = -version-info $(LIBF2FS_CURRENT):$(LIBF2FS_REVISION):$(LIBF2FS_AGE)

root_libdir=@root_libdir@

install-exec-hook:
	if test -n "$(root_libdir)" -a "$(libdir)" != "$(root_libdir)" -a \
	    -f "$(DESTDIR)$(libdir)/libf2fs.so"; then \
		$(MKDIR_P) $(DESTDIR)$(root_libdir); \
		mv $(DESTDIR)$(libdir)/libf2fs.so.* $(DESTDIR)$(root_libdir); \
		so_img_name=$$(readlink $(DESTDIR)$(libdir)/libf2fs.so); \
		so_img_rel_target=$$(echo $(libdir) | sed 's,\(^/\|\)[^/][^/]*,..,g'); \
		(cd $(DESTDIR)$(libdir) && \
			rm -f libf2fs.so && \
			$(LN_S) $$so_img_rel_target$(root_libdir)/$$so_img_name libf2fs.so); \
	fi

uninstall-hook:
	rm -f $(DESTDIR)$(root_libdir)/libf2fs.so*
