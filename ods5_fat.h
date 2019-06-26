#ifndef _ODS5_FAT_H

/*
 * linux/fs/ods5/ods5_fat.h
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
#define CHECK(x,y,z)
#endif

#include "./vms_types.h"

#define FAT_UNDEFINED 0			/* undefined record type */
#define FAT_FIXED 1			/* fixed record type */
#define FAT_VARIABLE 2			/* variable length */
#define FAT_VFC 3			/* variable + fixed control */
#define FAT_STREAM 4			/* RMS-11 (DEC traditional) stream format */
#define FAT_STREAMLF 5			/* LF-terminated stream format */
#define FAT_STREAMCR 6			/* CR-terminated stream format */
#define FAT_SEQUENTIAL 0		/* sequential organization */
#define FAT_RELATIVE 1			/* relative organization */
#define FAT_INDEXED 2			/* indexed organization */
#define FAT_DIRECT 3			/* direct organization */
#define FAT_SPECIAL 4			/* Special file organization */
#define FAT_FORTRANCC 0x1
#define FAT_IMPLIEDCC 0x2
#define FAT_PRINTCC 0x4
#define FAT_NOSPAN 0x8
#define FAT_MSBRCW 0x10
#define FAT_FIFO 1			/* FIFO special file */
#define FAT_CHAR_SPECIAL 2		/* character special file */
#define FAT_BLOCK_SPECIAL 3		/* block special file */
#define FAT_SYMLINK 4			/* symbolic link special file for pre-V8.2 */
#define FAT_SYMBOLIC_LINK 5		/* symbolic link special file for V8.2 and beyond */
#define FAT_GBC_PERCENT 0x1
#define FAT_GBC_DEFAULT 0x2

typedef struct fat_rtype {
	vms_byte rtype: 4;		/* record type */
	vms_byte fileorg: 4;		/* file organization */
} _FAT_RTYPE;
CHECK(_FAT_RTYPE,==,1)

typedef struct fat_rattrib {
	vms_byte fortrancc: 1;		/* Fortran carriage control */
	vms_byte impliedcc: 1;		/* implied carriage control */
	vms_byte printcc: 1;		/* print file carriage control */
	vms_byte nospan: 1;		/* no spanned records */
	vms_byte msbrcw: 1;		/* Format of RCW (0=LSB, 1=MSB) */
} _FAT_RATTRIB;
CHECK(_FAT_RATTRIB,==,1)

typedef struct fat_block {
	vms_word high;			/* high order word */
	vms_word low;			/* low order word */
} _FAT_BLOCK;
CHECK(_FAT_BLOCK,==,4)

typedef struct ods5_fat {
	struct fat_rtype rtype;		/* record type */
	struct fat_rattrib rattrib;	/* record attributes */
	vms_word rsize;			/* record size in bytes */
	struct fat_block hiblk;		/* highest allocated VBN*/
	struct fat_block efblk;		/* end of file VBN */
	vms_word ffbyte;		/* first free byte in EFBLK */
	vms_byte bktsize;		/* bucket size in blocks */
	vms_byte vfcsize;		/* size in bytes of fixed length control for VFC records */
	vms_word maxrec;		/* maximum record size in bytes */
	vms_word defext;		/* default extend quantity */
	vms_word gbc;			/* global buffer count (original word) */
	vms_byte recattr_flags;		/* flags for record attribute area */
	vms_byte fill_0;
	vms_long gbc32;			/* longword implementation of global buffer count */
	vms_word fill_1;		/* spare space documented as unused in I/O REF */
	vms_word versions;		/* default version limit for directory file */
} _ODS5_FAT;
CHECK(_ODS5_FAT,==,32)

#define _ODS5_FAT_H loaded
#endif
