/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief IAX Firmware Support
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include "asterisk/linkedlists.h"
#include "asterisk/md5.h"
#include "asterisk/paths.h"
#include "asterisk/utils.h"

#include "include/firmware.h"

struct iax_firmware {
	AST_LIST_ENTRY(iax_firmware) list;
	int fd;
	int mmaplen;
	int dead;
	struct ast_iax2_firmware_header *fwh;
	unsigned char *buf;
};

static AST_LIST_HEAD_STATIC(firmwares, iax_firmware);

static int try_firmware(char *s)
{
	struct stat stbuf;
	struct iax_firmware *cur = NULL;
	int ifd, fd, res, len, chunk;
	struct ast_iax2_firmware_header *fwh, fwh2;
	struct MD5Context md5;
	unsigned char sum[16], buf[1024];
	char *s2, *last;

	s2 = ast_alloca(strlen(s) + 100);

	last = strrchr(s, '/');
	if (last)
		last++;
	else
		last = s;

	snprintf(s2, strlen(s) + 100, "/var/tmp/%s-%ld", last, ast_random());

	if (stat(s, &stbuf) < 0) {
		ast_log(LOG_WARNING, "Failed to stat '%s': %s\n", s, strerror(errno));
		return -1;
	}

	/* Make sure it's not a directory */
	if (S_ISDIR(stbuf.st_mode))
		return -1;
	ifd = open(s, O_RDONLY);
	if (ifd < 0) {
		ast_log(LOG_WARNING, "Cannot open '%s': %s\n", s, strerror(errno));
		return -1;
	}
	fd = open(s2, O_RDWR | O_CREAT | O_EXCL, AST_FILE_MODE);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Cannot open '%s' for writing: %s\n", s2, strerror(errno));
		close(ifd);
		return -1;
	}
	/* Unlink our newly created file */
	unlink(s2);

	/* Now copy the firmware into it */
	len = stbuf.st_size;
	while(len) {
		chunk = len;
		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		res = read(ifd, buf, chunk);
		if (res != chunk) {
			ast_log(LOG_WARNING, "Only read %d of %d bytes of data :(: %s\n", res, chunk, strerror(errno));
			close(ifd);
			close(fd);
			return -1;
		}
		res = write(fd, buf, chunk);
		if (res != chunk) {
			ast_log(LOG_WARNING, "Only write %d of %d bytes of data :(: %s\n", res, chunk, strerror(errno));
			close(ifd);
			close(fd);
			return -1;
		}
		len -= chunk;
	}
	close(ifd);
	/* Return to the beginning */
	lseek(fd, 0, SEEK_SET);
	if ((res = read(fd, &fwh2, sizeof(fwh2))) != sizeof(fwh2)) {
		ast_log(LOG_WARNING, "Unable to read firmware header in '%s'\n", s);
		close(fd);
		return -1;
	}
	if (ntohl(fwh2.magic) != IAX_FIRMWARE_MAGIC) {
		ast_log(LOG_WARNING, "'%s' is not a valid firmware file\n", s);
		close(fd);
		return -1;
	}
	if (ntohl(fwh2.datalen) != (stbuf.st_size - sizeof(fwh2))) {
		ast_log(LOG_WARNING, "Invalid data length in firmware '%s'\n", s);
		close(fd);
		return -1;
	}
	if (fwh2.devname[sizeof(fwh2.devname) - 1] || ast_strlen_zero((char *)fwh2.devname)) {
		ast_log(LOG_WARNING, "No or invalid device type specified for '%s'\n", s);
		close(fd);
		return -1;
	}
	fwh = (struct ast_iax2_firmware_header*)mmap(NULL, stbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (fwh == MAP_FAILED) {
		ast_log(LOG_WARNING, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	MD5Init(&md5);
	MD5Update(&md5, fwh->data, ntohl(fwh->datalen));
	MD5Final(sum, &md5);
	if (memcmp(sum, fwh->chksum, sizeof(sum))) {
		ast_log(LOG_WARNING, "Firmware file '%s' fails checksum\n", s);
		munmap((void*)fwh, stbuf.st_size);
		close(fd);
		return -1;
	}

	AST_LIST_TRAVERSE(&firmwares, cur, list) {
		if (!strcmp((const char *) cur->fwh->devname, (const char *) fwh->devname)) {
			/* Found a candidate */
			if (cur->dead || (ntohs(cur->fwh->version) < ntohs(fwh->version)))
				/* The version we have on loaded is older, load this one instead */
				break;
			/* This version is no newer than what we have.  Don't worry about it.
			   We'll consider it a proper load anyhow though */
			munmap((void*)fwh, stbuf.st_size);
			close(fd);
			return 0;
		}
	}

	if (!cur && ((cur = ast_calloc(1, sizeof(*cur))))) {
		cur->fd = -1;
		AST_LIST_INSERT_TAIL(&firmwares, cur, list);
	}

	if (cur) {
		if (cur->fwh)
			munmap((void*)cur->fwh, cur->mmaplen);
		if (cur->fd > -1)
			close(cur->fd);
		cur->fwh = fwh;
		cur->fd = fd;
		cur->mmaplen = stbuf.st_size;
		cur->dead = 0;
	}

	return 0;
}

static void destroy_firmware(struct iax_firmware *cur)
{
	/* Close firmware */
	if (cur->fwh) {
		munmap((void*)cur->fwh, ntohl(cur->fwh->datalen) + sizeof(*(cur->fwh)));
	}
	close(cur->fd);
	ast_free(cur);
}

void iax_firmware_reload(void)
{
	struct iax_firmware *cur = NULL;
	DIR *fwd;
	struct dirent *de;
	char dir[256], fn[256];

	AST_LIST_LOCK(&firmwares);

	/* Mark all as dead */
	AST_LIST_TRAVERSE(&firmwares, cur, list) {
		cur->dead = 1;
	}

	/* Now that we have marked them dead... load new ones */
	snprintf(dir, sizeof(dir), "%s/firmware/iax", ast_config_AST_DATA_DIR);
	fwd = opendir(dir);
	if (fwd) {
		while((de = readdir(fwd))) {
			if (de->d_name[0] != '.') {
				snprintf(fn, sizeof(fn), "%s/%s", dir, de->d_name);
				if (!try_firmware(fn)) {
					ast_verb(2, "Loaded firmware '%s'\n", de->d_name);
				}
			}
		}
		closedir(fwd);
	} else {
		ast_log(LOG_WARNING, "Error opening firmware directory '%s': %s\n", dir, strerror(errno));
	}

	/* Clean up leftovers */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&firmwares, cur, list) {
		if (!cur->dead)
			continue;
		AST_LIST_REMOVE_CURRENT(list);
		destroy_firmware(cur);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_UNLOCK(&firmwares);
}

void iax_firmware_unload(void)
{
	struct iax_firmware *cur = NULL;

	AST_LIST_LOCK(&firmwares);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&firmwares, cur, list) {
		AST_LIST_REMOVE_CURRENT(list);
		destroy_firmware(cur);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&firmwares);
}

int iax_firmware_get_version(const char *dev, uint16_t *version)
{
	struct iax_firmware *cur = NULL;

	if (ast_strlen_zero(dev))
		return 0;

	AST_LIST_LOCK(&firmwares);
	AST_LIST_TRAVERSE(&firmwares, cur, list) {
		if (!strcmp(dev, (const char *) cur->fwh->devname)) {
			*version = ntohs(cur->fwh->version);
			AST_LIST_UNLOCK(&firmwares);
			return 1;
		}
	}
	AST_LIST_UNLOCK(&firmwares);

	return 0;
}

int iax_firmware_append(struct iax_ie_data *ied, const char *dev, unsigned int desc)
{
	int res = -1;
	unsigned int bs = desc & 0xff;
	unsigned int start = (desc >> 8) & 0xffffff;
	unsigned int bytes;
	struct iax_firmware *cur;

	if (ast_strlen_zero((char *)dev) || !bs)
		return -1;

	start *= bs;

	AST_LIST_LOCK(&firmwares);
	AST_LIST_TRAVERSE(&firmwares, cur, list) {
		if (strcmp(dev, (const char *) cur->fwh->devname))
			continue;
		iax_ie_append_int(ied, IAX_IE_FWBLOCKDESC, desc);
		if (start < ntohl(cur->fwh->datalen)) {
			bytes = ntohl(cur->fwh->datalen) - start;
			if (bytes > bs)
				bytes = bs;
			iax_ie_append_raw(ied, IAX_IE_FWBLOCKDATA, cur->fwh->data + start, bytes);
		} else {
			bytes = 0;
			iax_ie_append(ied, IAX_IE_FWBLOCKDATA);
		}
		if (bytes == bs)
			res = 0;
		else
			res = 1;
		break;
	}
	AST_LIST_UNLOCK(&firmwares);

	return res;
}

void iax_firmware_traverse(
	const char *filter,
	int (*callback)(struct ast_iax2_firmware_header *header, void *data),
	void *data)
{
	struct iax_firmware *cur = NULL;

	if (!callback) {
		return;
	}

	AST_LIST_LOCK(&firmwares);
	AST_LIST_TRAVERSE(&firmwares, cur, list) {
		if (!filter || !strcasecmp(filter, (const char *) cur->fwh->devname)) {
			if (callback(cur->fwh, data)) {
				break;
			}
		}
	}
	AST_LIST_UNLOCK(&firmwares);
}
