/*
 * linux/fs/ods5/inode.c
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
#include <linux/xattr.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>

#include <linux/sched.h>
#include <asm/current.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/nls.h>

#include "./ods5_fs.h"
#include "./ods5.h"

#ifdef CONFIG_LBDAF
# define FMT_blkcnt_t "%llu"
#else
# define FMT_blkcnt_t "%lu"
#endif

/*
 * Handling of UCS-2 names, vtf7 mode:
 * The input name fname represents an UCS-2 name if it contains the escape
 * character '?', for example ?03B1.; - a filename with a single UCS-2
 * character.
 * Handling of UCS-2 names, utf8 mode:
 * The input name fname is converted to UCS-2, the UCS-2 encoding represents
 * an ODS-5 UCS-2 name if it contains non-zero-values in the upper half of the
 * UCS-2 word (short),  for example U+03B1 - a non-ISO Latin-1 character.
 *
 * A name in the directory is an ODS-5 UCS-2 name if the nametype is DIR_UCS2.
 * Consider four posible combinations:
 * 1) fname non-UCS-2, name in directory non-UCS-2: caseblind ASCII compare.
 * 2) fname non-UCS-2, name in directory UCS-2: convert fname to UCS-2 and
 *    compare - caseblind in ASCII characters;
 *    there is no indication that there are UCS-2 names in a directory,
 *    so the convert is done when the first UCS-2 shows.
 * 3) fname contains '?', name in directory non-UCS-2: convert fname to UCS-2,
 *    convert every name in the directory to UCS-2 and compare - caseblind
 *    in ASCII characters
 * 4) fname contains '?', name in directory UCS-2: convert fname to UCS-2 and
 *    compare - caseblind in ASCII characters
 *
 * The caseblind ASCII compare is used to match the order of filenames
 * in the directory: a, B, b, C, ...; if a caseblind compare returns a
 * greater name the lookup can be stopped: there will be no other match
 * in the directory. After a caseblind match a case sensitive match is
 * necessary to find a real match.
 * For temporary storage use character arrays of ODS5_FILENAME_LEN bytes. Even
 * long (>ODS5_FILENAME_LEN/2) ASCII input names can be converted to fit into
 * this array just for the compare, as the UCS-2 name to compare with can't
 * be any longer.
 */

/*
 * Convert a filename with ucs-2 escape sequences to an ucs-2 filename, as is and
 * in uppercase
 */
static int escfn_to_ucs(char *ucs2_fn, const char *fname, int fl) {
	int i, l;
	for (i= l=0; i<fl; ) {
		if (fname[i]=='?') {
			i++;
			if (fname[i]>'9')
				ucs2_fn[l+1] = fname[i]-'A'+10;
			else
				ucs2_fn[l+1] = fname[i]-'0';
			i++;
			ucs2_fn[l+1]<<= 4;
			if (fname[i]>'9')
				ucs2_fn[l+1] += fname[i]-'A'+10;
			else
				ucs2_fn[l+1] += fname[i]-'0';
			i++;
			if (fname[i]>'9')
				ucs2_fn[l] = fname[i]-'A'+10;
			else
				ucs2_fn[l] = fname[i]-'0';
			i++;
			ucs2_fn[l]<<= 4;
			if (fname[i]>'9')
				ucs2_fn[l] += fname[i]-'A'+10;
			else
				ucs2_fn[l] += fname[i]-'0';
			i++;
			l+=2;
		}
		else {
			ucs2_fn[l] = fname[i++];
			l++;
			ucs2_fn[l] = 0;
			l++;
		}
	}
	return l;
}

/* Convert an utf-8 filename to an ucs-2 filename */
static int utf8fn_to_ucs(unsigned char *ucs2_fn, const char *fname, int fl) {
	int i, l;
	for (i= l=0; i<fl; ) {
		unicode_t u;
		int cl;
		cl = utf8_to_utf32(&fname[i], fl, &u);
		if (cl==-1)
			return -1;
		if (u & 0xffff0000)
			return -1;
		if (l>=ODS5_FILENAME_LEN)
			return -1;
		ucs2_fn[l++] = (unsigned char)u;
		ucs2_fn[l++] = (unsigned char)(u>>8);
		i += cl;
	}
	return l;
}

static int upcase2cmp(unsigned char *upper_fn, const unsigned char *name, int minl) {
	int i;
	int r;
	for (i=0; i<minl; i++) {
		r = upper_fn[i]-toupper(name[i]);
		if (r==0)
			continue;
		ods5_debug(3, "i: %d, upper_fn[i]: %02x, name[i]: %02x, toupper: %02x\n",
				i, upper_fn[i], name[i], toupper(name[i]));
		return r;
	}
	return 0;
}

/* find a matching name for fname;version in block */
static struct ods5_fid *ods5_find_match(char block[ODS5_BLOCK_SIZE],
					const unsigned char *fname,
					vms_long fl, vms_long version, int utf8)
{
	struct ods5_dir *dir;
	vms_long fnoff;
	vms_long veroff;
	struct ods5_dirent *dirval;
	int i, minl, l;
	unsigned char ucs2_fn[ODS5_FILENAME_LEN];
	unsigned char upper_fn[ODS5_FILENAME_LEN];
	unsigned char isl_fn[ODS5_FILENAME_LEN];
	enum { isl1, ucs2 } fn_mode;

	fn_mode = isl1;
	if (utf8) {
		fl = utf8fn_to_ucs(ucs2_fn, fname, fl);
		if (fl==-1)
			return (struct ods5_fid *)-1;
		for (i=0, l=0; i<fl; i+=2) {
			if (ucs2_fn[i+1]!=0) {
				fn_mode = ucs2;
				break;
			}
			isl_fn[l] = ucs2_fn[i];
			upper_fn[l++] = toupper(ucs2_fn[i]);
		}
		if (fn_mode==isl1) {
			fl = l;
			fname = isl_fn;
		}
		if (fn_mode==isl1) {
			ods5_debug(2, "utf8, upper_fn: %.*s\n", fl, upper_fn);
		} else {
			ods5_debug(2, "%s", "utf8, ucs2_fn\n");
			for (i=0; i<fl; i++) {
				if (ucs2_fn[i])
					ods5_debug(2, " [%d]: 0x%x\n", i, ucs2_fn[i]);
			}
		}
	} else {
		int ucs2_len = 0;
		for (i=0; i<fl; i++) {
			if (fname[i]=='?') {
				fn_mode = ucs2;
				i += 4;
			}
			ucs2_len += 2;
			if (fn_mode!=ucs2)
				upper_fn[i] = toupper(fname[i]);
		}
		if (fn_mode==ucs2) {
			if (ucs2_len > ODS5_FILENAME_LEN)
				return (struct ods5_fid *)-1;
			fl = escfn_to_ucs(ucs2_fn, fname, fl);
			ods5_debug(2, "%s", "vtf8, ucs2_fn\n");
			for (i=0; i<fl; i++) {
				if (ucs2_fn[i])
					ods5_debug(2, " [%d]: 0x%x\n", i, ucs2_fn[i]);
			}
		}
		else
			ods5_debug(2, "vtf8, upper_fn: %.*s\n", fl, upper_fn);
	}

	/* walk through all the records, stop if record length is 0xffff */
	for (fnoff = 0; block[fnoff] != -1 && block[fnoff + 1] != -1;
	     fnoff += dir->size + sizeof dir->size) {

		l = fl;
		dir = (struct ods5_dir *)(block + fnoff);
		ods5_debug(2, "flags.nametype: %d\n", dir->flags.nametype);
		if (dir->flags.nametype==DIR_UCS2) {
			ods5_debug(2, "nametype: <UCS2>\n");
			switch (fn_mode) {
			    case isl1:
				continue;
			    case ucs2:
				break;
			}
		}
		else
			ods5_debug(2, "name: %.*s\n", dir->namecount, dir->name);

		/*
		 * For isl-1 names,
		 * compare upcased names: the given filename with the name in
		 * the directory entry;
		 * if the name in the directory is greater than
		 * the filename, then stop, there can't be any match here and
		 * in further directory entries - that's the whole purpose of
		 * doing this (blindcase/upcased) compare;
		 * if the filename is smaller, then this can't be a match
		 * but continue with the next directory entry
		 */
		if (fn_mode==isl1) {
		    minl = (l > dir->namecount) ? dir->namecount : l;
			i = upcase2cmp(upper_fn, dir->name, minl);
			ods5_debug(2, "upcase2cmp: %d\n", i);
			if (i < 0)
				return (struct ods5_fid *)-1;
			if (i > 0)
				continue;
		}
		if (l != dir->namecount)
			continue;

		/* this is ucs-2 or an isl-1 caseblind match */
		switch (fn_mode) {
		    case ucs2:
				ods5_debug(2, "l: %d, fname: <UCS-2>, dir->name: <UCS-2>\n", l);
			break;
		    case isl1:
				ods5_debug(2, "l: %d, fname: %.*s, dir->name: %.*s\n", l, fl,
					    fname, dir->namecount, dir->name);
			break;
		}
		/* finally, do an exact compare */
		if (dir->flags.nametype==DIR_UCS2) {
			if (memcmp(ucs2_fn, dir->name, l)!=0)
				continue;
		} else {
			if (memcmp(fname, dir->name, l)!=0)
				continue;
		}
		ods5_debug(2, "%s\n", "filename match");
		/* name match, now look for a matching version */
		veroff = fnoff + offsetof(struct ods5_dir,name) + ((dir->namecount + 1) & ~1);
		dirval = (struct ods5_dirent *)(block + veroff);
		ods5_debug(2, "versions: " FMT_size_t "\n",
			   (dir->size + sizeof dir->size -
			    dir->namecount) / sizeof *dirval);
		for (;
		     dirval <
		     (struct ods5_dirent *)(block + fnoff + dir->size +
					    sizeof dir->size); dirval++) {
			/* match */
			if (dirval->version == version) {
				if (dir->flags.nametype==DIR_UCS2) {
					ods5_debug(2, "=> '<UCS-2>;%d', (%d,%d,%d)\n",
						   dirval->version,
						   dirval->fid.num +
						   (dirval->fid.nmx << 16),
						   dirval->fid.seq, dirval->fid.rvn);
				} else {
						ods5_debug(2, "=> '%.*s;%d', (%d,%d,%d)\n",
							   dir->namecount, dir->name,
							   dirval->version,
							   dirval->fid.num +
							   (dirval->fid.nmx << 16),
							   dirval->fid.seq, dirval->fid.rvn);
				}
				return &dirval->fid;
			}
			/* version too low, no match possible */
			if (dirval->version < version)
				return (struct ods5_fid *)-1;
		}
		/*
		 * no version match up to now, but if this was the last record,
		 * maybe the matching filename is continued in the next block
		 */
		if (dirval->version == (vms_word) - 1)
			return NULL;
		/* no match */
		return (struct ods5_fid *)-1;
	}
	return NULL;
}
/*
 * Symlink matches are different. Input is a filename without a version.
 * A match can be
 * - the exact match with the filename, highest version
 * - the exact match with a directory (.DIR appended to the filename), highest
 *   version, which usually is 1.
 * As far as I know, you can't have UCS-2 names in a symbolic link, so I do not
 * try to match them.
 * You can have ISL-1 names in a symbolic link.
 * So for utf8 support it is necessary to convert the utf8 to ucs-2, check whether
 * it is a non-ISO Latin-1 name, etc.
 */

/* More or less the same as ods5_find_match but just the highest version */

static struct ods5_fid *find_syml_match(char block[ODS5_BLOCK_SIZE],
					const unsigned char *fname,
					vms_long fl, int utf8)
{
	struct ods5_dir *dir;
	vms_long fnoff;
	vms_long veroff;
	struct ods5_dirent *dirval;
	int i, minl;

	unsigned char ucs2_fn[ODS5_FILENAME_LEN];
	unsigned char upper_fn[ODS5_FILENAME_LEN];
	unsigned char isl_fn[ODS5_FILENAME_LEN];
	int dot_seen;
	int dl;

	dot_seen = 0;
	if (utf8) {
		int l;
		ods5_debug(2, "fl: %d\n", fl);
		fl = utf8fn_to_ucs(ucs2_fn, fname, fl);
		ods5_debug(2, "utf8fn_to_ucs->fl: %d\n", fl);
		if (fl==-1)
			return (struct ods5_fid *)-1;
		for (i=0, l=0; i<fl; i+=2) {
			if (ucs2_fn[i+1]!=0)
				return (struct ods5_fid *)-1;
			isl_fn[l] = ucs2_fn[i];
			upper_fn[l++] = toupper(ucs2_fn[i]);
			if (dot_seen)
				continue;
			dot_seen = ucs2_fn[i]=='.';
		}
		ods5_debug(2, "isl1, upper_fn, l: %d\n", l);
		fl = dl = l;
		fname = isl_fn;
	} else {
		if (fl>ODS5_FILENAME_LEN)
			return (struct ods5_fid *)-1;
		dl= fl;
		for (i=0; i<fl; i++) {
			upper_fn[i] = toupper(fname[i]);
			if (dot_seen)
				continue;
			dot_seen = upper_fn[i]=='.';
		}
	}
	/*
	 * VMS/ODS filenames consist of a name and a type, for example "readme"
	 * and ".txt" which give the filename "readme.txt". (There is more, but
	 * for explaining the below code file versions can be ignored The name
	 * part of a filename can be emtpy, the type part can not. That is,
	 * VMS/ODS filenames always have a type, such as ".txt". The
	 * minimal/shortest type is ".". If there is a Posix filename
	 * without a dot, it has to be matched with a VMS/ODS filename with
	 * a trailing dot: "readme" will be matched by "readme.".
	 * If there is a trailing dot in the Posix filename
	 * it has to be matched with a dot in the name: "readme."
	 * will be matched by "readme.." (shown by the DIR command as "readme^..")
	 * - it can't be matched with the VMS/ODS filename "readme.", which would
	 * create an ambiguous mapping of filenames.
	 */
	if (dot_seen==0 || upper_fn[fl-1]=='.') {
		if (fl+1>ODS5_FILENAME_LEN)
			return (struct ods5_fid *)-1;
		upper_fn[fl]= '.';
		fl++;
	}
	ods5_debug(2, "upper_fn: %.*s\n", fl, upper_fn);
	/* walk through all the records, stop if record length is 0xffff */
	for (fnoff = 0; block[fnoff] != -1 && block[fnoff + 1] != -1;
	     fnoff += dir->size + sizeof dir->size) {
		dir = (struct ods5_dir *)(block + fnoff);
		ods5_debug(2, "flags.nametype: %d\n", dir->flags.nametype);
		if (dir->flags.nametype==DIR_UCS2)
			continue;
		ods5_debug(2, "compare with name: %.*s\n", dir->namecount,
			   dir->name);

		minl = (fl > dir->namecount) ? dir->namecount : fl;
		ods5_debug(2, "dl: %d, fl: %d, minl: %d\n", dl, fl, minl);
		i = upcase2cmp(upper_fn, dir->name, minl);

		if (i < 0)
			return (struct ods5_fid *)-1;
		if (i > 0)
			continue;

		if (fl != dir->namecount && dl+4 != dir->namecount)
			continue;

		/*
		 * finally do an exact compare for the file or the directory;
		 * dl is either equal to fl or less by one;
		 * comparing characters at index 0..dl-1 and at index fl-1 is
		 * enough for an exact file match;
		 * comparing characters at index 0..dl-1 and the directory
		 * entry with ".DIR" is enough for an exact directory match
		 */
		ods5_debug(2, "fname: %s, dir->name: %*s\n", fname, dir->namecount, dir->name);
		if (memcmp(fname, dir->name, dl)!=0)
			continue;
		else if (fl!=dl && dir->name[fl-1]!='.')
			continue;
		else if (fl!=dir->namecount && memcmp(&dir->name[dl],".DIR",4)!=0)
			continue;

		/* highest version is a match */
		veroff = fnoff + offsetof(struct ods5_dir,name) + ((dir->namecount + 1) & ~1);
		dirval = (struct ods5_dirent *)(block + veroff);
		ods5_debug(2, "version: %d\n", dirval->version);
		return &dirval->fid;
	}
	return NULL;
}

/* same as ods5_lookup but calls find_syml_match (a match without version) */
static struct dentry *symlink_lookup(struct inode *dir, struct dentry *dentry)
{
	vms_long vbn;
	vms_long lbn, unused;
	char *block;
	vms_long fl;
	unsigned long ino;
	struct ods5_sb_info *sb_info;

	fl = dentry->d_name.len;
	if (fl > ODS5_FILENAME_LEN) {
		d_add(dentry, NULL);
		return NULL;
	}

	ods5_debug(2, "fl: %d\n", fl);

	vbn = 0;
	sb_info = get_sb_info(dir->i_sb);
	while (1) {
		/* map the vbn */
		vbn += 1;
		ods5_debug(3, "directory, vbn: %d, blocks: " FMT_blkcnt_t ", size: %lld\n", vbn,
			   dir->i_blocks, dir->i_size);
		if ((vbn * ODS5_BLOCK_SIZE) > dir->i_size) {
			d_add(dentry, NULL);
			return NULL;
		}
		if (!mapvbn(dir->i_sb, dir, vbn, &lbn, &unused))
			return ERR_PTR(-EIO);

		/* read the block */
		{
			struct buffer_head *bh;
			struct ods5_fid *fid;
			vms_long iopos;

			bh = ods5_bread(dir->i_sb, lbn, &iopos);
			if (bh == NULL) {
				ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
				return ERR_PTR(-EIO);
			}
			block = bh->b_data + iopos;

			/* fid points into the block, it is valid as long as bh is valid */
			fid = find_syml_match(block, dentry->d_name.name, fl, sb_info->utf8);
			if (fid == (struct ods5_fid *)-1) {
				d_add(dentry, NULL);
				brelse(bh);
				return NULL;
			}
			if (fid) {
				struct inode *inode;
				ino = fid->num + (fid->nmx << 16);
				inode = ods5_iget (dir->i_sb, ino, fid->seq);
				brelse(bh);
				if (!inode)
					return ERR_PTR(-ENOENT);
				d_add(dentry, inode);
				return NULL;
			}
			brelse(bh);
		}
	}
	d_add(dentry, NULL);
	return NULL;
}

static struct dentry *ods5_lookup(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	vms_long vbn;
	vms_long lbn, unused;
	char *block;
	vms_long fl;
	vms_long version;
	int i;
	unsigned long ino;
	struct ods5_sb_info *sb_info;
	const unsigned char *delim;

	ods5_debug(3, "dir->i_ino: %ld\n", dir->i_ino);
	ods5_debug(3, "dentry->d_name.len: %d\n", dentry->d_name.len);
	ods5_debug(3, "dentry->d_name.hash: 0x%x\n", dentry->d_name.hash);
	ods5_debug(2, "dentry->d_name.name: '%.*s'\n", dentry->d_name.len,
		   dentry->d_name.name);

	sb_info = get_sb_info(dir->i_sb);
	if (sb_info->dotversion)
		delim = strrchr(dentry->d_name.name, '.');
	else
		delim = strrchr(dentry->d_name.name, ';');
	if (delim)
		fl = delim - dentry->d_name.name;
	else if (sb_info->syml)
		return symlink_lookup (dir, dentry);
	else {
		d_add(dentry, NULL);
		return NULL;
	}
	if (fl==0) {
		d_add(dentry, NULL);
		return NULL;
	}
	if (fl > ODS5_FILENAME_LEN) {
		d_add(dentry, NULL);
		return NULL;
	}

	version = 0;
	/* at fl there is the delimiter, do an atoi() of what follows */
	for (i = fl + 1; i < dentry->d_name.len; i++)
		if ((dentry->d_name.name[i] < '0')
		    || (dentry->d_name.name[i] > '9'))
			break;
		else
			version = version * 10 + (dentry->d_name.name[i] - '0');
	ods5_debug(2, "fl: %d, version: %d\n", fl, version);
	if (version < 1 || version > 32767 || i < dentry->d_name.len) {
		d_add(dentry, NULL);
		return NULL;
	}

	vbn = 0;
	while (1) {
		/* map the vbn */
		vbn += 1;
		ods5_debug(3, "directory, vbn: %d, blocks: " FMT_blkcnt_t ", size: %lld\n", vbn,
			   dir->i_blocks, dir->i_size);
		if ((vbn * ODS5_BLOCK_SIZE) > dir->i_size) {
			d_add(dentry, NULL);
			return NULL;
		}
		if (!mapvbn(dir->i_sb, dir, vbn, &lbn, &unused))
			return ERR_PTR(-EIO);

		/* read the block */
		{
			struct buffer_head *bh;
			struct ods5_fid *fid;
			vms_long iopos;

			bh = ods5_bread(dir->i_sb, lbn, &iopos);
			if (bh == NULL) {
				ods5_debug(1, "ods5_bread of lbn %d failed\n", lbn);
				return ERR_PTR(-EIO);
			}
			block = bh->b_data + iopos;

			/* fid points into the block, it is valid as long as bh is valid */
			fid = ods5_find_match(block, dentry->d_name.name, fl,
					      version, sb_info->utf8);

			if (fid == (struct ods5_fid *)-1) {
				d_add(dentry, NULL);
				brelse(bh);
				return NULL;
			}
			if (fid) {
				struct inode *inode;
				ino = fid->num + (fid->nmx << 16);
				inode = ods5_iget (dir->i_sb, ino, fid->seq);
				brelse(bh);
				if (!inode)
					return ERR_PTR(-ENOENT);
				d_add(dentry, inode);
				return NULL;
			}
			brelse(bh);

		}
	}
	d_add(dentry, NULL);
	return NULL;
}

int ods5_readlink(struct dentry *dentry, char __user *buffer, int buflen) {
	struct inode *inode;
	size_t fbytes;
	vms_long vbn, vbnextends;
	vms_long lbn, lbnextends;
	vms_long iopos, iobytes, xbytes;
	struct buffer_head *bh;
	char *lbdata;
	unsigned char *utf8_buffer;
	unsigned int not_copied;

	ods5_debug(2, "%s\n","start");

	/* do not read other than symbolic links */
	inode = dentry->d_inode;
	if (!S_ISLNK(inode->i_mode))
		return -EINVAL;
	if (buflen < 0)
		return -EINVAL;

	/* todo: copy but truncate to buflen if the buffer is too small, */
	/* which then sets ENAMETOOLONG */
	fbytes = inode->i_size;
	if (fbytes == 0)
		return 0;
	if (fbytes != inode->i_size)
		return -EINVAL;;
	if (buflen < fbytes)
		return -EINVAL;

	iobytes = 0;

	/* for mapping the file calculate the vbn to read */
	vbn = 1;
	vbnextends = (fbytes + ODS5_BLOCK_SIZE - 1) >> ODS5_BLOCK_SHIFT;
	ods5_debug(2, "vbn: %d, vbnextends: %d\n", vbn, vbnextends);

	/* as long as there aren't all bytes transfered */
	xbytes = 0;
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
		iobytes = inode->i_sb->s_blocksize - iopos;

		/* limit to what is mapped */
		if (iobytes > (lbnextends << ODS5_BLOCK_SHIFT))
			iobytes = (lbnextends << ODS5_BLOCK_SHIFT);
		if (iobytes > (fbytes - xbytes))
			iobytes = fbytes - xbytes;
		if (get_sb_info(inode->i_sb)->utf8) {
			utf8_buffer = kmalloc(fbytes+2, GFP_NOFS);
			if (utf8_buffer) {
				vms_long rlen;
				rlen = ods5_isl_to_utf(utf8_buffer,fbytes+2,lbdata,iobytes);
				if (rlen==0 || xbytes+rlen>buflen)
					not_copied = iobytes;
				else {
					not_copied = copy_to_user(&buffer[xbytes], utf8_buffer, rlen);
					xbytes += rlen-iobytes;
					kfree(utf8_buffer);
				}
			}
		} else
			not_copied = copy_to_user(&buffer[xbytes], lbdata, iobytes);
		brelse(bh);
		if (not_copied != 0) {
			ods5_debug(3, "data at %p, iobytes: %d, not copied: %d\n",
				   lbdata, iobytes, not_copied);
			return -EFAULT;
		}

		ods5_debug(2,
		    "copy_to_user, &buf[%d], iobytes: %d\n", xbytes, iobytes);
		/* update transfered bytes, position to next vbn in file */
		xbytes += iobytes;
		vbn += iobytes >> ODS5_BLOCK_SHIFT;
		vbnextends -= iobytes >> ODS5_BLOCK_SHIFT;
	}			/* while xbytes<fbytes */
	ods5_debug(2, "return, fbytes: " FMT_size_t "\n", fbytes);
	return fbytes;
}

/* the only difference is the memcpy; todo: make common sub-function */
static int readlink(struct dentry *dentry, char *buffer, int buflen) {
	struct inode *inode;
	size_t fbytes;
	vms_long vbn, vbnextends;
	vms_long lbn, lbnextends;
	vms_long iopos, iobytes, xbytes;
	struct buffer_head *bh;
	char *lbdata;

	ods5_debug(2, "%s\n","start");

	/* do not read other than symbolic links */
	inode = dentry->d_inode;
	if (!S_ISLNK(inode->i_mode))
		return -EINVAL;
	if (buflen < 0)
		return -EINVAL;

	/* todo: copy but truncate to buflen if the buffer is too small, */
	/* which then sets ENAMETOOLONG */
	fbytes = inode->i_size;
	if (fbytes == 0)
		return 0;
	if (fbytes != inode->i_size)
		return -EINVAL;;
	if (buflen < fbytes)
		return -EINVAL;

	iobytes = 0;

	/* for mapping the file calculate the vbn to read */
	vbn = 1;
	vbnextends = (fbytes + ODS5_BLOCK_SIZE - 1) >> ODS5_BLOCK_SHIFT;
	ods5_debug(3, "vbn: %d, vbnextends: %d\n", vbn, vbnextends);

	/* as long as there aren't all bytes transfered */
	xbytes = 0;
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
		iobytes = inode->i_sb->s_blocksize - iopos;

		/* limit to what is mapped */
		if (iobytes > (lbnextends << ODS5_BLOCK_SHIFT))
			iobytes = (lbnextends << ODS5_BLOCK_SHIFT);
		if (iobytes > (fbytes - xbytes))
			iobytes = fbytes - xbytes;
		memcpy (&buffer[xbytes], lbdata, iobytes);
		brelse(bh);

		ods5_debug(2,
		    "copied, &buf[%d], iobytes: %d\n", xbytes, iobytes);
		/* update transfered bytes, position to next vbn in file */
		xbytes += iobytes;
		vbn += iobytes >> ODS5_BLOCK_SHIFT;
		vbnextends -= iobytes >> ODS5_BLOCK_SHIFT;
	}			/* while xbytes<fbytes */
	ods5_debug(2, "return, fbytes: " FMT_size_t "\n", fbytes);
	return fbytes;
}

const char * ods5_get_link (struct dentry *d, struct inode *inode, struct delayed_call *done) {
	unsigned int len;
	unsigned int rlen;
	char *symlink;
	char *utf8_symlink;
	struct ods5_sb_info *sb_info;

	ods5_debug(3, "dentry: 0x%p\n", d);
	if (!d)
		return ERR_PTR(-ECHILD);

	/* max appended string is ".DIR;0" */
	len = d->d_inode->i_size + 7;
	symlink = kmalloc(len, GFP_NOFS);
	sb_info = get_sb_info(d->d_sb);
	if (sb_info->utf8)
		utf8_symlink = kmalloc(len+2, GFP_NOFS);
	else
		utf8_symlink = NULL;
	if (!symlink)
		symlink = ERR_PTR(-ENOMEM);
	else {
		rlen = readlink(d,symlink,len);
		if (rlen==0) {
			ods5_debug(1, "%s\n", "failed to read symlink");
			kfree(symlink);
			if (!utf8_symlink)
				kfree(utf8_symlink);
			symlink = ERR_PTR(-EIO);
		}
		else {
			symlink[rlen]= 0;
			if (utf8_symlink) {
				rlen = ods5_isl_to_utf(utf8_symlink,len+2,symlink,rlen);
				if (rlen) {
					kfree(symlink);
					symlink = utf8_symlink;
				}
			}
			ods5_debug(2, "set_link: '%s'\n", symlink);
		}
	}
	set_delayed_call(done, kfree_link, symlink);
	return symlink;
}

struct inode_operations ods5_inode_operations = {
	.lookup = ods5_lookup,
};
struct inode_operations ods5_inode_symlink_ops = {
	.readlink = ods5_readlink,
	.get_link = ods5_get_link,
};
