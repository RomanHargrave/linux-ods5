/*
 * linux/fs/ods5/ioctl.c
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
#include <linux/uaccess.h>
#include <linux/xattr.h>

#include "./ods5_fs.h"
#include "./ods5.h"

static int ods5_xattr_user_get (const struct xattr_handler *,
				struct dentry *, struct inode *inode,
				const char *, void *, size_t);

static const struct xattr_handler ods5_xattr_user_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get    = ods5_xattr_user_get,
};
const struct xattr_handler *ods5_xattr_handlers[] = {
	&ods5_xattr_user_handler,
	NULL
};

static int ods5_xattr_user_get (const struct xattr_handler *handler,
				struct dentry *dentry, struct inode *inode,
				const char *name, void *buffer, size_t size) {
	struct ods5_fh2 *fh2;
	struct buffer_head *bh;
	struct ods5_fid fid;
	struct ods5_fh_info *fh_info;
	size_t minl;

	ods5_debug(2, "name: %s\n", name);
	inode = dentry->d_inode;
	if (strcmp(name, "fat")==0) {
		minl = sizeof fh2->recattr;
		if (size==0)
			return minl;
		if (size<minl)
			return -ERANGE;
		fh_info = (struct ods5_fh_info *)inode->i_private;
		memcpy(buffer, &fh_info->recattr, minl);
	} else if (strcmp(name, "fh")==0) {
		minl = sizeof *fh2;
		if (size==0)
			return minl;
		if (size<minl)
			return -ERANGE;
		bh = ods5_read_fh(inode->i_sb, inode->i_ino, &fh2);
		if (bh == NULL) {
			ods5_debug(1, "ods5_read_fh for ino %lu failed\n", inode->i_ino);
			return -EIO;
		}
		/* its a file header, verify it */
		fid = mkfid(inode);
		if (!is_used_fh2(fh2, fid)) {
			brelse(bh);
			return -EBADF;
		}
		memcpy(buffer, fh2, minl);
		brelse(bh);
	} else
		return -EOPNOTSUPP;
	return minl;
}

long ods5_ioctl (struct file * filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode;
	struct ods5_fh2 *fh2;
	struct buffer_head *bh;
	struct ods5_fid fid;
	vms_long not_copied;
	struct ods5_fh_info *fh_info;

	ods5_debug(2, "command: %u\n", cmd);
	ods5_debug(2, "argument: %lu\n", arg);

	inode = filp->f_path.dentry->d_inode;
	switch (cmd) {
	    case ODS5_IOC_GETFAT:
		fh_info = (struct ods5_fh_info *)inode->i_private;
		not_copied = copy_to_user((void*)arg, &fh_info->recattr, sizeof fh2->recattr);
		if (not_copied != 0) {
			ods5_debug(3, "user addr %p, iobytes: " FMT_size_t ", not copied: %d\n",
				   (void*)arg, sizeof fh2->recattr, not_copied);
			return -EFAULT;
		}
		break;		    
	    case ODS5_IOC_GETFH:
		    bh = ods5_read_fh(inode->i_sb, inode->i_ino, &fh2);
		    if (bh == NULL) {
			ods5_debug(1, "ods5_read_fh for ino %lu failed\n", inode->i_ino);
			return -EIO;
		}
		/* its a file header, verify it */
		fid = mkfid(inode);
		if (!is_used_fh2(fh2, fid))
			return brelse(bh), -EBADF;
		/* copy fh */
		not_copied = copy_to_user((void*)arg, fh2, sizeof *fh2);
		brelse(bh);
		if (not_copied != 0) {
			ods5_debug(3, "user addr %p, iobytes: " FMT_size_t ", not copied: %d\n",
				   (void*)arg, sizeof *fh2, not_copied);
			return -EFAULT;
		}
		break;
	    default:
		return -ENOTTY;
	}
	return 0;
}
