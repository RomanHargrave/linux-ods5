/*
 * linux/fs/ods5/sizchk.c
 *
 * This file is part of the OpenVMS ODS5 file system for Linux.
 * Copyright (C) 2010 Hartmut Becker.
 *
 * The OpenVMS ODS5 file system for Linux is free software; you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * The OpenVMS ODS5 file system for Linux is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>

/*
 * Check the size of some data types, just to make sure they are as big as
 * expected.
 *
 * This is a compile only module. If it compiles without error everything is
 * fine.
 *
 * Some compilers have a #pragma assert for this kind of stuff. To use it
 * anywhere the check sets the size of a character array. This can be included
 * in the other sources but then it might create some unused data.
 *
 * Here's the reason for having some usually unwanted typedefs:
 * you can't easily construct a valid C identifier out of a string with blank.
 */
#define CHECK(type,op,size) \
static char check_##type[(sizeof(type) op size)?1:-1];

/* The check is after each struct definition in the include files */
#include "./vms_types.h"
#include "./ods5_fs.h"
#include "./ods5_fat.h"
