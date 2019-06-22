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

int send_d77(HANDLE fd, const char *filename) {
	FILE *fp = NULL;
	D77_SECTOR_DATA header;
	unsigned char secdat[256];
	int ret = 1, len = 0;
	if ((fp = fopen(filename, "rb")) == NULL)
		return -1;
	fseek(fp, 0x2b0, SEEK_SET);	//ヘッダ部分は読み飛ばす
	while (1) {
		if ((len = fread(&header, 1, sizeof(header), fp)) < 0)
			goto error;
		if (len < sizeof(header))
			break;	//EOF
		if (header.size <= 0)
			continue;
		if (header.size > sizeof(secdat)) {
			printf("It's not standard of disk format. (sector size = %d)\n", header.size);
			goto error;
		}
		if ((len = fread(secdat, 1, header.size, fp)) < header.size)
			goto error;
		//物理セクタ情報を送る
		printf("Track: %d, Sector: %d, Side: %d\n", header.track, header.sector, header.side);
		if (block_write(fd, &header, sizeof(header)) < 0)
			printf("block_write() failed. (%d)\n", errno);
		if (block_write(fd, secdat, header.size) < 0)
			printf("block_write() failed. (%d)\n", errno);
	}
	secdat[0] = 0xff;			// for EOF
	block_write(fd, secdat, 1);
	ret = 0;
error:
	if (fp != NULL)
		fclose(fp);
	return ret;
}

int emul_d77(HANDLE fd, const char *filename1, const char *filename2) {
	RCB_DATA rcb;
	void *image[2];
	unsigned char secdat[256];
	unsigned long filesize[2];
	int ret = 1, len, is_write, index, sector;
	image[0] = image[1] = NULL;
	if ((image[0] = get_file_image(filename1, &filesize[0])) == NULL)
		goto error;
	if (filename2 != NULL)
		if ((image[1] = get_file_image(filename2, &filesize[1])) == NULL)
			goto error;
	printf("%lu bytes read, and connected. (CTRL-C to quit)\n", filesize[0]);
	while (1) {
		if ((len = block_read(fd, &rcb, sizeof(rcb))) < 0) {
			printf("read() failed. (%d)\n", errno);
			goto error;
		}
		if (len < sizeof(rcb))
			continue;
		is_write = rcb.command == 0x09;
		index = rcb.drive;
		sector = logical_sector_RCB(&rcb);
		printf("%c : #%04d = TRACK #%02d, SECTOR #%02d, SIDE %d\n", (is_write ? 'W' : 'R'), sector, rcb.track, rcb.sector, rcb.side);
		if (image[index] == NULL)
			index = 0;
		if (is_write) {
			// write
			//FM-7側では, セクタに書き込むデータ256バイトを送信するので,
			//PC側ではそれを受信する
			if (block_read(fd, secdat, sizeof(secdat)) < 0) {
				printf("block_read() failed. (%d)\n", errno);
				goto error;
			}
			//受信したデータでセクタの内容を更新
			put_sector_mem(image[index], filesize[index], sector, secdat);
		} else {
			// read
			if (get_sector_mem(image[index], filesize[index], sector, secdat) != 0)
				printf("get_sector_mem() failed.\n");
			//セクタの内容を送る(64 × 4 = 256 bytes)
			//FM-7側では, 物理セクタ情報にある通りのバイト数(256)を受信
			for (int i = 0; i < sizeof(secdat); i += FT245RL_BLOCK)
				if (block_write(fd, secdat + i, FT245RL_BLOCK) < 0)
					printf("block_write() failed. (%d)\n", errno);
		}
	}
	ret = 0;
error:
	if (image[0] != NULL)
		free(image[0]);
	if (image[1] != NULL)
		free(image[1]);
	return ret;
}
