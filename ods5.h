#ifndef	_ODS5_H

/*
 * linux/fs/ods5/ods5.h
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

#include "./vms_types.h"
#include "./ods5_fs.h"
#include <linux/buffer_head.h>
#include <linux/semaphore.h>

#ifdef DEBUG
extern int ods5_debug_level;
#define ods5_debug(l,fmt,arg...) if (ods5_debug_level>=l) pr_debug ("ODS5(%s), " fmt,__func__,##arg)
#else
#define ods5_debug(fmt,arg...)
#endif
#define ods5_info(fmt,arg...) pr_info ("ODS5(%s), " fmt,__func__,##arg)

// ??? #define FMT_size_t "%z"
#if BITS_PER_LONG==64
# define FMT_size_t "%lu"
#else
# define FMT_size_t "%u"
#endif

/* super block extension */
typedef struct ods5_sb_info {
	vms_long ibmapsize;
	vms_long indexflbn;
	vms_long ioblocks;
	vms_long ioshifts;
	vms_long maxfiles;
	vms_long freeblocks;
	vms_long usedfids;
	vms_long volsize;
	vms_long home;		/* home lbn, decimal, >0 */
	vms_long mode;		/* mode has an umask value, octal */
	vms_word blocksize;
	vms_word clustersize;
	vms_word volchar;
	vms_byte dotversion;	/* ... the others are boolean */
	vms_byte nomfd;
	vms_byte home_opt;
	vms_byte mode_opt;
	vms_byte bs_opt;
	vms_byte syml;
	vms_byte utf8;
} _ODS5_SB_INFO;

/* inode extension: mapping info from file header */
typedef struct ods5_ext_info {
	struct ods5_fid ext_fid;
	struct ods5_ext_info *next;
	vms_byte map_inuse;
	union ods5_fm2 map[0];
} _ODS5_EXT_INFO;

/* inode extension: some file header info */
typedef struct ods5_fh_info {
	vms_word fid_seq;
	struct ods5_fat recattr;
        struct semaphore ext_lock;
	struct ods5_ext_info ext;
} _ODS5_FH_INFO;

int ods5_isl_to_utf(unsigned char *utf8, unsigned int utf8len, unsigned char *name, vms_byte namelen);
int is_valid_home(struct ods5_home * home) ;
int is_used_fh2(struct ods5_fh2 * fh2, struct ods5_fid fid) ;
int mapvbn(struct super_block *sb, struct inode *inode, vms_long vbn,
		vms_long * lbn, vms_long * extend);
struct buffer_head *ods5_read_fh (struct super_block *sb, int fnum, 
				  struct ods5_fh2 **fh2);
long ods5_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);

static inline struct ods5_sb_info *get_sb_info (struct super_block *sb) {
	return sb->s_fs_info;
}

/*
 * Short, but not simple. The purpose is to have a common code sequence to
 * do buffer reads.
 * A macro would be OK but a macro can't check the argument types, so it's
 * an inline function.
 * To make it fast, at least in the default case (a successful read), it
 * contains no check of the sb_bread returned pointer. The caller has to
 * check it anyway, because the caller also has to release the buffer.
 * Checking it twice is too much.
 * The caller also needs to get to the data of the ODS5 lbn. The caller
 * can calculate the offset, but then it needs to access the sb_info. So
 * the calculation is done here, where the sb_info is required, anyway.
 * The calculated offset is returned.
 */

static inline struct buffer_head *ods5_bread(struct super_block *sb,
					     vms_long lbn, vms_long *iopos)
{
	struct ods5_sb_info *sb_info;
	struct buffer_head *bh;
	vms_long n, o;
	sb_info = get_sb_info(sb);

	n = lbn >> sb_info->ioshifts;
	o = lbn - (n << sb_info->ioshifts);
	ods5_debug(3, "lbn: %d, ioblock: %d, offset: %d\n", lbn, n, o);
	bh = sb_bread(sb, n);
	*iopos = o * ODS5_BLOCK_SIZE;
	return bh;
}

static inline struct ods5_fid mkfid (struct inode *inode) {
	struct ods5_fid fid;
	struct ods5_fh_info *fh_info;
	fid.num = (vms_word) inode->i_ino;
	fh_info = (struct ods5_fh_info *)inode->i_private;
	fid.seq = fh_info->fid_seq;;
	fid.rvn = 0;
	fid.nmx = (vms_byte) (inode->i_ino >> 16);
	return fid;	
}

/*
 * Do an 'inode = iget (sb, ino);'
 * iget sets ino and calls .read_inode == ods5_read_inode, which checks the fid, but seq isn't in ino,
 * it must be explicitly set, so ... but it depends on the kernel.
 * 
 * With 2.6.25 there is no more superop read_inode(), but also for the older 2.6 kernels ods5_read_inode()
 * can be called directly, here.
 * And with no superop, ods5_read_inode can return a status. I'm interested in the ENOENT.
 * 
 * With 2.6.25 there is no iget() anymore - but I didn't use it at all - an own read_inode is expected to
 * be moved into an own iget - I always had - which should be called for the now obsolete vfs iget().
 * 
 * A fid contains two numbers the one which is used for the inode number and seq, the sequence number.
 * Both together make the file unique. Using only one part for the inode number was chosen on purpose:
 * VMS users know the numbers for some significant files and that should show as ino, here. That requires an
 * additional check for a match of the sequence numbers if the iget_locked returns an existing inode for a given
 * ino. If the inode is new and the unique check in ods5_read_super failed, that function made a bad inode.
 * Instead of checking for a bad inode, a return of ENOENT would be easier.
 */
extern void ods5_read_inode(struct inode *);
static inline struct inode * ods5_iget (struct super_block *sb , unsigned long ino , vms_word seq) {
	struct inode * inode;
	inode = iget_locked(sb, ino);
	if (inode && !(inode->i_state & I_NEW)) {
		if (((struct ods5_fh_info *)inode->i_private)->fid_seq != seq) {
			iput(inode);
			return NULL;
		}
		return inode;
	}
   
	inode->i_private = (void *)(unsigned long)seq;
	ods5_read_inode(inode);
	if (is_bad_inode(inode)) {
       		iput(inode);
		unlock_new_inode(inode);
		return NULL;
	}
	unlock_new_inode(inode);
	return inode;
}

#define	_ODS5_H loaded
#endif
