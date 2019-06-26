#ifndef	_ODS5_FS_H

/*
 * linux/fs/ods5/ods5_fs.h
 *
 * This file is part of the OpenVMS ODS5 file system for Linux.
 * Copyright (C) 2016 Hartmut Becker.
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

# ifndef CHECK
#define CHECK(x,y,z)
#endif

#include "./vms_types.h"
#include "./ods5_fat.h"

#define ODS5_MAGIC	0x3553444f
#define ODS5_MODVER	"1.0.0"
#if defined(DEBUG) && defined(CONFIG_SYSCTL)
#define ODS5_MODDEBUG	"(Debug/Sysctl Version)"
#elif defined(DEBUG)
#define ODS5_MODDEBUG	"(Debug Version)"
#else
#define ODS5_MODDEBUG	""
#endif

/* page size is 1<<page_shift */
#define ODS5_BLOCK_SHIFT	9
#define ODS5_BLOCK_SIZE	512

#define ODS5_INDEXF_INO	1
#define ODS5_BITMAP_INO	2
#define ODS5_MFD_INO	4
#define	ODS5_LAST_FIXED_FH	16

/*
 * FILENAME_LEN + ';' + VERS_MAX + \0
 * = ODS2: 87 rounded 88
 * = ODS5(ISL-1): 243 rounded to 244
 * = ODS5(UCS-2): 250
 */
#define ODS5_FN_STRING_SIZE 250
#define ODS5_FILENAME_LEN 236
#define ODS5_VERS_MAX 32767

#define ODS5_IOC_GETFAT 0x000D5501
#define ODS5_IOC_GETFH  0x000D5502

#define ODS5_VOL_READCHECK 0x1
#define ODS5_VOL_WRITCHECK 0x2
#define ODS5_VOL_ERASE 0x4
#define ODS5_VOL_NOHIGHWATER 0x8
#define ODS5_VOL_CLASS_PROT 0x10
#define ODS5_VOL_ACCESSTIMES 0x20
#define ODS5_VOL_HARDLINKS 0x40
/* home block (second to boot block on disk) */
typedef struct ods5_home {
	vms_long homelbn;
	vms_long alhomelbn;
	vms_long altidxlbn;
	vms_word struclev;
	vms_word cluster;
	vms_word homevbn;
	vms_word alhomevbn;
	vms_word altidxvbn;
	vms_word ibmapvbn;
	vms_long ibmaplbn;
	vms_long maxfiles;
	vms_word ibmapsize;
	vms_word resfiles;
	vms_word devtype;
	vms_word rvn;
	vms_word setcount;
	vms_word volchar;
	struct vms_uic volowner;
	vms_long reserved1;
	struct vms_prot protect;
	struct vms_prot fileprot;
	vms_word reserved2;
	vms_word checksum1;
	vms_quad credate __attribute ((packed));
	vms_byte window;
	vms_byte lru_lim;
	vms_word extend;
	vms_quad retainmin;
	vms_quad retainmax;
	vms_quad revdate;
	vms_byte min_class[20];
	vms_byte max_class[20];
	vms_byte reserved3[320];
	vms_long serialnum;
	vms_byte strucname[12];
	vms_byte volname[12];
	vms_byte ownername[12];
	vms_byte format[12];
	vms_word reserved4;
	vms_word checksum2;
} _ODS5_HOME;
CHECK(_ODS5_HOME,==,512)

/* file identifier */
typedef struct ods5_fid {
	vms_word num;
	vms_word seq;
	vms_byte rvn;
	vms_byte nmx;
} _ODS5_FID;
CHECK(_ODS5_FID,==,6)

/* file characteristics */
typedef struct ods5_fch {
	vms_long wascontig: 1;
	vms_long nobackup: 1;
	vms_long writeback: 1;
	vms_long readcheck: 1;
	vms_long writcheck: 1;
	vms_long contigb: 1;
	vms_long locked: 1;
	vms_long contig: 1;

	vms_long vcc_state: 3;
	vms_long badacl: 1;
	vms_long spool: 1;
	vms_long directory: 1;
	vms_long badblock: 1;
	vms_long markdel: 1;

	vms_long nocharge: 1;
	vms_long erase: 1;
	vms_long alm_aip: 1;
	vms_long shelved: 1;
	vms_long scratch: 1;
	vms_long nomove: 1;
	vms_long noshelvable: 1;
	vms_long preshelved: 1;
} _ODS5_FCH;
CHECK(_ODS5_FCH,==,4)

/* file header */
typedef struct ods5_fh2 {
	vms_byte idoffset;
	vms_byte mpoffset;
	vms_byte acoffset;
	vms_byte rsoffset;
	vms_word seg_num;
	vms_word struclev;
	struct ods5_fid fid;
	struct ods5_fid ext_fid;
	struct ods5_fat recattr;
	struct ods5_fch filechar;
	vms_word recprot;
	vms_byte map_inuse;
	vms_byte acc_mode;
	struct vms_uic fileowner;
	struct vms_prot fileprot;
	struct ods5_fid backlink;
	vms_byte journal;
	vms_byte ru_active;
	vms_word linkcount;
	vms_long highwater;
	vms_byte reserved[430];
	vms_word checksum;
} _ODS5_FH2;
CHECK(_ODS5_FH2,==,512)

/* file ident, level 2 */
typedef struct ods5_fi2 {
	vms_byte filename[20];
	vms_word revision;
	vms_quad credate __attribute ((packed));
	vms_quad revdate __attribute ((packed));
	vms_quad expdate __attribute ((packed));
	vms_quad bakdate __attribute ((packed));
	vms_byte filenamext[66];
} _ODS5_FI2;
CHECK(_ODS5_FI2,==,120)

/* file ident, level 5 */
#define FI5_ODS2 0
#define FI5_ISL1 1
#define FI5_UCS2 3
typedef struct ods5_fi5 {
	vms_byte control;
	vms_byte namelen;
	vms_word revision;
	vms_quad credate __attribute ((packed));
	vms_quad revdate __attribute ((packed));
	vms_quad expdate __attribute ((packed));
	vms_quad bakdate __attribute ((packed));
	vms_quad accdate __attribute ((packed));
	vms_quad attdate __attribute ((packed));
	vms_quad ex_recattr __attribute ((packed));
	vms_quad hint_lo_qw __attribute ((packed));
	vms_quad hint_hi_qw __attribute ((packed));
	vms_byte filename [44];
	vms_byte filenamext [204];
} _ODS5_FI5;
CHECK(_ODS5_FI5,==,324)

/* file mapping, retrieval pointer */
typedef union ods5_fm2 {
	struct format0 {
		vms_word word0: 14;
		vms_word format: 2;
	} format0;
	struct format1 {
		vms_byte count;
		vms_byte highlbn: 6;
		vms_byte format: 2;
		vms_word lowlbn;
	} format1;
	struct format2 {
		vms_word count: 14;
		vms_word format: 2;
		vms_long lbn __attribute ((packed));
	} format2;
	struct format3 {
		vms_word highcount: 14;
		vms_word format: 2;
		vms_word lowcount;
		vms_long lbn __attribute ((packed));
	} format3;
} _ODS5_FM2;
CHECK(_ODS5_FM2,==,8)

/* directory record flags */
typedef struct ods5_dirflags {
	vms_byte type: 3;
	vms_byte nametype: 3;
	vms_byte nextrec: 1;
	vms_byte prevrec: 1;
} _ODS5_DIRFLAGS;
CHECK(_ODS5_DIRFLAGS,==,1)

/* directory record types */
#define DIR_FID 0
#define DIR_LINKNAME 1
/* directory record name types */
#define DIR_ODS2 0
#define DIR_ISL1 1
#define DIR_UCS2 3

/*
 * directory record
 * name is variable, but vms_word size padded, therefore the [2]
 * the directory entries follow
 */
typedef struct ods5_dir {
	vms_word size;
	vms_word version;
	struct ods5_dirflags flags;
	vms_byte namecount;
	vms_byte name[2];
} _ODS5_DIR;
CHECK(_ODS5_DIR,==,8)

/* directory entry */
typedef struct ods5_dirent {
	vms_word version;
	struct ods5_fid fid;
} _ODS5_DIRENT;
CHECK(_ODS5_DIRENT,==,8)

/* storage control block */
typedef struct ods5_scb {
	vms_word struclev;
	vms_word cluster;
	vms_long volsize;
	vms_long blksize;
	vms_long sectors;
	vms_long tracks;
	vms_long cylinder;
	vms_long status;
	vms_long status2;
	vms_word writecnt;
	char volockname [12];
	vms_quad mounttime __attribute ((packed));
	vms_byte not_used [456];
	vms_word checksum;
} _ODS5_SCB;
CHECK(_ODS5_SCB,==,512)

#define	_ODS5_FS_H loaded
#endif
