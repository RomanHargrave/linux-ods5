/*
 * linux/fs/ods5/dir.c
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
#include <linux/nls.h>

#include "./ods5_fs.h"
#include "./ods5.h"

#define NO_MORE_RECORDS ((vms_word)-1)

static int ucs_to_utf(unsigned char *utf8, unsigned int utf8len, unsigned char *name, vms_byte namelen) {
	int l, m, n;
	l = 0;
	for (n=0; n<namelen && (l+3)<utf8len; n+=2, l+=m) {
		wchar_t u = name[n]+(name[n+1]<<8);
		ods5_debug(3, "u: 0x%x\n", u);
		m = utf32_to_utf8 (u, &utf8[l], 3);
		if (m<0)
			return 0;
	}
	for (m=0; m<l; m++)
		ods5_debug(3, "utf8[%d]: 0x%x\n", m, utf8[m]);
	utf8[l] = 0;
	return l;
}
int ods5_isl_to_utf(unsigned char *utf8, unsigned int utf8len, unsigned char *name, vms_byte namelen) {
	int l, m, n;
	l = 0;
	m = -1;
	for (n=0; n<namelen && name[n] && (l+2)<utf8len; n++, l+=m) {
		wchar_t u = name[n];
		ods5_debug(3, "u: 0x%x\n", u);
		m = utf32_to_utf8 (u, &utf8[l], 2);
		if (m<0)
			return 0;
	}
	ods5_debug(3, "n: %d, name[%d]: 0x%x, l: %d, m: %d\n", n, n, name[n], l, m);
	for (m=0; m<l; m++)
		ods5_debug(3, "utf8[%d]: 0x%x\n", m, utf8[m]);
	utf8[l] = 0;
	return l;
}

static int ods5_readdir(struct file *file, struct dir_context *ctx) {
	struct inode *inode;
	unsigned long ino;
	vms_long vbn;
	vms_long lbn, unused;
	struct ods5_dir *dir;
	struct ods5_dirent *dirval;
	struct buffer_head *bh;
	vms_long iopos;
	char *block;
	vms_long fnoff;	/* file name offset */
	vms_long vfoff;	/* version entry aka value field offset */
	struct ods5_sb_info *sb_info;
	loff_t pos=0; /* gcc can't figure out that it IS correctly initialized */

	/* switch with loff_t seems to require a libc function */
	if (ctx->pos>2)
		pos = ctx->pos;
	else {
		if (ctx->pos==0) {
			if (!dir_emit_dot(file, ctx))
				return 1;
			ctx->pos++;
		}
		if (ctx->pos==1) {
			if (!dir_emit_dotdot(file, ctx))
				return 1;
			ctx->pos++;
		}
		if (ctx->pos==2)
			pos = 0;
	}

	/* at or beyond EOF? */
	inode = file->f_path.dentry->d_inode;
	if (pos >= inode->i_size)
		return 0;

	vbn = (pos >> ODS5_BLOCK_SHIFT) + 1;
	ods5_debug(2, "pos: %Ld, vbn: %d\n", pos, vbn);
	if (!mapvbn(inode->i_sb, inode, vbn, &lbn, &unused))
		return -EBADF;

	/* read the directory vbn, iopos will contain the offset to the vbn */
	sb_info = get_sb_info(inode->i_sb);
	bh = ods5_bread(inode->i_sb, lbn, &iopos);
	if (bh == NULL) {
		ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
		return -EIO;
	}
	block = bh->b_data + iopos;

	/*
	 * Derive the filename and value field offsets within the vbn;
	 * interpret the data as ods5_dir and ods5_dirent, the last one is
	 * the value field
	 */
	fnoff = pos & (ODS5_BLOCK_SIZE - 1);
	vfoff = fnoff & ~1;
	if (*(vms_word *) (block + vfoff) == NO_MORE_RECORDS)
		return brelse(bh), 0;

	if (fnoff != vfoff) {
		fnoff = 0;
		dir = (struct ods5_dir *)block;
		while (fnoff + dir->size + sizeof dir->size < vfoff) {
			fnoff += dir->size + sizeof dir->size;
			dir = (struct ods5_dir *)(block + fnoff);
		}
	} else {
		dir = (struct ods5_dir *)(block + fnoff);
		vfoff = fnoff + offsetof(struct ods5_dir, name)
			+ ((dir->namecount + 1) & ~1);
	}
	dirval = (struct ods5_dirent *)(block + vfoff);

	/* if possible, process one ODS5 directory disk block */
	for (; ; ) {
		char fn[ODS5_FN_STRING_SIZE*3];
		vms_long fl;
		int ucs2;

		ods5_debug(2, "fnoff: %d, vfoff: %d\n", fnoff, vfoff);
		ods5_debug(2, "size: %d\n", dir->size);
		ods5_debug(2, "version limit: %d\n", dir->version);
		ods5_debug(2, "dirflags: %02x\n", *(vms_byte *) & dir->flags);
		ods5_debug(2, "namecount: %d\n", dir->namecount);
		if (dir->flags.nametype==DIR_UCS2) {
			ods5_debug(2, "name: <UCS2>\n");
		} else {
			ods5_debug(2, "name: %.*s\n", dir->namecount, dir->name);
		}
		ino = dirval->fid.num + (dirval->fid.nmx << 16);
		if (ino == ODS5_MFD_INO && sb_info->nomfd) {
			if (dirval[1].version == NO_MORE_RECORDS) {
				/* unlikely, but ... let pos point to next vbn */
				ctx->pos = (loff_t)vbn * ODS5_BLOCK_SIZE;
				ods5_debug(2, "skip MFD, continue with pos: %Ld\n", ctx->pos);
				break;
			} else {
				fnoff += dir->size + sizeof dir->size;
				ctx->pos = ((loff_t)vbn - 1) * ODS5_BLOCK_SIZE + fnoff;
				ods5_debug(2, "skip MFD, continue in block\n");
				dir = (struct ods5_dir *)(block + fnoff);
				vfoff = fnoff + offsetof(struct ods5_dir, name)
					+ ((dir->namecount + 1) & ~1);		
				dirval = (struct ods5_dirent *)(block + vfoff);
				continue;
			}
		}

		/* fill in the vfs dirent */
		ucs2 = dir->flags.nametype==DIR_UCS2;
		if (sb_info->utf8) {
			if (ucs2) {
				fl = ucs_to_utf(fn,sizeof fn,dir->name,dir->namecount);
				if (fl) {
					ods5_debug(2, "name: %s\n", fn);
				} else {
					ods5_debug(1, "%s\n", "ucs_to_utf failed.");
				}
			} else if (dir->flags.nametype==DIR_ISL1) {
				fl = ods5_isl_to_utf(fn,sizeof fn,dir->name,dir->namecount);
				if (fl) {
					ods5_debug(2, "name: %s\n", fn);
				} else {
					ods5_debug(1, "%s\n", "isl_to_utf failed.");
				}
			} else {
				memcpy(fn, dir->name, dir->namecount);
				fl = dir->namecount;
			}
		} else {
			if (ucs2) {
				int i;
				fl= i= 0;
				while (i<dir->namecount) {
					if (dir->name[i+1]==0)
						fn[fl++]= dir->name[i];
					else
						fl+= sprintf (&fn[fl], "?%02X%02X", (unsigned char)dir->name[i+1], (unsigned char)dir->name[i]);
					i+= 2;
				}
			} else {
				memcpy(fn, dir->name, dir->namecount);
				fl = dir->namecount;
			}
		}

		if (sb_info->dotversion)
			fn[fl] = '.';
		else
			fn[fl] = ';';
		fl++;
		fl += sprintf(&fn[fl], "%d", dirval->version);
		ods5_debug(2, "fn: '%s', fl: %d\n", fn, fl);
		if (!dir_emit(ctx, fn, fl, ino, DT_UNKNOWN))
			return brelse(bh), 1;

		if (dirval[1].version == NO_MORE_RECORDS) {
			/* no more entries in this vbn, let pos point to next vbn */
			ctx->pos = (loff_t)vbn * ODS5_BLOCK_SIZE;
			break;
		} else {
			/* let pos point after the current value field */
			vfoff += sizeof *dirval;
			ctx->pos = ((loff_t)vbn - 1) * ODS5_BLOCK_SIZE + vfoff;
			if (vfoff < (fnoff + dir->size + sizeof dir->size))
				ctx->pos++;
			else {
				fnoff = vfoff;
				dir = (struct ods5_dir *)(block + fnoff);
				vfoff = fnoff + offsetof(struct ods5_dir, name)
					+ ((dir->namecount + 1) & ~1);		
			}
			dirval = (struct ods5_dirent *)(block + vfoff);
		}
	}
	ods5_debug(2, "return pos: %Ld\n", ctx->pos);
	brelse(bh);
	return 2;
}

struct file_operations ods5_dir_operations = {
	.read = generic_read_dir,
	.iterate = ods5_readdir,
	.llseek = default_llseek,
};
