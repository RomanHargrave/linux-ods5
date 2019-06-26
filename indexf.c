/*
 * linux/fs/ods5/indexf.c
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

// Check: can the index file header have an extension header (mapped by
// the first one)?
//

#include <linux/fs.h>
#include <linux/slab.h>

#include "./ods5_fs.h"
#include "./ods5.h"

/*
 * Look up the lbn for a given vbn in the mapping information, aka
 * retrieval pointers
 */
static int lbn_lookup(union ods5_fm2 *fm2, vms_byte map_inuse, vms_long vbn, vms_long * xlbn,
		  vms_long * extent, vms_long * sum)
{
	vms_long lbn;
	vms_long count;
	vms_word *wp;
	int i;

	lbn = count = 0;
	wp = (vms_word *) fm2;
	for (i = 0; i < map_inuse;) {
		switch (fm2->format0.format) {
		case 0:
			ods5_debug(3, "0x%04x\n", wp[i]);
			i += 1;
			ods5_debug(3, "format: %d\n", fm2->format0.format);
			break;
		case 1:
			ods5_debug(3, "0x%04x 0x%04x\n", wp[i], wp[i+1]);
			count = fm2->format1.count + 1;
			i += 2;
			ods5_debug(3, "format: %d, count: %d\n",
				   fm2->format0.format, count);
			break;
		case 2:
			ods5_debug(3, "0x%04x 0x%04x 0x%04x\n", wp[i], wp[i+1],
				   wp[i+2]);
			count = fm2->format2.count + 1;
			i += 3;
			ods5_debug(3, "format: %d, count: %d\n",
				   fm2->format0.format, count);
			break;
		case 3:
			ods5_debug(3, "0x%04x 0x%04x 0x%04x 0x%04x\n",
				   wp[i], wp[i+1], wp[i+2], wp[i+3]);
			count =
			    (fm2->format3.highcount << 16) +
			    fm2->format3.lowcount + 1;
			i += 4;
			ods5_debug(3, "format: %d, count: %d\n",
				   fm2->format0.format, count);
			break;
		}
		ods5_debug(2, "vbn: %d, sum+count: %d\n", vbn, *sum + count);
		if (vbn <= *sum + count) {
			switch (fm2->format0.format) {
			case 0:
				break;
			case 1:
				lbn =
				    (fm2->format1.highlbn << 16) +
				    fm2->format1.lowlbn;
				break;
			case 2:
				lbn = fm2->format2.lbn;
				break;
			case 3:
				lbn = fm2->format3.lbn;
				break;
			}
			*xlbn = lbn + (vbn - *sum) - 1;
			*extent = count - (*xlbn - lbn);
			ods5_debug(2, "vbn: %d, mapped by lbn: %d, extent: %d\n",
				   vbn, *xlbn, *extent);
			return 1;
		}
		*sum += count;
		fm2 = (union ods5_fm2 *) & wp[i];
	}
	return 0;
}

/*
 * Map a file vbn (1,2,...) to a disk lbn (0,1,...) plus extent
 */
int mapvbn(struct super_block *sb, struct inode *inode, vms_long vbn,
	   vms_long * lbn, vms_long * extent)
{
	vms_long sum;
	struct ods5_fh_info *fh_info;

	sum = 0;
	fh_info = (struct ods5_fh_info *)inode->i_private;
	
	if (lbn_lookup(&fh_info->ext.map[0], fh_info->ext.map_inuse,
		       vbn, lbn, extent, &sum))
	    return 1;
       else {
	       struct buffer_head *bh;
	       struct ods5_fh2 *fh2;
	       struct ods5_ext_info *ext, *next;
	       int fnum;
	       ext = &fh_info->ext;
	       fnum = ext->ext_fid.num + (ext->ext_fid.nmx << 16);
	       ods5_debug(2, "ino %lu, extension header %d\n", inode->i_ino, fnum);
	       if (fnum == 0)
		       return 0;
	       while (1) {
		       ods5_debug(2, "next ext %p\n", ext->next);
		       if (ext->next!=NULL) {
			       ext = ext->next;
			       if (lbn_lookup(&ext->map[0], ext->map_inuse,
					      vbn, lbn, extent, &sum))
				       return 1;
			       fnum = ext->ext_fid.num + (ext->ext_fid.nmx << 16);
			       ods5_debug(2, "extension header %d\n", fnum);
			       if (fnum == 0)
				       return 0;
			       continue;
		       }
		       bh = ods5_read_fh(inode->i_sb, fnum, &fh2);
		       if (bh == NULL) {
			       ods5_debug(1, "ods5_read_fh for ino %d failed\n", fnum);
			       return 0;
		       }
		       next = kmalloc (sizeof *next+sizeof(vms_word)*fh2->map_inuse, GFP_NOFS);
		       ods5_debug(2, "kmalloc next %p\n", next);
		       if (next==NULL) {
			       brelse (bh);
			       return 0;
		       }
		       next->next = NULL;
		       memcpy (&next->ext_fid, &fh2->ext_fid, sizeof next->ext_fid);
		       next->map_inuse = fh2->map_inuse;
		       memcpy (&next->map[0], &((vms_word*)fh2)[fh2->mpoffset], 
			       sizeof(vms_word)*fh2->map_inuse);
		       if (down_interruptible(&fh_info->ext_lock)==-EINTR) {
			       kfree (next);
			       return 0;
		       }
		       if (ext->next!=NULL) {
			       ods5_debug(2, "found next %p in mutex\n", ext->next);
			       up(&fh_info->ext_lock);
			       kfree (next);
			       continue;
		       }
		       ext->next = next;
		       wmb();
		       up(&fh_info->ext_lock);
	       }
	    }
}

/*
 * Check if the passed structure is a valid used ODS5 file header
 */
int is_used_fh2(struct ods5_fh2 * fh2, struct ods5_fid fid)
{
	int i;
	vms_word checksum;

	/* the checksum must match */
	checksum = 0;
	for (i = 0; i < offsetof(struct ods5_fh2, checksum) / sizeof(vms_word); i++)
		checksum += ((vms_word *) fh2)[i];
	if (checksum != fh2->checksum)
		goto checksum;

	/* the ident offset must point to or beyond the highwater offset */
	if (fh2->idoffset < offsetof(struct ods5_fh2, highwater) / 2)
		goto idoffset;

	/* the ident offset must be less or equal to the map offset */
	if (fh2->idoffset > fh2->mpoffset)
		goto mpoffset;

	/* the map offset must be less or equal to the access offset */
	if (fh2->mpoffset > fh2->acoffset)
		goto acoffset;

	/* the access offset must be less or equal to the reserved offset */
	if (fh2->acoffset > fh2->rsoffset)
		goto rsoffset;

	/* the ODS2 structure level must be two with positive subversion
	   the ODS5 structure level must be five with positive subversion */
	if (((fh2->struclev >> 8) != 5 && (fh2->struclev >> 8) != 2)
	    || (fh2->struclev & 0xff) < 1)
		goto struclev;

	/* the map size must fit between map and access offset */
	if (fh2->map_inuse > (fh2->acoffset - fh2->mpoffset))
		goto map_inuse;

	/* the fid of the file and the one in the file header must match */
	if (fh2->fid.num != fid.num)
		goto fid;
	if (fh2->fid.seq != fid.seq)
		goto fid;
	if (fh2->fid.nmx != fid.nmx)
		goto fid;

	return 1;

      checksum:
	ods5_debug(1, "checksum doesn't match: 0x%04x (calculated: 0x%04x)\n",
		   fh2->checksum, checksum);
	goto invalid;

      idoffset:
	ods5_debug(1, "idoffset: 0x%02x points before highwater: 0x%02x\n",
		   fh2->idoffset, (vms_byte)offsetof(struct ods5_fh2, highwater) / 2);
	goto invalid;

      mpoffset:
	ods5_debug(1, "idoffset: 0x%02x is greater than mpoffset: 0x%02x\n",
		   fh2->idoffset, fh2->mpoffset);
	goto invalid;

      acoffset:
	ods5_debug(1, "mpoffset: 0x%02x is greater than acoffset: 0x%02x\n",
		   fh2->mpoffset, fh2->acoffset);
	goto invalid;

      rsoffset:
	ods5_debug(1, "acoffset: 0x%02x is greater than rsoffset: 0x%02x\n",
		   fh2->acoffset, fh2->rsoffset);
	goto invalid;

      struclev:
	ods5_debug(1, "invalid structure level: 0x%04x\n", fh2->struclev);
	goto invalid;

      map_inuse:
	ods5_debug(1, "map in use: 0x%02x greater than size: 0x%02x\n",
		   fh2->map_inuse, fh2->acoffset - fh2->mpoffset);
	goto invalid;

      fid:
	ods5_debug(1, "fid doesn't match, file: (%d,%d,%d), fh: (%d,%d,%d)\n",
		   fid.num, fid.seq, fid.nmx,
		   fh2->fid.num, fh2->fid.seq, fh2->fid.nmx);
	goto invalid;

      invalid:
	ods5_debug(1, "%s\n", "invalid file header");

	return 0;
}
