#ifndef	_VMS_TYPES_H

/*
 * linux/fs/ods5/vms_types.h
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

#ifndef CHECK
# define CHECK(x,y,z)
#endif

/* 1, 2, 4, 8 byte data types: unsigned integers */
typedef	unsigned char	vms_byte;
typedef unsigned short	vms_word;
typedef unsigned int	vms_long;
typedef unsigned long long vms_quad;
CHECK(vms_byte,==,1)
CHECK(vms_word,==,2)
CHECK(vms_long,==,4)
CHECK(vms_quad,==,8)

/* vms user identification code */
typedef struct vms_uic {
	vms_word mem;
	vms_word grp;
} _VMS_UIC;
CHECK(_VMS_UIC,==,4)

/* vms protection */
typedef struct vms_prot {
	vms_word system:4;
	vms_word owner:4;
	vms_word group:4;
	vms_word world:4;
} _VMS_PROT;
CHECK(_VMS_PROT,==,2)

#define	_VMS_TYPES_H loaded
#endif
