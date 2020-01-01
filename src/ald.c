/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Based on code from xsystem35
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "little_endian.h"
#include "system4/ald.h"

static const char *errtab[ALD_MAX_ERROR] = {
	[ALD_SUCCESS]           = "Success",
	[ALD_FILE_ERROR]        = "Error opening ALD file",
	[ALD_BAD_ARCHIVE_ERROR] = "Invalid ALD file"
};

/* Get a message describing an error. */
const char *ald_strerror(int error)
{
	if (error < ALD_MAX_ERROR)
		return errtab[error];
	return "Invalid error number";
}

/* Get the size of a file in bytes. */
static long get_file_size(FILE *fp)
{
	fseek(fp, 0L, SEEK_END);
	return ftell(fp);
}

/* Check validity of file. */
static bool file_check(FILE *fp)
{
	uint8_t b[6];
	int mapsize, ptrsize;
	long filesize;

	// get filesize / 256
	filesize = (get_file_size(fp) + 255) >> 8;

	long fsize = get_file_size(fp);
	fseek(fp, fsize  & ~0xFF, SEEK_SET);
	fread(b, 1, 4, fp);

	// read top 6 bytes
	fseek(fp, 0L, SEEK_SET);
	fread(b, 1, 6, fp);

	// get ptrsize and mapsize
	ptrsize = LittleEndian_get3B(b, 0);
	mapsize = LittleEndian_get3B(b, 3) - ptrsize;

	// check that sizes are valid
	if (ptrsize < 0 || mapsize < 0)
		return false;
	if (ptrsize > filesize || mapsize > filesize)
		return false;
	return true;
}

/* Read the file map of an ALD file into a `struct ald_archive`. */
static void get_filemap(struct ald_archive *archive, FILE *fp)
{
	uint8_t b[6], *_b;
	int mapsize, ptrsize;

	// read top 6 bytes
	fseek(fp, 0L, SEEK_SET);
	fread(b, 1, 6, fp);

	// get ptrsize and mapsize
	ptrsize = LittleEndian_get3B(b, 0);
	mapsize = LittleEndian_get3B(b, 3) - ptrsize;

	// allocate read buffer
	_b = malloc(mapsize * 256);

	// read filemap
	fseek(fp, ptrsize * 256L, SEEK_SET);
	fread(_b, 256, mapsize, fp);

	// get max file number from mapdata
	archive->maxfile = (mapsize * 256) / 3;

	// map of disk
	archive->map_disk = malloc(archive->maxfile);
	archive->map_ptr = malloc(sizeof(short) * archive->maxfile);

	for (int i = 0; i < archive->maxfile; i++) {
		archive->map_disk[i] = _b[i * 3] - 1;
		archive->map_ptr[i] = LittleEndian_getW(_b, i * 3 + 1) - 1;
	}

	free(_b);
	return;
}

/* Read the pointer map of an ALD file into a `struct ald_archive`. */
static void get_ptrmap(struct ald_archive *archive, FILE *fp, int disk)
{
	uint8_t b[8], *_b;
	int ptrsize, filecnt;

	// read top 6 bytes
	fseek(fp, 0L, SEEK_SET);
	fread(b, 1, 6, fp);

	// get ptrmap size
	ptrsize = LittleEndian_get3B(b, 0);

	// estimate number of entries in ptrmap
	filecnt = (ptrsize * 256) / 3;

	// allocate read buffer
	_b = malloc(ptrsize * 256);

	// read pointers
	fseek(fp, 0L, SEEK_SET);
	fread(_b, 256, ptrsize, fp);

	// allocate pointers buffer
	archive->fileptr[disk] = calloc(filecnt, sizeof(int));

	// store pointers
	for (int i = 0; i < filecnt - 1; i++) {
		*(archive->fileptr[disk] + i) = (LittleEndian_get3B(_b, i * 3 + 3) * 256);
	}

	free(_b);
	return;
}

static int _ald_get(struct ald_archive *ar, int no, int *disk_out, int *dataptr_out)
{
	int disk, ptr, dataptr, dataptr2;

	// check that index is within range of file map
	if (no < 0 || no >= ar->maxfile)
		return 0;

	// look up data in file map
	disk = ar->map_disk[no];
	ptr  = ar->map_ptr[no];
	if (disk < 0 || ptr < 0)
		return 0;

	// no file registered
	if (ar->fileptr[disk] == NULL)
		return 0;

	// get pointer in file and size
	dataptr = *(ar->fileptr[disk] + ptr);
	dataptr2 = *(ar->fileptr[disk] + ptr + 1);
	if (!dataptr || !dataptr2)
		return 0;

	*dataptr_out = dataptr;
	*disk_out = disk;
	return dataptr2 - dataptr;
}

bool ald_data_exists(struct ald_archive *ar, int no)
{
	int disk, dataptr;
	return ar && !!_ald_get(ar, no, &disk, &dataptr);
}

/* Get a piece of data from an ALD archive. */
struct archive_data *ald_get(struct ald_archive *ar, int no)
{
	if (!ar)
		return NULL;

	uint8_t *data;
	struct archive_data *dfile;
	int disk, dataptr, ptr, size;
	int readsize = _ald_get(ar, no, &disk, &dataptr);

	// get data top
	if (ar->mmapped) {
		data = ar->files[disk].data + dataptr;
	} else {
		//int readsize = dataptr2 - dataptr;
		FILE *fp;
		// FIXME: check return values
		data = malloc(sizeof(char) * readsize);
		fp = fopen(ar->files[disk].name, "r");
		fseek(fp, dataptr, SEEK_SET);
		fread(data, 1, readsize, fp);
		fclose(fp);
	}

	// get real data and size
	ptr  = LittleEndian_getDW(data, 0);
	size = LittleEndian_getDW(data, 4);

	dfile = calloc(1, sizeof(struct archive_data));
	dfile->data_raw = data;
	dfile->data = data + ptr;
	dfile->size = size;
	dfile->name = strdup((char*)data + 16);
	dfile->archive = ar;
	return dfile;
}

/* Free an ald_data strcture returned by `ald_get`. */
void ald_free_data(struct archive_data *data)
{
	if (!data)
		return;
	if (!((struct ald_archive*)data->archive)->mmapped)
		free(data->data);
	free(data->name);
	free(data);
}

/* Free an ald_archive structure returned by `ald_open`. */
void ald_free_archive(struct ald_archive *ar)
{
	if (!ar)
		return;
	// unmap mmap files
	if (ar->mmapped) {
		for (int i = 0; i < ALD_FILEMAX; i++) {
			if (ar->files[i].data)
				munmap(ar->files[i].data, ar->files[i].size);
		}
	}
	free(ar);
}

/* Open an ALD archive, optionally memory-mapping it. */
struct ald_archive *ald_open(char **files, int count, int flags, int *error)
{
	FILE *fp;
	long filesize;
	struct ald_archive *ar = calloc(1, sizeof(struct ald_archive));
	bool gotmap = false;

	for (int i = 0; i < count; i++) {
		// XXX: why allow this?
		if (!files[i])
			continue;
		if (!(fp = fopen(files[i], "r"))) {
			*error = ALD_FILE_ERROR;
			goto exit_err;
		}
		// check if it's a valid archive
		if (!file_check(fp)) {
			*error = ALD_BAD_ARCHIVE_ERROR;
			fclose(fp);
			goto exit_err;
		}
		// get file map, if we haven't already
		if (!gotmap) {
			get_filemap(ar, fp);
			gotmap = true;
		}
		// get pointer map
		get_ptrmap(ar, fp, i);
		// get file size for mmap
		filesize = get_file_size(fp);
		// copy filename
		ar->files[i].name = strdup(files[i]);
		// close
		fclose(fp);
		if (flags & ALD_MMAP) {
			int fd;
			if (0 > (fd = open(files[i], O_RDONLY))) {
				*error = ALD_FILE_ERROR;
				goto exit_err;
			}
			ar->files[i].data = mmap(0, filesize, PROT_READ, MAP_SHARED, fd, 0);
			close(fd);
			if (ar->files[i].data == MAP_FAILED) {
				*error = ALD_FILE_ERROR;
				goto exit_err;
			}
			ar->files[i].size = filesize;
		}
	}
	int c = 0;
	for (int i = 0; i < ar->maxfile; i++) {
		if (ar->map_disk[i] < 0 || ar->map_ptr[i] < 0)
			continue;
		c++;
	}
	ar->mmapped = flags & ALD_MMAP;
	ar->nr_files = count;
	return ar;
exit_err:
	free(ar);
	return NULL;
}
