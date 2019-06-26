/*
 * linux/fs/ods5/file.c
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

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>

#include "./ods5_fs.h"
#include "./ods5.h"

static ssize_t ods5_read(struct file *file, char *buf, size_t max,
			 loff_t * offset)
{
	struct inode *inode;
	loff_t fsize, fpos;
	size_t fbytes;
	vms_long vbn, vbnextends, vbnpos;
	vms_long lbn, lbnextends;
	vms_long iopos, iobytes, xbytes;
	struct buffer_head *bh;
	char *lbdata;
	unsigned int not_copied;

	ods5_debug(2, "file: %p, buf: %p, max: " FMT_size_t ", *offset: %Ld\n",
		   file, buf, max, *offset);

	/* nothing to read, nothing to do */
	if (max == 0)
		return 0;

	/* do not read other than regular files */
	inode = file->f_path.dentry->d_inode;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	/* do not start after EOF */
	fpos = *offset;
	fsize = inode->i_size;
	if (fpos >= fsize)
		return 0;

	/* read up to EOF */
	fbytes = max;
	if (max > (fsize - fpos))
		fbytes = (fsize - fpos);

	/* for mapping the file, calculate the vbn range in which
	 * the fbytes starting at fpos are: first vbn and extends */
	vbn = (fpos >> ODS5_BLOCK_SHIFT) + 1;
	vbnpos = fpos & (ODS5_BLOCK_SIZE - 1);
	vbnextends = (vbnpos + fbytes + ODS5_BLOCK_SIZE - 1) >> ODS5_BLOCK_SHIFT;
	ods5_debug(2, "vbn: %d, vbnpos: %d, vbnextends: %d\n", vbn, vbnpos,
		   vbnextends);

	xbytes = 0;
	iobytes = 0;
	/* as long as there aren't all bytes transfered */
	while (xbytes < fbytes) {
		/* get the lbn and the extend size */
		if (!mapvbn(inode->i_sb, inode, vbn, &lbn, &lbnextends))
			return 0;
		/* don't read more lbns than necessary */
		if (lbnextends > vbnextends)
			lbnextends = vbnextends;
		/* read the block */
		bh = ods5_bread(inode->i_sb, lbn, &iopos);
		if (bh == NULL) {
			ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
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
		not_copied =
		    copy_to_user(&buf[xbytes], &lbdata[vbnpos], iobytes);
		brelse(bh);
		if (not_copied != 0) {
			ods5_debug(2, "data at %p, iobytes: %d, not copied: %d\n",
				   lbdata, iobytes, not_copied);
			return -EFAULT;
		}

		ods5_debug(2,
		    "copy_to_user, &buf[%d], vbnpos: %d, iobytes: %d\n",
		     xbytes, vbnpos, iobytes);
		/* update transferred bytes, position to next vbn in file */
		xbytes += iobytes;
		vbn += (vbnpos + iobytes) >> ODS5_BLOCK_SHIFT;
		vbnextends -= (vbnpos + iobytes) >> ODS5_BLOCK_SHIFT;
		vbnpos += iobytes;
		vbnpos &= (ODS5_BLOCK_SIZE - 1);
	}			/* while xbytes<fbytes */
	*offset += fbytes;
	ods5_debug(2, "return, fbytes: " FMT_size_t "\n", fbytes);
	return fbytes;
}

struct file_operations ods5_file_operations = {
	.read = ods5_read,
	.unlocked_ioctl = ods5_ioctl,
	.llseek = default_llseek,
};
