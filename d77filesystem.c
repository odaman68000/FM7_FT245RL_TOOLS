//*********************************************************
// ft245tools Version 1.0
// Copyright (C) 2019 by odaman68000. All rights reserved.
//  Linux(/Mac)と, FT245RLを搭載したFM-7系マイコンとを繋ぐ！
//  With FM-7 binaries:
//    - FT245DRV   : FM-7用バイナリデータ送・受信プログラム
//    - FT245TRN   : FM-7用バイナリデータ送・受信プログラム
//    - FT245D77   : FM-7用D77イメージ受信＆書込みプログラム
//    - DRVEMUL    : FM-7用ドライブエミュレートプログラム
//    - BUBEMUL    : FM-7用BUBRコマンド拡張プログラム
//*********************************************************

// オリジナル FM_DATAtransfer2018V2.c から出発

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#ifdef WIN32
#include <Windows.h>
#include <io.h>
#else
#include <sys/param.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#endif

#pragma pack(1)

#include "common.h"

typedef struct {
	unsigned char filename[8];
	char reserved1[3];
	unsigned char file_type;
	char reserved2[2];
	unsigned char start_cluster;
	char reserved3[17];
} DIR_ENTRY;

static int add_to_buffer(void **buffer, unsigned long *buffer_size, const void *target, unsigned int target_size) {
	char *new_buffer = NULL;
	unsigned long new_size = *buffer_size + target_size;
	if (buffer == NULL || buffer_size == NULL || target == NULL)
		return 1;
	if (*buffer == NULL) {
		if ((*buffer = malloc(target_size)) == NULL)
			return -1;
		memcpy(*buffer, target, target_size);
		*buffer_size = target_size;
		return 0;
	}
	if ((new_buffer = malloc(new_size)) == NULL)
		return -1;
	memcpy(new_buffer, *buffer, *buffer_size);
	memcpy(new_buffer + *buffer_size, target, target_size);
	free(*buffer);
	*buffer = new_buffer;
	*buffer_size = new_size;
	return 0;
}

static int is_valid_entry(DIR_ENTRY *entry) {
	return entry != NULL && (entry->filename[0] >= 0x20 && entry->filename[0] < 0x80);
}

static void terminate_filename(DIR_ENTRY *entry) {
	unsigned char *po = entry->filename;
	int ansi = 0;
	for (int i = 0; i < 8; i++, po++) {
		if (*po < 0x20)
			break;
		if (ansi && *po <= 0x20)
			break;
		if (*po > 0x20)
			ansi = 1;
	}
	*po = '\0';
}

static int get_cluster(D77HANDLE *handle, int cluster, unsigned char *buffer) {
	int sector = cluster * 8 + 64;
	unsigned char *dest = buffer;
	for (int i = 0; i < 8; i++, dest += 256, sector++)
		if (get_sector_mem(handle->image_data, handle->image_size, sector, dest) != 0)
			return -1;
	return 0;
}

static int put_cluster(D77HANDLE *handle, int cluster, unsigned char *buffer) {
	int sector = cluster * 8 + 64;
	unsigned char *dest = buffer;
	for (int i = 0; i < 8; i++, dest += 256, sector++)
		if (put_sector_mem(handle->image_data, handle->image_size, sector, dest) != 0)
			return -1;
	return 0;
}

static int get_fat(D77HANDLE *handle, unsigned char *buffer) {
	unsigned char *dest = buffer;
	for (int i = 0; i < 3; i++, dest += 256)
		if (get_sector_mem(handle->image_data, handle->image_size, i + 32, dest) != 0)
			return 1;
	return 0;
}

static int put_fat(D77HANDLE *handle, unsigned char *buffer) {
	unsigned char *dest = buffer;
	for (int i = 0; i < 3; i++, dest += 256)
		if (put_sector_mem(handle->image_data, handle->image_size, i + 32, dest) != 0)
			return 1;
	return 0;
}

static DIR_ENTRY *unused_entry(D77HANDLE *handle, int *sector, unsigned char *buffer) {
	DIR_ENTRY *entry;
	for (int sec = 35; sec < 64; sec++) {
		if (get_sector_mem(handle->image_data, handle->image_size, sec, buffer) != 0)
			return NULL;
		entry = (DIR_ENTRY *)buffer;
		for (int i = 0; i < 256 / sizeof(*entry); i++, entry++)
			if (entry->filename[0] == 0x00 || entry->filename[0] == 0xe5 || entry->filename[0] == 0xff) {
				*sector = sec;
				return entry;
			}
	}
	return NULL;
}

static DIR_ENTRY *get_file_entry(D77HANDLE *handle, const char *filename, int *sector, unsigned char *buffer) {
	DIR_ENTRY *entry;
	for (int sec = 35; sec < 64; sec++) {
		if (get_sector_mem(handle->image_data, handle->image_size, sec, buffer) != 0)
			return NULL;
		entry = (DIR_ENTRY *)buffer;
		for (int i = 0; i < 256 / sizeof(*entry); i++, entry++) {
			if (strncmp(filename, (char *)entry->filename, 8) == 0) {
				*sector = sec;
				return entry;
			}
		}
	}
	return NULL;
}

static void set_cluster_to_fat(unsigned char *fat, int cluster, int value) {
	fat[cluster + 5] = value;
}

static int get_unused_cluster(const unsigned char *fat) {
	for (int cluster = 0; cluster < 152; cluster++)
		if (fat[cluster + 5] == 0xff)
			return cluster;
	return -1;
}

static void set_cluster_as_unused(unsigned char *fat, int cluster) {
	set_cluster_to_fat(fat, cluster, 0xff);
}

static int get_next_cluster(const unsigned char *fat, int cluster, int *used_sector_count) {
	if (cluster < 0 || cluster > 152)
		return -1;
	if ((fat[cluster + 5] & 0xc0) == 0xc0) {
		if (used_sector_count != NULL)
			*used_sector_count = (fat[cluster + 5] & 0x0f) + 1;
		return -1;
	}
	if (used_sector_count != NULL)
		*used_sector_count = 8;
	return fat[cluster + 5];
}

static void *iterate_dir_entry(D77HANDLE *handle, void *user, void * (*func)(D77HANDLE *handle, DIR_ENTRY *entry, int sector, void *parameter)) {
	void *ret = NULL;
	unsigned char buffer[256];
	DIR_ENTRY *entry;
	for (int sec = 35; ret == NULL && sec < 64; sec++) {
		if (get_sector_mem(handle->image_data, handle->image_size, sec, buffer) != 0)
			return NULL;
		entry = (DIR_ENTRY *)buffer;
		for (int i = 0; ret == NULL && i < 256 / sizeof(*entry); i++, entry++)
			ret = func(handle, entry, sec, user);
	}
	return ret;
}

static void *dir_func(D77HANDLE *handle, DIR_ENTRY *entry, int sector, void *parameter) {
	const void **parameters = parameter;
	DIR_ENTRY w_entry = *entry;
	HANDLE fd = (HANDLE)((unsigned long)parameters[0]);
	void (*callback)(D77HANDLE *handle, HANDLE fd, const unsigned char *filename) = (void (*)(D77HANDLE *handle, HANDLE, const unsigned char *))parameters[1];
	if (fd == INVALID_HANDLE_VALUE)
		return NULL;
	if (callback == NULL)
		return NULL;
	if (!is_valid_entry(entry))
		return NULL;
	terminate_filename(&w_entry);
	callback(handle, fd, w_entry.filename);
	return NULL;
}

static void *get_func(D77HANDLE *handle, DIR_ENTRY *entry, int sector, void *parameter) {
	const void **parameters = parameter;
	const char *filename = (const char *)parameters[0];
	unsigned char fat_table[768], data[2048];
	void *object = NULL, *new_object = NULL;
	unsigned long object_size = 0;
	int cluster = 0, used_sector = 8;
	DIR_ENTRY w_entry = *entry;
	if (!is_valid_entry(entry))
		return NULL;
	terminate_filename(&w_entry);
	if (strcmp((char *)w_entry.filename, filename) != 0)
		return NULL;
	if (get_fat(handle, fat_table) != 0)
		return NULL;
	cluster = entry->start_cluster;
	while (cluster >= 0) {
		if (get_cluster(handle, cluster, data) != 0)
			goto error;
		cluster = get_next_cluster(fat_table, cluster, &used_sector);
		if (add_to_buffer(&object, &object_size, data, used_sector * 256) != 0)
			goto error;
	}
	parameters[1] = (void *)object_size;
	printf("object = %p\n", object);
	return object;
error:
	parameters[1] = (void *)0;
	if (object != NULL)
		free(object);
	return NULL;
}

void d77filesystem_dir(D77HANDLE *handle, HANDLE fd, void (*callback)(D77HANDLE *hndl, HANDLE fd, const unsigned char *filename)) {
	void *parameters[] = { (void *)((unsigned long)fd), callback };
	iterate_dir_entry(handle, (void *)parameters, dir_func);
}

void *d77filesystem_get(D77HANDLE *handle, const char *filename, unsigned long *size) {
	void *parameters[] = { (char *)filename, NULL };
	void *object = iterate_dir_entry(handle, (void *)parameters, get_func);
	if (size != NULL)
		*size = (unsigned long)parameters[1];
	return object;
}

void d77filesystem_close(D77HANDLE *handle) {
	if (handle == NULL)
		return;
	if (handle->image_data != NULL)
		free(handle->image_data);
	memset(handle, 0, sizeof(*handle));
	free(handle);
}

D77HANDLE *d77filesystem_open(const char *d77_filename) {
	D77HANDLE *handle = NULL;
	if ((handle = malloc(sizeof(D77HANDLE))) == NULL)
		goto error;
	handle->d77_filename = d77_filename;
	if ((handle->image_data = get_file_image(d77_filename, &handle->image_size)) == NULL)
		goto error;
	return handle;
error:
	d77filesystem_close(handle);
	return (handle = NULL);
}
