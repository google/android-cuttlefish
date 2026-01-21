#!/bin/sh

aclocal && \
autoheader && \
autoconf && \
libtoolize && \
automake -a -c
