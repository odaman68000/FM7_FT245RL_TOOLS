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
#include <arpa/inet.h>

#pragma pack(1)

#include "common.h"

//物理セクタ情報を論理セクタ番号へ変換
static int phy2log(int trck, int sect, int side) {
	return (trck << 5) | ((side ? 1 : 0) << 4) | (sect & 0x0f);
}

static int get_stmode(const char *path) {
	struct stat s;
	if (path == NULL || stat(path, &s) < 0)
		return -1;
	return s.st_mode;
}

void dump(const void *data, int size) {
	for (int offset = 0; offset < size; offset += 16) {
		for (int i = 0; i < 16 && (offset + i) < size; i++)
			printf("%02x ", *((const unsigned char *)data + offset + i));
		printf("\n");
	}
}

//指定のパスがディレクトリかを判定
int is_dir(const char *path) {
	int ret = get_stmode(path);
	return ret >= 0 && S_ISDIR(ret) ? 1 : 0;
}

//指定のパスがファイルかを判定
int is_file(const char *path) {
	int ret = get_stmode(path);
	return ret >= 0 && S_ISREG(ret) ? 1 : 0;
}

int is_file_in_path(const char *file, const char *path) {
	char pathname[PATH_MAX];
	sprintf(pathname, "%s/%s", path, file);
	return is_file(pathname);
}

//HEX様式の文字列をint値に変換
int htoi(const char *v) {
	int ret = 0;
	for (const char *c = v; *c != '\0'; c++) {
		int bc = *c & 0x5f;
		bc = (bc >= 'A' && bc <='F') ? (bc - 'A' + 10) : (bc & 0x0f);
		ret = (ret << 4) + bc;
	}
	return ret;
}

//RCBに格納された物理セクタ情報を論理セクタ番号へ変換
int logical_sector_RCB(const RCB_DATA *rcb) {
	return phy2log(rcb->track, (rcb->sector - 1) & 0x0f, rcb->side);
}

//D77イメージセクタ情報に格納された物理セクタ情報を論理セクタ番号へ変換
int logical_sector_D77(const D77_SECTOR_DATA *d77) {
	return phy2log(d77->track, (d77->sector - 1) & 0x0f, d77->side);
}

//カンマで区切られた文字列を分割する
int split_csv(char *buffer, char **values, int count) {
	int index = 1;
	char *buf, *cp;
	values[0] = buffer;
	for (buf = cp = buffer; index < count && cp != NULL; ) {
		if ((cp = strchr(buf, ',')) != NULL) {
			*cp = '\0';
			values[index] = buf = cp + 1;
			index++;
		}
	}
	return index;
}

//ダブルクオートで囲まれた文字列を取り出す
//NOTE: 指定されたメモリ領域(value)を書き換える事に注意
char *get_quoted_filename(unsigned char *value, int offset) {
	char *s, *filename = NULL;
	if (value[offset] != '\"')
		return NULL;
	filename = (char *)value + offset + 1;
	if ((s = strchr(filename, '\"')) != NULL)
		*s = '\0';
	return filename;
}
