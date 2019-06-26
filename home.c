/*
 * linux/fs/ods5/home.c
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
#include <linux/kernel.h>
#include <linux/string.h>

#include "./ods5_fs.h"
#include "./ods5.h"
#include "./v2utime.h"

/* convert vms binary time to string */
static void vms_ctime(vms_quad bintim, char *buf)
{
	struct timespec ts;
	struct tm tm;
	ts = v2utime(bintim);
	time_to_tm(ts.tv_sec, 0, &tm);
	sprintf(buf, "%02d-", tm.tm_mday);
	sprintf(buf + 3, "%.3s-", &"JanFebMarAprMayJunJulAugSepOctNovDec"[tm.tm_mon * 3]);
	sprintf(buf + 3 + 4, "%4ld ", tm.tm_year + 1900);
	sprintf(buf + 3 + 4 + 5, "%02d:", tm.tm_hour);
	sprintf(buf + 3 + 4 + 5 + 3, "%02d:", tm.tm_min);
	sprintf(buf + 3 + 4 + 5 + 3 + 3, "%02d.", tm.tm_sec);
	sprintf(buf + 3 + 4 + 5 + 3 + 3 + 3, "%02ld", ts.tv_nsec / 10000000);
	buf[3 + 4 + 5 + 3 + 3 + 3 + 2] = 0;
}

/*
 * I don't really like the gotos, although they are quite common in other
 * kernel sources. But all the other tricks to do the same are just ugly:
 * the checks should be clear and efficient, the debug information in one
 * place and a nop for non-debug code.
 * I apologize for the 200+ lines of code for this function. I know, it would
 * be acceptable if there is a BIG switch. I hope I'm not forced to rewrite
 * this and to use a switch on the block offset.
 */

/* check if the passed structure is a valid ODS5 home block */
int is_valid_home(struct ods5_home * home)
{
	int curclu;
	int i;
	vms_word checksum;
	char buf[sizeof "dd-mmm-yyyy hh:mm:ss.mm"];

	/* home block lbn must be non-zero */
	if(home->homelbn == 0)
		goto homelbn;

	/* alternate home block lbn must be non-zero */
	if(home->alhomelbn == 0)
		goto alhomelbn;

	/* backup index file header lbn must be non-zero */
	if(home->altidxlbn == 0)
		goto altidxlbn;

	/* structure level must be ODS2: two or ODS5: five and subversion greater or equal one
	 *
	 * That's what "The Book" says. However the subversion isn't checked on VMS, neither in
	 * a mount nor in an ANALYZE/DISK operation. I didn't expect the subversion to be zero
	 * it is on the OpenVMS/Alpha CD for version 8.4 - which makes me wonder how it was
	 * created, anyway. As a result, I shouldn't require a subversion, either. For what it
	 * is worth, the (unexpected) subversion is printed as an info.
	 */
	if((home->struclev >> 8) != 5 && (home->struclev >> 8) != 2)
		goto struclev;
	ods5_info("struclev: %d, %d\n", home->struclev>>8, home->struclev & 0xff);
	ods5_info("cluster factor: %d\n", home->cluster);

	/* home block vbn must be non-zero */
	if(home->homevbn == 0)
		goto homevbn;

	/* backup home block vbn must be in the third cluster */
	curclu = 2;
	if(home->alhomevbn < home->cluster * curclu + 1
			|| home->alhomevbn > home->cluster * (curclu + 1))
		goto alhomevbn;

	/* backup index file header vbn must be in the forth cluster */
	curclu++;
	if(home->altidxvbn < home->cluster * curclu + 1
			|| home->altidxvbn > home->cluster * (curclu + 1))
		goto altidxvbn;

	/* index file bitmap vbn must be in the fifth cluster */
	curclu++;
	if(home->ibmapvbn < home->cluster * curclu + 1
			|| home->ibmapvbn > home->cluster * (curclu + 1))
		goto ibmapvbn;

	/* index file bitmap lbn must be non-zero */
	if(home->ibmaplbn == 0)
		goto ibmaplbn;

	/* maximum number of files must be greater than reserved and less then 2**24 */
	if(home->maxfiles <= home->resfiles || home->maxfiles >= 1 << 24)
		goto maxfiles;

	/* index file bitmap size must be non-zero */
	if(home->ibmapsize == 0)
		goto ibmapsize;

	/* header for file ID n is at CLUSTER*4+IBMAPSIZE+n */
	ods5_debug(1, "=> lbn of file header of INDEXF.SYS: %d\n",
			home->ibmaplbn + home->ibmapsize);
	ods5_debug(1, "=> vbn of file header of INDEXF.SYS: %d\n",
			home->cluster * 4 + home->ibmapsize + 1);

	/* volume sets aren't supported, so relative volume number must be zero */
	if(home->rvn != 0)
		goto rvn;

	/* volume characteristics not supported, but checked for valid bits */
	if(home->volchar != 0) {
		#define BUFLEN sizeof "volchar: readcheck writecheck erase nohighwater class_prot accesstimes hardlinks"
		char buf[BUFLEN] = "volchar:";
		if((home->volchar & ODS5_VOL_READCHECK) != 0)
			strcat (buf, " readcheck");
		if((home->volchar & ODS5_VOL_WRITCHECK) != 0)
			strcat (buf, " writecheck");
		if((home->volchar & ODS5_VOL_ERASE) != 0)
			strcat (buf, " erase");
		if((home->volchar & ODS5_VOL_NOHIGHWATER) != 0)
			strcat (buf, " nohighwater");
		if((home->volchar & ODS5_VOL_CLASS_PROT) != 0)
			strcat (buf, " class_prot");
		if((home->struclev >> 8) == 5) {
			if((home->volchar & ODS5_VOL_ACCESSTIMES) != 0)
				strcat (buf, " accesstimes");
			if((home->volchar & ODS5_VOL_HARDLINKS) != 0)
				strcat (buf, " hardlinks");
			ods5_info("%s\n", buf);
			if((home->volchar & ~0x7f) != 0)
				goto volchar;
		} else { /* (home->struclev >> 8) == 2) */
			ods5_info("%s\n", buf);
			if((home->volchar & ~0x1f) != 0)
				goto volchar;
		}
	}

	/* volume owner not checked */
	ods5_info("volowner: [%d,%d]\n",
			home->volowner.grp, home->volowner.mem);
	/* volume protection not checked */
	ods5_info("protect: system=0x%02x, owner=0x%02x, group=0x%02x, world=0x%02x\n",
			home->protect.system, home->protect.owner, home->protect.group, home->protect.world);
	/* default file protection not used */
	ods5_info("fileprot: system=0x%02x, owner=0x%02x, group=0x%02x, world=0x%02x\n",
			home->fileprot.system, home->fileprot.owner, home->fileprot.group, home->fileprot.world);

	/* volume creation date */
	vms_ctime(home->credate, buf);
	ods5_info("credate: %s\n", buf);

	/* volume revision date */
	if(home->revdate)
		vms_ctime(home->revdate, buf);
	else
		memcpy(buf, "<none>", sizeof "<none>");
	ods5_info("revdate: %s\n", buf);

	checksum = 0;
	for(i = 0; i < offsetof(struct ods5_home, checksum1) / sizeof(short); i++)
		checksum += ((short *)home)[i];
	if(checksum != home->checksum1)
		goto checksum1;

	ods5_info("volname: '%.*s'\n", (int)sizeof home->volname, home->volname);
	ods5_info("ownername: '%.*s'\n", (int)sizeof home->ownername, home->ownername);

	/* format string must be DECFILE11B */
	if(strncmp(home->format, "DECFILE11B  ", sizeof home->format) != 0)
		goto format;

	for(; i < offsetof(struct ods5_home, checksum2) / sizeof(short); i++)
		checksum += ((short *)home)[i];
	if(checksum != home->checksum2)
		goto checksum2;

	return 1;

	/* report a failing check */
	homelbn:
	ods5_info("%s\n", "homelbn, home block lbn is zero");
	goto not_valid;

	alhomelbn:
	ods5_info("%s\n", "alhomelbn, alternate home block lbn is zero");
	goto not_valid;

	altidxlbn:
	ods5_info("%s\n", "altidxlbn, backup index file header lbn is zero");
	goto not_valid;

	struclev:
	ods5_info("struclev, wrong structure level: %d, version: %d\n",
			home->struclev >> 8, home->struclev & 0xff);
	goto not_valid;

	homevbn:
	ods5_info("%s\n", "homevbn, home block vbn is zero");
	goto not_valid;

	alhomevbn:
	ods5_info("alhomevbn, backup home block vbn is not in third cluster: "
			"%d (expected: %d-%d)\n",
			home->alhomevbn, home->cluster * curclu + 1, home->cluster * (curclu + 1));
	goto not_valid;

	altidxvbn:
	ods5_info("altidxvbn, backup index file header vbn is not in forth cluster: "
			"%d (expected: %d-%d)\n",
			home->altidxvbn, home->cluster * curclu + 1, home->cluster * (curclu + 1));
	goto not_valid;

	ibmapvbn:
	ods5_info("ibmapvbn, index file bitmap vbn is not in fifth cluster: "
			"%d (expected: %d-%d)\n",
			home->ibmapvbn, home->cluster * curclu + 1, home->cluster * (curclu + 1));
	goto not_valid;

	ibmaplbn:
	ods5_info("%s\n", "ibmaplbn, index file bitmap lbn is zero");
	goto not_valid;

	maxfiles:
	ods5_info("maxfiles, maximum number of files is invalid: "
			"maxfiles: %d, resfiles: %d, limit: %d\n",
			home->maxfiles, home->resfiles, 1 << 24);
	goto not_valid;

	ibmapsize:
	ods5_info("%s\n", "ibmapsize, index file bitmap size is zero");
	goto not_valid;

	rvn:
	ods5_info("%s\n", "rvn, relative volume number is not zero");
	goto not_valid;

	volchar:
	ods5_info("volchar, unknown flags 0x%04x\n", home->volchar & ~0x0f);
	goto not_valid;

	checksum1:
	ods5_info("checksum1, invalid value: 0x%04x (calculated: 0x%04x)\n",
			home->checksum1, checksum);
	goto not_valid;

	format:
	ods5_info("format, must be 'DECFILE11B  ': '%.*s'\n",
			(int)sizeof home->format, home->format);
	goto not_valid;

	checksum2:
	ods5_info("checksum2, invalid value: 0x%04x (calculated: 0x%04x)\n",
			home->checksum2, checksum);
	goto not_valid;

	not_valid:
	ods5_info("%s\n", "not a valid home block");
	return 0;
}
