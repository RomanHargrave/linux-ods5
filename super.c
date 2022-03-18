/*
 * linux/fs/ods5/super.c
 *
 * This file is part of the OpenVMS ODS5 file system for Linux.
 * Copyright (C) 2017 Hartmut Becker.
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
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/nls.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/statfs.h>

#if defined(DEBUG) && defined(CONFIG_SYSCTL)
# include <linux/proc_fs.h>
# include <linux/sysctl.h>
#endif

#include "./v2utime.h"
#include "./ods5_fs.h"
#include "./ods5.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ODS5 Filesystem " ODS5_MODDEBUG);
MODULE_AUTHOR("Hartmut Becker");
MODULE_VERSION(ODS5_MODVER);

#ifdef DEBUG
int ods5_debug_level = 0;
module_param(ods5_debug_level, int, 0644);
MODULE_PARM_DESC(ods5_debug_level, " >0 - write debug info into the kernel message buffer; default = 0; changeable with sysctl");
#endif

static vms_byte bit_table[256] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

static struct super_operations ods5_super_operations;
extern struct file_operations ods5_dir_operations;
extern struct file_operations ods5_file_operations;
extern struct inode_operations ods5_inode_operations;
extern struct inode_operations ods5_inode_symlink_ops;
extern const struct xattr_handler *ods5_xattr_handlers[];

static void fill_fh_info (struct ods5_fh_info *fh_info, struct ods5_fh2 *fh2)
{
	memcpy (&fh_info->recattr, &fh2->recattr, sizeof fh_info->recattr);
	memcpy (&fh_info->ext.ext_fid, &fh2->ext_fid, sizeof fh_info->ext.ext_fid);
	ods5_debug(2, "map_inuse: 0x%02x, mpoffset: 0x%02x\n",
		   fh2->map_inuse, fh2->mpoffset);
	fh_info->ext.map_inuse = fh2->map_inuse;
	memcpy (&fh_info->ext.map[0], &((vms_word*)fh2)[fh2->mpoffset], sizeof(vms_word)*fh2->map_inuse);
}

struct buffer_head *ods5_read_fh (struct super_block *sb, int fnum, struct ods5_fh2 **fh2)
{
	vms_long lbn;
	vms_long unused;
	struct buffer_head *bh;
	vms_long iopos;
	struct ods5_sb_info *sb_info;
	sb_info = get_sb_info(sb);
	
        if (fnum <= ODS5_LAST_FIXED_FH)
		lbn = sb_info->indexflbn + fnum - 1;
        else {
	   struct inode *indexf_inode;
	   int ret;
	   /* map the vbn of indexf.sys which contains the file header of the inode */
	   indexf_inode = ods5_iget (sb, ODS5_INDEXF_INO,ODS5_INDEXF_INO);
	   if (!indexf_inode)
	     return NULL;
	   ret= mapvbn
	       (sb, indexf_inode,
		sb_info->clustersize * 4 + sb_info->ibmapsize + fnum, &lbn,
		&unused);
	   iput (indexf_inode);
	   if (!ret)
	     return NULL;;
	}

	/* read it */
    bh = ods5_bread(sb, lbn, &iopos);
	if (bh != NULL)
		*fh2 = (struct ods5_fh2 *)(bh->b_data + iopos);
	return bh;
}

/*
 * Hackery to get UTF-8 support working for symbolic links. VMS/ODS-5 stores
 * ISO Latin-1 characters in the link file. To make such symbolic links work
 * in the ods5/UTF-8 environment, the symbolic link will internally be re-encoded
 * as UTF-8, which is done in readlink. For non-ASCII characters that means,
 * the UTF-8 string is longer than the ISO Latin-1 string: UTF-8 encoding needs
 * two bytes. However, the size of the buffer (supplied by the user) will be
 * i_size, so readlink can't re-encode into that buffer.
 * To work around this, here i_size is adjusted based on the contents of the
 * link file: count the 8-bit ISO Latin-1 characters and add that to i_size.
 * This has the side effect, that for ODS-5 symbolic links "ls -l" shows
 * different file sizes depending on whether the ODS-5 disk was mounted with
 * option utf8 or vtf7.
 */
static loff_t adjust_size(struct inode *inode) {
	/* extracted/copied from ods5_read(), file.c */
	size_t fbytes;
	vms_long vbn, vbnextends, vbnpos;
	vms_long lbn, lbnextends;
	vms_long iopos, iobytes, xbytes;
	struct buffer_head *bh;
	char *lbdata;
	unsigned char *buf;
	int incr;
	int i;

	fbytes = inode->i_size;
	if (fbytes==0)
		return fbytes;

	buf = kmalloc(fbytes, GFP_NOFS);
	if (!buf)
		return fbytes;

	/* for mapping the file, calculate the vbn range in which
	 * the fbytes starting at fpos are: first vbn and extends */
	vbn = 1;
	vbnpos = 0;
	vbnextends = (vbnpos + fbytes + ODS5_BLOCK_SIZE - 1) >> ODS5_BLOCK_SHIFT;
	ods5_debug(3, "vbn: %d, vbnpos: %d, vbnextends: %d\n", vbn, vbnpos,
		   vbnextends);

	xbytes = 0;
	iobytes = 0;
	/* as long as there aren't all bytes transfered */
	while (xbytes < fbytes) {
		/* get the lbn and the extend size */
		if (!mapvbn(inode->i_sb, inode, vbn, &lbn, &lbnextends)) {
			kfree(buf);
			return 0;
		}
		/* don't read more lbns than necessary */
		if (lbnextends > vbnextends)
			lbnextends = vbnextends;
		/* read the block */
		bh = ods5_bread(inode->i_sb, lbn, &iopos);
		if (bh == NULL) {
			ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
			kfree(buf);
			return -EIO;
		}
		/* iopos == offset of lbdata in bh->b_data */
		lbdata = bh->b_data + iopos;
		/* there is more in bh->b_data,
		 * calculate how many of the read bytes can be copied */
		iobytes = inode->i_sb->s_blocksize - iopos - vbnpos;
		/* limit to what is mapped */
		if (iobytes > (lbnextends << ODS5_BLOCK_SHIFT)-vbnpos)
			iobytes = (lbnextends << ODS5_BLOCK_SHIFT)-vbnpos;
		/* limit to what is needed */
		if (iobytes > (fbytes - xbytes))
			iobytes = fbytes - xbytes;
		memcpy (&buf[xbytes], &lbdata[vbnpos], iobytes);
		brelse(bh);

		ods5_debug(3,
		    "memcpy, &buf[%d], vbnpos: %d, iobytes: %d\n",
		     xbytes, vbnpos, iobytes);

		/* update transferred bytes, position to next vbn in file */
		xbytes += iobytes;
		vbn += (vbnpos + iobytes) >> ODS5_BLOCK_SHIFT;
		vbnextends -= (vbnpos + iobytes) >> ODS5_BLOCK_SHIFT;
		vbnpos += iobytes;
		vbnpos &= (ODS5_BLOCK_SIZE - 1);
	}			/* while xbytes<fbytes */
	incr = 0;
	for (i=0; i<fbytes; i++)
		if (buf[i] & 0x80)
			incr++;
	kfree(buf);
	ods5_debug(2, "return, fbytes: " FMT_size_t ", incr: %d\n", fbytes, incr);
	return inode->i_size+incr;
}

#define GOOD_RETURN goto good
#define BAD_RETURN goto bad
#define BAD_BRELSE_RETURN goto bad_brelse
void ods5_read_inode(struct inode *inode)
{
	struct fat_block *b;
	struct ods5_fid fid;
	struct ods5_fh2 *fh2;
	struct ods5_fi2 *fi2;
	struct ods5_fi5 *fi5;
	struct buffer_head *bh;
	struct ods5_sb_info *sb_info;
	struct ods5_fh_info *fh_info;
	unsigned long tmp_seq;

	bh = ods5_read_fh(inode->i_sb, inode->i_ino, &fh2);
	if (bh == NULL) {
	     ods5_debug(1, "ods5_read_fh for ino %lu failed\n", inode->i_ino);
	     BAD_RETURN;
	}

	/*
	 * fid = mkfid(inode)
	 * seq is not yet in fh_info, so manually construct the fid
	 */
	fid.num = (vms_word) inode->i_ino;
	/* seq is really vms_word, but casting from a void* to unsigned short gives a warning, this cast does not */
	tmp_seq = (unsigned long)inode->i_private;
	fid.seq = (vms_word) tmp_seq;
	fid.rvn = 0;
	fid.nmx = (vms_byte) (inode->i_ino >> 16);

	/* check the file header */
	if (!is_used_fh2(fh2, fid))
	        BAD_BRELSE_RETURN;

	fh_info = kmalloc (sizeof *fh_info+sizeof(vms_word)*fh2->map_inuse, GFP_NOFS);
	if (!fh_info)
	        BAD_BRELSE_RETURN;

	memset (fh_info, 0, sizeof *fh_info);
        sema_init (&fh_info->ext_lock, 1);
	fh_info->fid_seq= (vms_word)tmp_seq;

        inode->i_private = fh_info;
	fill_fh_info (inode->i_private, fh2);

	ods5_debug(2, "filechar: 0x%08x\n", *(vms_long *) (&fh2->filechar));
	if (fh2->filechar.directory) {
		inode->i_mode = S_IFDIR;
		inode->i_op = &ods5_inode_operations;
		inode->i_fop = &ods5_dir_operations;
	} else {
		if ((fh2->recattr.rtype.fileorg==FAT_SPECIAL)
		    && (*(vms_byte*)(&fh2->recattr.rattrib)==FAT_SYMBOLIC_LINK)) {
			inode->i_mode = S_IFLNK;
			inode->i_op = &ods5_inode_symlink_ops;
		} else {
			inode->i_mode = S_IFREG;
			inode->i_op = &ods5_inode_operations; /* ??? needed for regular files ? */
		}
		inode->i_fop = &ods5_file_operations; /* ??? needed for symlinks ? */
	}

#define DENY_READ 0x01
#define DENY_WRITE 0x02
#define DENY_EXEC 0x04
#define DENY_DEL 0x08

	if ((fh2->fileprot.owner & DENY_READ) == 0)
		inode->i_mode |= S_IRUSR;
	if ((fh2->fileprot.owner & DENY_WRITE) == 0)
		inode->i_mode |= S_IWUSR;
	if ((fh2->fileprot.owner & DENY_EXEC) == 0)
		inode->i_mode |= S_IXUSR;
	if ((fh2->fileprot.group & DENY_READ) == 0)
		inode->i_mode |= S_IRGRP;
	if ((fh2->fileprot.group & DENY_WRITE) == 0)
		inode->i_mode |= S_IWGRP;
	if ((fh2->fileprot.group & DENY_EXEC) == 0)
		inode->i_mode |= S_IXGRP;
	if ((fh2->fileprot.world & DENY_READ) == 0)
		inode->i_mode |= S_IROTH;
	if ((fh2->fileprot.world & DENY_WRITE) == 0)
		inode->i_mode |= S_IWOTH;
	if ((fh2->fileprot.world & DENY_EXEC) == 0)
		inode->i_mode |= S_IXOTH;

	sb_info = get_sb_info(inode->i_sb);
	inode->i_mode |= sb_info->mode;
	i_uid_write(inode, fh2->fileowner.mem);
	i_gid_write(inode, fh2->fileowner.grp);

	ods5_debug(2, "i_mode: 0x%08x\n", inode->i_mode);
	b = &fh2->recattr.hiblk;
	inode->i_blocks = (b->high << 16) + b->low;
	b = &fh2->recattr.efblk;
	inode->i_size = (((loff_t)(b->high) << 16) + b->low - 1) * ODS5_BLOCK_SIZE
			+ fh2->recattr.ffbyte;
	if (sb_info->utf8 && S_ISLNK(inode->i_mode))
		inode->i_size = adjust_size(inode);
	if (fh2->idoffset == 0) {
		inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
		GOOD_RETURN;
	}
	if ((fh2->struclev >> 8) == 2) {
		fi2 = (struct ods5_fi2 *)&((vms_word *) fh2)[fh2->idoffset];
		inode->i_ctime = v2utime(fi2->credate);
		inode->i_mtime = inode->i_atime = v2utime(fi2->revdate);
		set_nlink(inode,1);
	} else { /* ((fh2->struclev >> 8) == 5) */
		fi5 = (struct ods5_fi5 *)&((vms_word *) fh2)[fh2->idoffset];
		inode->i_ctime = v2utime(fi5->attdate);
		inode->i_mtime = v2utime(fi5->revdate);
		inode->i_atime = v2utime(fi5->accdate);
		set_nlink(inode,(sb_info->volchar /* & ODS5_VOL_HARDLINKS */)? fh2->linkcount: 1);
	}

good:
        brelse(bh);
        return;

bad_brelse:
	brelse(bh);
bad:
	inode->i_private = NULL;
        make_bad_inode (inode);
        return;
}
#undef GOOD_RETURN
#undef BAD_RETURN
#undef BAD_BRELSE_RETURN

/* make common code with get_freeblocks, additionally just pass the fid */
/* move (static) usedfids */
static vms_long get_usedfids(struct super_block *sb, vms_long maxfiles)
{
	vms_long vbn, lbn;
	struct buffer_head *bh;
	vms_long iopos;
	vms_long bitmap_bytes;
	vms_long extends;
	vms_long *long_bits;
	vms_byte *byte_bits;
	vms_long usedfids;
	int i, j;
	struct ods5_sb_info *sb_info;

	ods5_debug(2, "%s\n", "start");
	sb_info = get_sb_info(sb);

	/* use common code with read? */
	ods5_debug(2, "home->maxfiles: %d\n", sb_info->maxfiles);
	bitmap_bytes = (sb_info->maxfiles+ 8- 1)/ 8;
	ods5_debug(2, "=> ibmapsize: %d bytes\n", bitmap_bytes);
	ods5_debug(2, "home->ibmapsize: %d blocks\n", sb_info->ibmapsize);

	usedfids = 0;
	/* I know, the starting lbn is in the home block ... */
	for (vbn = sb_info->clustersize * 4 + 1;
	     vbn < sb_info->clustersize * 4 + 1 + sb_info->ibmapsize; vbn++) {
		struct inode *indexf_inode;
		int ret;
		/* map the vbn of INDEXF.SYS (1,1,0) */
		indexf_inode = ods5_iget (sb, ODS5_INDEXF_INO,ODS5_INDEXF_INO);
		if (!indexf_inode)
			return 0;
		ret = mapvbn(sb, indexf_inode, vbn, &lbn, &extends);
		iput (indexf_inode);
		if (!ret)
			return 0;
		/* read the lbn */
		bh = ods5_bread(sb, lbn, &iopos);
		if (bh == NULL) {
			ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
			return 0;
		}
		long_bits = (vms_long *)(bh->b_data + iopos);
		for (i = 0; i < 512 / sizeof *long_bits; i++) {
			if (long_bits[i]) {
				byte_bits = (vms_byte *) &long_bits[i];
				for (j = 0; j < sizeof *long_bits; j++)
					usedfids += bit_table[byte_bits[j]];
			}
		}
		brelse(bh);
	}
	ods5_debug(2, "used fids: %d\n", usedfids);
	return usedfids;
}

static vms_long get_freeblocks(struct super_block *sb, vms_long volsize)
{
	vms_long vbn, lbn;
	struct buffer_head *bh;
	vms_long iopos;
	vms_long bitmap_blocks;
	vms_long extends;
	vms_long freeblocks;
	vms_long *long_bits;
	vms_byte *byte_bits;
	int i, j;
	struct ods5_sb_info *sb_info;

	ods5_debug(2, "%s\n", "start");
	sb_info = get_sb_info(sb);

	/* make common code with read? */
	bitmap_blocks =
	    ((sb_info->volsize + sb_info->clustersize -
	      1) / sb_info->clustersize);
	ods5_debug(2, "bitmap size: %d bits, 0x%x bytes\n", bitmap_blocks,
		   (bitmap_blocks + 7) / 8);
	bitmap_blocks = ((bitmap_blocks + 4096 - 1) / 4096);
	ods5_debug(2, "bitmap_blocks: %d\n", bitmap_blocks);

	freeblocks = 0;
	for (vbn = 2; vbn < 2 + bitmap_blocks; vbn++) {
		struct inode *bitmap_inode;
		int ret;
		/* map the vbn of BITMAP.SYS (2,2,0) */
		bitmap_inode = ods5_iget (sb, ODS5_BITMAP_INO,ODS5_BITMAP_INO);
		if (!bitmap_inode)
			return 0;
		ret = mapvbn(sb, bitmap_inode, vbn, &lbn, &extends);
		iput (bitmap_inode);
		if (!ret)
			return 0;
		/* read the lbn */
		bh = ods5_bread(sb, lbn, &iopos);
		if (bh == NULL) {
			ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
			return 0;
		}
		long_bits = (vms_long *)(bh->b_data + iopos);
		for (i = 0; i < 512 / sizeof *long_bits; i++)
			if (long_bits[i]) {
				byte_bits = (vms_byte *) &long_bits[i];
				for (j = 0; j < sizeof *long_bits; j++)
					freeblocks += bit_table[byte_bits[j]];
			}
		brelse(bh);
	}
	freeblocks *= sb_info->clustersize;
	ods5_debug(2, "freeblocks: %d\n", freeblocks);
	return freeblocks;
}

/* make value of volsize static */
static vms_long get_volsize(struct super_block *sb)
{
	struct ods5_scb *scb;
	vms_long lbn;
	struct buffer_head *bh;
	vms_long iopos;
	vms_long unused;
	vms_long volsize;

	ods5_debug(2, "%s\n", "start");

	/* make common code with read? */
	  {
		struct inode *bitmap_inode;
		int ret;
		/* map the vbn 1 of BITMAP.SYS (2,2,0) */
		bitmap_inode = ods5_iget (sb, ODS5_BITMAP_INO,ODS5_BITMAP_INO);
		if (!bitmap_inode)
			return 0;
		ret = mapvbn(sb, bitmap_inode, 1, &lbn, &unused);
		iput (bitmap_inode);
		if (!ret)
			return 0;
	  }
	/* read the lbn */
	bh = ods5_bread(sb, lbn, &iopos);
	if (bh == NULL) {
		ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
		return 0;
	}
	scb = (struct ods5_scb *)(bh->b_data + iopos);
	volsize = scb->volsize;
	/* here the scb can be checked (and debug/info can be printed) */
	ods5_debug(2, "volsize: %d\n", volsize);
	ods5_debug(2, "blksize: %d\n", scb->blksize);
	ods5_debug(2, "sectors: %d\n", scb->sectors);
	ods5_debug(2, "tracks: %d\n", scb->tracks);
	ods5_debug(2, "cylinder: %d\n", scb->cylinder);
	brelse(bh);
	/* volsize is expressed in lbn */
	return volsize;
}

static int ods5_statfs(struct dentry *d, struct kstatfs *buf)
{
	struct super_block *sb;
	struct ods5_sb_info *sb_info;
	sb = d->d_sb;
	sb_info = get_sb_info(sb);

	/* as long as this is read-only: */
	/* volsize is initialized to 0, which indicates the info needs to be fetched */
	if (sb_info->volsize == 0) {
		sb_info->volsize = get_volsize(sb);
		sb_info->freeblocks = get_freeblocks(sb, sb_info->volsize);
		sb_info->usedfids = get_usedfids(sb, sb_info->maxfiles);
	}
	buf->f_type = ODS5_MAGIC;
	buf->f_bsize = ODS5_BLOCK_SIZE;
	/* get the total blocks from the storage control block */
	buf->f_blocks = sb_info->volsize;
	if (buf->f_blocks == 0)
		return -1;	/* or whatever is appropriate */
	buf->f_bfree = sb_info->freeblocks;
	buf->f_bavail = sb_info->freeblocks;

	buf->f_files = sb_info->maxfiles;
	buf->f_ffree = sb_info->maxfiles - sb_info->usedfids;
	buf->f_namelen = 255;
	return 0;
}

static void ods5_put_super(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	return;
}

static void ods5_evict_inode (struct inode *inode) {
	struct ods5_fh_info *fh_info;
	struct ods5_ext_info *ext, *next;
        fh_info = inode->i_private;
	if (!fh_info)
		return;
	for (ext=fh_info->ext.next; ext; ext=next) {
		next = ext->next;
		kfree (ext);
	}
	kfree (fh_info);
	clear_inode(inode);	
}

static int ods5_show_options(struct seq_file *sf, struct dentry *root) {
	struct ods5_sb_info *sb_info;
	ods5_debug(2, "%s\n", "start");
	sb_info = get_sb_info(root->d_sb);
	if (sb_info->dotversion)
		seq_printf(sf, ",dotversion");
	if (sb_info->home_opt)
		seq_printf(sf, ",home=%d", sb_info->home);
	if (sb_info->bs_opt)
		seq_printf(sf, ",bs=%d", sb_info->blocksize);
	if (sb_info->mode_opt)
		seq_printf(sf, ",mode=0%o", sb_info->mode);
	if (sb_info->nomfd)
		seq_printf(sf, ",nomfd");
	if (sb_info->syml)
		seq_printf(sf, ",syml");
	if (sb_info->utf8)
		seq_printf(sf, ",utf8");
	else
		seq_printf(sf, ",vtf7");
	return 0;
}

static void set_common_options (struct ods5_sb_info *sb_info, char *data) {
	char *optv;
	/*
	 * I check only if the strings are present, not the order of options.
	 * So dotversion can't overwrite syml but syml always overwrites
	 * dotversion. I keep both overwrites, just in case I change the
	 * processing of the options.
	 * Somehow similar, if utf8 is present, it UTF-8 support is enabled
	 * if vtf8 is present, it overwrites utf8. If vtf8 is absent, UTF-8
	 * support is enabled, no matter whether utf8 was present or not.
	 * Essentially this makes utf8 the default.
	 */
	if (data && strstr(data, "dotversion")) {
		sb_info->dotversion = 1;
		sb_info->syml = 0;
	} else
		sb_info->dotversion = 0;
	if (data && strstr(data, "nomfd"))
		sb_info->nomfd = 1;
	else
		sb_info->nomfd = 0;

	if (data && strstr(data, "syml")) {
		sb_info->syml = 1;
		sb_info->dotversion = 0;
	} else
		sb_info->syml = 0;

	if (data && strstr(data, "utf8"))
		sb_info->utf8 = 1;
	else
		sb_info->utf8 = 0;
	if (data && strstr(data, "vtf7"))
		sb_info->utf8 = 0;
	else
		sb_info->utf8 = 1;

	sb_info->mode = 0;
	if (data && NULL != (optv = strstr(data, "mode="))) {
		sb_info->mode_opt = 1;		
		for (optv += sizeof "mode=" - 1; *optv >= '0' && *optv <= '7';
		     optv++)
			sb_info->mode = (sb_info->mode << 3) + *optv - '0';
		sb_info->mode &= 0777;
	} else
		sb_info->mode_opt = 0;
	
	ods5_debug(2, "mode=0%o\n", sb_info->mode);
}

static int ods5_remount_fs (struct super_block *sb, int *flags, char *data)  {
   struct ods5_sb_info *sb_info;
   sb_info = get_sb_info(sb);
   ods5_debug(2, "flags: %p, *flags: 0x%x\n", flags, *flags);
   ods5_debug(2, "data: %p, *data: %s\n", data, data);
   set_common_options (sb_info, data);
   return 0;
}

static struct super_operations ods5_super_operations = {
	.put_super = ods5_put_super,
	.statfs = ods5_statfs,
	.evict_inode = ods5_evict_inode,
        .remount_fs = ods5_remount_fs,
	.show_options = ods5_show_options,
};

static int ods5_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	vms_long iopos;
	struct ods5_home *home;
	struct inode *inode;
	struct dentry *root;
	struct ods5_sb_info *sb_info;
	char *optv;
	vms_long home_lbn;
	vms_long blocksize;

	ods5_debug(2, "%s\n", "start");
	ods5_info("options: '%s'\n", data? (char *)data: "<NULL>");

	if ((sb->s_flags & SB_RDONLY) == 0) {
		ods5_info("version %s only supports read-only\n", ODS5_MODVER);
		return -EACCES;
	}

	sb->s_magic = ODS5_MAGIC;
	sb->s_fs_info = kmalloc(sizeof *sb_info, GFP_KERNEL);
	sb_info = get_sb_info(sb);
	memset(sb_info, 0, sizeof *sb_info);
	if (data && NULL != (optv = strstr(data, "bs="))) {
		blocksize = 0;
		for (optv += sizeof "bs=" - 1; *optv >= '0' && *optv <= '9';
		     optv++)
			blocksize = (blocksize * 10) + *optv - '0';
		switch (blocksize) {
			case 512:
			case 1024:
			case 2048:
			case 4096:
				sb_info->blocksize = blocksize;
				sb_info->bs_opt = 1;
				break;
			default:
				ods5_info("unsupported filesystem blocksize %d\n", blocksize);
				return -EINVAL;
		}
	} else {
		blocksize = ODS5_BLOCK_SIZE;
		sb_info->bs_opt = 0;
	}
	ods5_debug(2, "bs=0x%x\n", blocksize);
	sb_min_blocksize(sb, blocksize);
	if (sb->s_blocksize != ODS5_BLOCK_SIZE)
		ods5_info("s_blocksize: %ld\n", sb->s_blocksize);
	if (sb->s_blocksize < ODS5_BLOCK_SIZE) {
		ods5_info("bad s_blocksize: %lu, minimum: %d\n", sb->s_blocksize, ODS5_BLOCK_SIZE);
		return -EINVAL;
	}
    set_common_options (sb_info, data);
	if (data && NULL != (optv = strstr(data, "home="))) {
		sb_info->home_opt = 1;
		home_lbn = 0;
		for (optv += sizeof "home=" - 1; *optv >= '0' && *optv <= '9';
		     optv++)
			home_lbn = (home_lbn * 10) + *optv - '0';
		sb_info->home = home_lbn;
	} else {
		sb_info->home_opt = 0;
		home_lbn = 1;
	}
	ods5_debug(2, "home=0x%x\n", home_lbn);

	for (sb->s_blocksize_bits = ODS5_BLOCK_SHIFT;
	     (1U << sb->s_blocksize_bits) < sb->s_blocksize;
	     sb->s_blocksize_bits++) ;
	if (sb->s_blocksize_bits != ODS5_BLOCK_SHIFT)
		ods5_debug(2, "s_blocksize_bits: %d\n", sb->s_blocksize_bits);
	sb_info->ioshifts = sb->s_blocksize_bits - ODS5_BLOCK_SHIFT;
	sb_info->ioblocks = 1U << sb_info->ioshifts;

	sb->s_op = &ods5_super_operations;

	bh = ods5_bread(sb, home_lbn, &iopos);
	if (bh == NULL) {
		ods5_info("ods5_bread of home block %d failed\n", home_lbn);
		goto failed;
	}
	home = (struct ods5_home *)(bh->b_data + iopos);

	if (!is_valid_home(home)) {
		bforget(bh);
		goto failed;
	}

	sb_info->clustersize = home->cluster;
	sb_info->volchar = home->volchar & ODS5_VOL_HARDLINKS;
	sb_info->ibmapsize = home->ibmapsize;
	sb_info->indexflbn = home->ibmaplbn + home->ibmapsize;
	sb_info->maxfiles = home->maxfiles;
	sb_info->volsize = 0;

	bforget(bh);
	inode = ods5_iget (sb, ODS5_MFD_INO, ODS5_MFD_INO);
	if (!inode)
		goto failed;
	ods5_debug(2, "inode: %p\n", inode);

	root = d_make_root(inode);
	if (!root) {
		iput(inode);
		goto failed;
	}
	ods5_debug(2, "root: %p\n", root);

	sb->s_xattr = ods5_xattr_handlers;
	sb->s_root = root;
	return 0;

      failed:
	kfree(sb->s_fs_info);
	return -EIO;
}

static struct dentry * ods5_mount(struct file_system_type *fs_type,
				       int flags, const char *dev_name,
				       void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, ods5_fill_super);
}

static struct file_system_type ods5_fs_type = {
	.name = "ods5",
	.owner = THIS_MODULE,
	.mount = ods5_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

#if defined(DEBUG) && defined(CONFIG_SYSCTL)
/* Definition of the ods5 sysctl. */
static struct ctl_table ods5_sysctls[] = {
        {
                .procname       = "ods5_debug_level",
                .data           = &ods5_debug_level,          /* Data pointer and size. */
                .maxlen         = sizeof(ods5_debug_level),
                .mode           = 0644,                 /* Mode, proc handler. */
                .proc_handler   = proc_dointvec
        },
        {}
};

/* Define the parent directory /proc/sys/fs. */
static struct ctl_table sysctls_root[] = {
        {
                .procname       = "fs",
                .mode           = 0555,
                .child          = ods5_sysctls
        },
        {}
};

static struct ctl_table_header *sysctls_root_table;
/*
 * add or remove the debug sysctl
 * @add:        add (1) or remove (0) the sysctl
 *
 * Add or remove the debug sysctl. Return 0 on success or -errno on error.
 */
static int ods5_sysctl(int add)
{
        if (add) {
                sysctls_root_table = register_sysctl_table(sysctls_root);
                if (!sysctls_root_table)
                        return -ENOMEM;
        } else {
		if (sysctls_root_table!=NULL) {
			unregister_sysctl_table(sysctls_root_table);
			sysctls_root_table = NULL;
		}
        }
        return 0;
}
#else
#define ods5_sysctl(add)
#endif

static int __init init_ods5_fs(void)
{
	ods5_info("ODS5 Filesystem %s %s\n", ODS5_MODVER, ODS5_MODDEBUG);
	ods5_sysctl(1);
	return register_filesystem(&ods5_fs_type);
}

static void __exit exit_ods5_fs(void)
{
	ods5_info("ODS5 Filesystem %s %s\n", ODS5_MODVER, ODS5_MODDEBUG);
	ods5_sysctl(0);
	unregister_filesystem(&ods5_fs_type);
}

module_init(init_ods5_fs)
module_exit(exit_ods5_fs)
