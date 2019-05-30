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
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

#pragma pack(1)

#include "common.h"

//FM-7で扱われる2バイト値を変換
//  $fe02xxxx     : F-BASIC 2バイト整数
//  $2648aabbccdd : "&Habcd" 16進数
static int get_fm7_int(unsigned char *buffer) {
	int flag = 1;
	if (*buffer == '\0')
		return 0;
	if (*(buffer + 0) == 0xfe && *(buffer + 1) == 0x02)
		return *(buffer + 2) * 256 + *(buffer + 3);
	if (*buffer == 0xda) {	//'-'
		flag = -1;
		++buffer;
	}
	if (*(buffer + 0) == '&' && (*(buffer + 1) & 0xdf) == 'H')
		return flag * htoi((char *)buffer + 2);
	return -6809;
}

static int get_basic_header(unsigned char *data, unsigned long size, BAS_HEADER **header, unsigned char **body, unsigned long *body_size, BAS_FOOTER **footer) {
	if (size < (sizeof(BAS_HEADER) + sizeof(BAS_FOOTER)))
		return -1;
	*header = (BAS_HEADER *)data;
	if ((*header)->type1 != 0xff || (*header)->type2 != 0xff || (*header)->type3 != 0xff)
		return 1;
	*footer = (BAS_FOOTER *)(data + size - sizeof(BAS_FOOTER));
	if ((*footer)->type != 0x00 || (*footer)->eof != 0x1a)
		return 1;
	*body = (data + sizeof(BAS_HEADER));
	*body_size = size - (sizeof(BAS_HEADER) + sizeof(BAS_FOOTER));
	return 0;
}

static int get_binary_header(unsigned char *data, unsigned long size, BIN_HEADER **header, unsigned char **body, BIN_FOOTER **footer) {
	if (size < (sizeof(BIN_HEADER) + sizeof(BIN_FOOTER)))
		return -1;
	*header = (BIN_HEADER *)data;
	if ((*header)->type != 0x00)
		return 1;
	(*header)->start = ntohs((*header)->start);
	(*header)->length = ntohs((*header)->length);
	if (size < (sizeof(BIN_HEADER) + sizeof(BIN_FOOTER) + (*header)->length))
		return -1;
	*footer = (BIN_FOOTER *)(data + sizeof(BIN_HEADER) + (*header)->length);
	if ((*footer)->type != 0xff || (*footer)->reserved != 0x0000 || (*footer)->eof != 0x1a)
		return 1;
	(*footer)->exec = ntohs((*footer)->exec);
	*body = (data + sizeof(BIN_HEADER));
	return 0;
}

//F-BASIC ファイルを受信して保存する
//BUBR SAVE "filename"
//BUBR SAVE "filename",A
static int emul_bub_save(int fd, const char *pathname) {
	int len;
	struct {
		BAS_HEADER header;
		unsigned char body[MAX16BIT];
	} data;
	BAS_FOOTER *footer;
	block_write_byte(fd, CMD_SAVE);
	len = recv_mem(fd, data.body);
	footer = (BAS_FOOTER *)(data.body + len);
	data.header.type1 = data.header.type2 = data.header.type3 = 0xff;
	footer->type = 0x00;
	footer->eof = 0x1a;
	return put_file_image(pathname, (unsigned char *)&data, sizeof(data.header) + len + sizeof(*footer));
}

//F-BASIC ファイルをファイルから読み込んで FM-7 に送信
//BUBR LOAD "filename"
static int emul_bub_load(int fd, const char *pathname) {
	int ret = -1;
	unsigned long size, body_size;
	BAS_HEADER *header;
	BAS_FOOTER *footer;
	unsigned char *buffer = NULL, *body = NULL;
	if ((buffer = get_file_image(pathname, &size)) == NULL)
		goto error;
	if (get_basic_header(buffer, size, &header, &body, &body_size, &footer) == 0) {
		//BASIC data in binary
		block_write_byte(fd, CMD_LOAD);
		ret = send_mem(fd, body, body_size, 0, 0);
	} else {
		//BASIC data in ASCII (0x1a terminated)
		BIN_HEADER *bin_header;
		BIN_FOOTER *bin_footer;
		unsigned char *bin_body = NULL;
		if (get_binary_header(buffer, size, &bin_header, &bin_body, &bin_footer) == 0) {
			block_write_string_result(fd, "Illegal file mode.");
			goto error;
		}
		*(buffer + size + 1) = '\0';
		if ((body = (unsigned char *)strchr((char *)buffer, 0x1a)) != NULL) {
#ifdef DEBUG
			printf("emul_bub_load(): EOF character was cleared at %d.\n", (int)(body - buffer));
#endif
			*body = '\0';
		}
		block_write_byte(fd, CMD_LOADA);
		ret = block_write_string(fd, (char *)buffer);
	}
error:
	if (buffer != NULL)
		free(buffer);
	return ret;
}

//バイナリを受信して保存する
//BUBR SAVEM "filename",Start,End,Exec
static int emul_bub_savem(int fd, const char *pathname, int st, int ed, int ex) {
	int len;
	FILEINFO info;
	struct {
		BIN_HEADER header;
		unsigned char body[MAX16BIT];
	} data;
	BIN_FOOTER *footer;
	info.start = htons(st);
	info.bottom = htons(ed + 1);
	info.exec = 0;
#ifdef DEBUG
	dump(&info, sizeof(info));
#endif
	block_write_byte(fd, CMD_SAVEM);
	block_write(fd, &info, sizeof(info));
	len = recv_mem(fd, data.body);
	footer = (BIN_FOOTER *)(data.body + len);
	data.header.type = 0x00;
	data.header.length = htons(ed - st + 1);
	data.header.start = htons(st);
	footer->type = 0xff;
	footer->reserved = 0;
	footer->exec = ex < 0 ? data.header.start : htons(ex);
	footer->eof = 0x1a;
	return put_file_image(pathname, (unsigned char *)&data, sizeof(data.header) + len + sizeof(*footer));
}

//バイナリファイルを読み込んで FM-7 に送信
//BUBR LOADM "filename"
//BUBR LOADM "filename",&Hoffset
//BUBR LOADM "filename",&Hoffset,R
static int emul_bub_loadm(int fd, const char *pathname, int offset, int exec) {
	int ret = -1;
	unsigned long size;
	BIN_HEADER *header;
	BIN_FOOTER *footer;
	unsigned char *buffer = NULL, *body = NULL;
	struct binary_image {
		BIN_HEADER header;
		unsigned char body[MAX16BIT];
	} *data;
	if ((buffer = get_file_image(pathname, &size)) == NULL)
		goto error;
	if (get_binary_header(buffer, size, &header, &body, &footer) != 0) {
		block_write_string_result(fd, "Illegal file mode.");
		goto error;
	}
	block_write_byte(fd, CMD_LOADM);
	ret = send_mem(fd, body, header->length, header->start + offset, exec == 0 ? 0 : footer->exec + offset);
error:
	if (buffer != NULL)
		free(buffer);
	return ret;
}

//バイナリファイルを読み込んで FM-7 に送信
//バイナリファイルに SAVEM/LOADM 様式のヘッダ類が無くても送信する
//BUBR LOADR "filename",&Hstart
//BUBR LOADR "filename",&Hstart,N
//BUBR LOADR "filename",&Hstart,&Hexec
//BUBR LOADR "filename",&Hstart,&Hexec,N
static int emul_bub_loadr(int fd, const char *pathname, int st, int ex, int without_header) {
	int ret = -1;
	unsigned long size;
	BIN_HEADER *header;
	BIN_FOOTER *footer;
	unsigned char *buffer = NULL, *body = NULL;
	if ((buffer = get_file_image(pathname, &size)) == NULL)
		goto error;
	body = buffer;
	if (without_header && get_binary_header(buffer, size, &header, &body, &footer) == 0)
		size = header->length;
	block_write_byte(fd, CMD_LOADM);
	ret = send_mem(fd, body, size, st, ex);
error:
	if (buffer != NULL)
		free(buffer);
	return ret;
}

int emul_bub(int fd, const char *dirname) {
	char string_buffer[256];
	char *b, buffer[1024], *filename, pathname[PATH_MAX];
	unsigned char *values[64];
	int vcnt, binary;
	while (serial_read_string(fd, string_buffer, sizeof(string_buffer)) >= 0) {
		if (*string_buffer == '\0')
			goto error_loop;
#ifdef DEBUG
		for (int i = 0; i < strlen(string_buffer); i++)
			printf("%02x ", (unsigned char)*(string_buffer + i));
		printf("\n");
#endif
		binary = 0;
		vcnt = split_csv(string_buffer, (char **)values, ARRAY_COUNT(values));
#ifdef DEBUG
		for (int i = 0; i < vcnt; i++) {
			for (int j = 0; j < strlen((char *)values[i]); j++)
				printf("%02x ", *(values[i] + j));
			printf("\n");
		}
#endif
		if (*(values[0] + 0) == 0xaa) {			//SAVE
			int p1 = 0, p2 = 0, p3 = -1;
			if ((*(values[0] + 1) & 0xdf) == 'M')		//SAVEM
				binary = 1;
			filename = get_quoted_filename(values[0], binary + 1);
			if (filename == NULL || (binary && vcnt < 3)) {
				printf("SAVE%s : Parameter error.\n", binary ? "M" : "");
				goto error_loop;
			}
			sprintf(pathname, "%s/%s", dirname, filename);
			if (!binary) {
				//BUBR SAVE "filename"
				//BUBR SAVE "filename",A
				if (vcnt > 1)
					p1 = ((*values[1] & 0xdf) == 'A');
				printf("SAVE \"%s\"%s\n", filename, p1 ? ",A" : "");
				emul_bub_save(fd, pathname);
			} else {
				//BUBR SAVEM "filename",Start,End,Exec
				p1 = get_fm7_int(values[1]);
				p2 = get_fm7_int(values[2]);
				p3 = get_fm7_int(values[3]);
				if ((p1 < 0 || p1 > 0xffff) || (p2 < 0 || p2 > 0xffff) || (p3 < 0 || p3 > 0xffff)) {
					block_write_string_result(fd, "Address range is invalid.");
					continue;
				}
				printf("SAVEM \"%s\",&H%04X,&H%04X,&H%04X\n", filename, p1, p2, p3);
				emul_bub_savem(fd, pathname, p1, p2, p3);
			}
			continue;
		} else if (*(values[0] + 0) == 0xab) {	//LOAD
			unsigned int p1 = 0, p2 = 0;
			if ((*(values[0] + 1) & 0xdf) == 'M')		//LOADM
				binary = 1;
			else if ((*(values[0] + 1) & 0xdf) == 'R')	//LOADR
				binary = 2;
			filename = get_quoted_filename(values[0], (binary ? 1 : 0) + 1);
			if (filename == NULL || (binary == 2 && vcnt < 2)) {
				printf("LOAD%s : Parameter error.\n", binary ? "M" : "");
				goto error_loop;
			}
			sprintf(pathname, "%s/%s", dirname, filename);
			if (!is_file(pathname)) {
				printf("LOAD%s \"%s\" : File not found.\n", binary ? "M" : "", filename);
				block_write_string_result(fd, "File not found.");
				continue;
			}
			if (!binary) {
				//BUBR LOAD "filename"
				printf("LOAD \"%s\"\n", filename);
				emul_bub_load(fd, pathname);
			} else {
				if (vcnt > 1)
					p1 = get_fm7_int(values[1]);
				if (binary == 1) {
					//BUBR LOADM "filename"
					//BUBR LOADM "filename",,R
					//BUBR LOADM "filename",&Hoffset
					//BUBR LOADM "filename",&Hoffset,R
					if (vcnt > 2)
						p2 = ((*values[2] & 0xdf) == 'R');
					if (p1 > 0xffff || p1 < -0xffff) {
						block_write_string_result(fd, "Address range is invalid.");
						continue;
					}
					printf("LOADM \"%s\",&H%04X%s\n", filename, p1, p2 ? ",R" : "");
					emul_bub_loadm(fd, pathname, p1, p2);
				} else {
					//BUBR LOADR "filename",&Hstart
					//BUBR LOADM "filename",&Hstart,,N
					//BUBR LOADR "filename",&Hstart,&Hexec
					//BUBR LOADM "filename",&Hstart,&Hexec,N
					if (vcnt < 2)
						goto error_loop;
					unsigned int p3 = 0;
					if (vcnt > 2)
						p2 = get_fm7_int(values[2]);
					if (vcnt > 3)
						p3 = (*values[3] & 0xdf) == 'N';
					if ((p1 < 0 || p1 > 0xffff) || (p2 < 0 || p2 > 0xffff)) {
						block_write_string_result(fd, "Address range is invalid.");
						continue;
					}
					printf("LOADR \"%s\",$%04X,$%04X%s\n", filename, p1, p2, p3 ? ",N" : "");
					emul_bub_loadr(fd, pathname, p1, p2, p3);
				}
			}
			continue;
		} else if (*(values[0] + 0) == 0xb0) {	//FILES
			DIR *dir = NULL;
			struct dirent *e = NULL;
			printf("FILES\n");
			if ((dir = opendir(dirname)) == NULL) {
				block_write_string_result(fd, "Directory access error.");
				continue;
			}
			block_write_byte(fd, CMD_PRINT);
			for (e = readdir(dir); e != NULL; e = readdir(dir))
				if (is_file_in_path(e->d_name, dirname)) {
					sprintf(buffer, "%s%c%c", e->d_name, 0x0d, 0x0a);
					block_write(fd, buffer, strlen(buffer));
				}
			closedir(dir);
			block_write_byte(fd, 0x1a);
			continue;
		}
		printf("Command not found.\n");
error_loop:
		block_write_byte(fd, CMD_ERROR);
	}
	printf("Connection refused.\n");
	return 0;
}
