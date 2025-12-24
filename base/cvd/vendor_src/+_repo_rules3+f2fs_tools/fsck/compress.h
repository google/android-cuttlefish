/**
 * compress.h
 *
 * Copyright (c) 2020 Google Inc.
 *   Robin Hsu <robinhsu@google.com>
 *  : add sload compression support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COMPRESS_H
#define COMPRESS_H

#include "f2fs_fs.h"

extern const char *supported_comp_names[];
extern compress_ops supported_comp_ops[];
extern filter_ops ext_filter;

#endif /* COMPRESS_H */
