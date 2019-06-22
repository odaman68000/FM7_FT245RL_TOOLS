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

#ifdef WIN32
int read_win32(HANDLE fd, void *buffer, int size) {
	int length = 0;
	if (!ReadFile(fd, buffer, size, &length, NULL))
		return -1;
	return length;
}

int write_win32(HANDLE fd, void *buffer, int size) {
	int length = 0;
	if (!WriteFile(fd, buffer, size, &length, NULL))
		return -1;
	return length;
}
#endif

//シリアルポートからの read() ユーティリティ
static int serial_read(HANDLE fd, void *buffer, int size) {
	int len = -1, total;
	for (total = 0; total < size; total += len)
		for (len = -1; len < 0; ) {
			if ((len = read(fd, (char *)buffer + total, size - total)) < 0) {
				if (errno == EAGAIN) {
					usleep(500);
					continue;
				}
				return len;
			}
#if DEBUG
			//printf("serial_read(): read() %d\n", len);
#endif
		}
	return total;
}

//シリアルポートへの write() ユーティリティ
static int serial_write(HANDLE fd, unsigned char *buffer, int size) {
	int len = -1, total;
	for (total = 0; total < size; total += len)
		for (len = -1; len < 0; )
			if ((len = write(fd, (char *)buffer + total, size - total)) < 0) {
				if (errno == EAGAIN) {
					usleep(500);
					continue;
				}
				return len;
			}
	return total;
}

//FT245RLに対してデータを送信する
static int send_block(HANDLE fd, const unsigned char *block, int len) {
	unsigned char send_buf[FT245RL_BLOCK * 2];
	for (int i = 0; i < len; i++) {
		send_buf[i * 2 + 1] = block[i];			// 下位7ビットはそのままコピー
		send_buf[i * 2 + 0] = block[i] >> 1;	// 上位7ビットは1ビット右シフトしコピー
	}
	return serial_write(fd, send_buf, len * 2);
}

//シリアルポートからの null-terminated 文字列受信ユーティリティ
int serial_read_string(HANDLE fd, char *buffer, int length) {
	int d, l = 0, eos = 0;
	char c[1], *po = NULL;
#ifdef DEBUG
	printf("serial_read_string(): ");
#endif
	memset(buffer, 0, length);
	for (c[0] = 0x0d, po = buffer, l = 0; c[0] != '\0'; ) {
		if (serial_read(fd, c, 1) < 0)
			return -1;
		d = c[0];
		if (c[0] == ':')
			d = '\0';
		if (!eos && l < (length - 1)) {
#ifdef DEBUG
			printf("%02x ", (unsigned char)d);
#endif
			*(po++) = d;
			eos = (d == '\0');
		}
		l++;
	}
#ifdef DEBUG
	printf("%d bytes received. (string: %lu)\n", l, strlen(buffer));
#endif
	return strlen(buffer);
}

//シリアルポートへの read() ユーティリティ
int block_read(HANDLE fd, void *data, int len) {
	int received = 0, bs = 0;
	for (received = 0; received < len; received += bs)
		if ((bs = serial_read(fd, (unsigned char *)data + received, MIN(FT245RL_BLOCK, len - received))) < 0)
			return -1;
#ifdef DEBUG
	printf("block_read(): %d bytes received. (len = %d)\n", received, len);
#endif
	return received;
}

//シリアルポートへの write() ユーティリティ
int block_write(HANDLE fd, const void *data, int len) {
	int sent = 0, bs = 0;
	for (sent = 0; sent < len; sent += bs)
		if (send_block(fd, (unsigned char *)data + sent, (bs = MIN(FT245RL_BLOCK, len - sent))) < 0)
			return -1;
#ifdef DEBUG
	printf("block_write(): %d bytes sent. (len = %d)\n", sent, len);
#endif
	return len;
}

//シリアルポートへの write() ユーティリティ
int block_write_byte(HANDLE fd, int d) {
	unsigned char c[1];
	c[0] = d;
	return send_block(fd, c, 1);
}

//シリアルポートへの null-terminated 文字列送信ユーティリティ
int block_write_string(HANDLE fd, const char *string) {
	static char *buffer = NULL;
	static int size = -1;
	if (buffer == NULL || size < (int)(strlen(string) + 2)) {
		if (buffer != NULL)
			free(buffer);
		size = strlen(string) + 2;
		if ((buffer = malloc(size)) == NULL)
			return -1;
	}
	sprintf(buffer, "%s%c", string, 0x1a);	//0x1a = EOS
#ifdef DEBUG
	printf("block_write_string(): %lu.\n", strlen(buffer));
#endif
	return block_write(fd, buffer, strlen(buffer));
}

int block_write_string_result(HANDLE fd, const char *message) {
	block_write_byte(fd, CMD_PRINT);
	return block_write_string(fd, message);
}

//論理セクタを指定して256バイト読み込む
int get_sector_file(FILE *fp, int sector, unsigned char *data) {
	D77_SECTOR_DATA header;
	int len = 0;
	fseek(fp, 0x2b0, SEEK_SET);	//ヘッダ部分は読み飛ばす
	while (1) {
		if ((len = fread(&header, 1, sizeof(header), fp)) < 0)
			return -1;
		if (len < sizeof(header))
			return 1;
		if (header.size <= 0)
			continue;
		if (fread(data, 1, header.size, fp) < header.size)
			return -1;
		if (logical_sector_D77(&header) == sector)
			break;
	}
	return 0;
}

//論理セクタを指定して256バイト読み込む
int get_sector_mem(const unsigned char *image, unsigned long len, int sector, unsigned char *data) {
	D77_SECTOR_DATA *header;
	for (const unsigned char *po = image + 0x2b0; po < (image + len); ) {
		header = (D77_SECTOR_DATA *)po;
		po += sizeof(*header);
		if (header->size <= 0)
			continue;
		if (logical_sector_D77(header) == sector) {
			memcpy(data, po, (header->size > 256 ? 256 : header->size));
			return 0;
		}
		po += header->size;
	}
	return -1;
}

//論理セクタを指定して256バイト書き込む
int put_sector_mem(unsigned char *image, unsigned long len, int sector, unsigned char *data) {
	D77_SECTOR_DATA *header;
	for (unsigned char *po = image + 0x2b0; po < (image + len); ) {
		header = (D77_SECTOR_DATA *)po;
		po += sizeof(*header);
		if (header->size <= 0)
			continue;
		if (logical_sector_D77(header) == sector) {
			memcpy(po, data, (header->size > 256 ? 256 : header->size));
			return 0;
		}
		po += header->size;
	}
	return -1;
}

//ファイルの内容を全てメモリに読み込む
void *get_file_image(const char *filename, unsigned long *filesize) {
	FILE *fp = NULL;
	char *image = NULL;
	unsigned long size;
	if ((fp = fopen(filename, "rb")) == NULL)
		goto error;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if ((image = (char *)malloc(size + 7)) == NULL)
		goto error;
	fread(image, 1, size, fp);
	fclose(fp);
	if (filesize != NULL)
		*filesize = size;
	return image;
error:
	if (image != NULL)
		free(image);
	if (fp != NULL)
		fclose(fp);
	return NULL;
}

//メモリの内容をファイルに書き込む
int put_file_image(const char *filename, const unsigned char *data, unsigned long length) {
	int ret;
	FILE *fp = NULL;
	if ((fp = fopen(filename, "wb")) == NULL)
		return -1;
	ret = fwrite(data, 1, length, fp);
	fclose(fp);
	return ret;
}

int send_mem(HANDLE fd, const void *buffer, int length, unsigned int st, unsigned int ex) {
	int sent;
	if (st >= 0) {
		FILEINFO info;
		info.start = htons(st);
		info.bottom = htons(st + length);
		info.exec = htons(ex);
#ifdef DEBUG
		dump(&info, sizeof(info));
		printf("send_mem(): start %04x bottom %04x, len %d\n", st, st + length, length);
#endif
		block_write(fd, &info, sizeof(info));
	}
	if ((sent = block_write(fd, buffer, length)) < 0)
		printf("block_write() failed. (%d)\n", errno);
	return sent;
}

int send_file(HANDLE fd, const char *filename, unsigned int start, unsigned int exec) {
	FILE *fp = NULL;
	char block[4096];
	int ret = 1, len = 0;
	unsigned int sent = 0;
	if ((fp = fopen(filename, "rb")) == NULL)
		return -1;
	if (start >= 0) {
		FILEINFO info;
		fseek(fp, 0, SEEK_END);
		info.start = htons(start);
		info.bottom = htons((u_short)(start + ftell(fp)));
		info.exec = htons(exec);
		fseek(fp, 0, SEEK_SET);
#ifdef DEBUG
		printf("send_file(): start %04x bottom %04x, len %d\n", start, (unsigned short)ntohs(info.bottom), (unsigned short)ntohs(info.bottom) - start);
#endif
		block_write(fd, &info, sizeof(info));
	}
	for (sent = 0, len = 1; len > 0; sent += len) {
		if ((len = fread(block, 1, sizeof(block), fp)) < 0)
			goto error;
		if (len > 0)
			if (block_write(fd, block, len) < 0)
				printf("block_write() failed. (%d)\n", errno);
	}
	ret = sent;
error:
	if (fp != NULL)
		fclose(fp);
	return ret;
}

static int recv_mem_chunked(HANDLE fd, void *buffer, unsigned int st, unsigned int ed) {
	FILEINFO info, info_n, info_r;
	const unsigned int length = ed - st + 1;
	unsigned int sent = 0, bs = 0;
	info.bottom = st;
	for (sent = 0; sent < length; sent += bs) {
		info.start = info.bottom;
		info.bottom = info.start + (bs = MIN(FT245RL_BLOCK, length - sent));
		info_n.start = htons(info.start);
		info_n.bottom = htons(info.bottom);
		info_n.exec = htons(0);
#ifdef DEBUG
		printf("recv_mem_chunked(): %d %d %d\n", sent, bs, length);
#endif
		block_write_byte(fd, CMD_SAVEM);
		block_write(fd, &info_n, sizeof(info_n));
		block_read(fd, &info_r, sizeof(info_r));
		block_read(fd, (char *)buffer + sent, bs);
	}
	block_write_byte(fd, CMD_END);
#ifdef DEBUG
	printf("recv_mem_chunked(): %d\n", sent);
#endif
	return sent;
}

int recv_mem(HANDLE fd, void *buffer) {
	FILEINFO info;
	int len = 0, size;
	block_read(fd, &info, sizeof(info));
#ifdef DEBUG
	dump(&info, sizeof(info));
#endif
	info.start = ntohs(info.start);
	info.bottom = ntohs(info.bottom);
	size = info.bottom + (info.bottom < info.start ? 0x10000 : 0) - info.start;
	if ((len = block_read(fd, buffer, size)) < 0)
		return -1;
#ifdef DEBUG
	printf("recv_mem(): start %04x bottom %04x, len %d, received %d.\n", (unsigned short)info.start, (unsigned short)info.bottom, (unsigned int)size, len);
#endif
	return size;
}

int recv_file(HANDLE fd, const char *filename) {
	unsigned char buffer[MAX16BIT];
	int len;
	if ((len = recv_mem(fd, buffer)) < 0)
		return len;
	return put_file_image(filename, buffer, len);
}

//シリアルデバイスのオープン
HANDLE open_serial_device(int baudRate) {
	HANDLE fd;
#ifdef WIN32
	DCB dcb;
	if ((fd = CreateFile(SERIAL_PORT, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
		goto error;
	if (!GetCommState(fd, &dcb))
		goto error;
	dcb.BaudRate = baudRate;
	dcb.ByteSize = 8;
	dcb.StopBits = 0;	//0,1,2 = 1,1.5,2
	dcb.Parity = 0;		//0,1,2,3,4 = None, Even, Odd, Skip
	dcb.fParity = FALSE;
	dcb.fBinary = TRUE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = FALSE;
	dcb.fRtsControl = FALSE;
	dcb.fInX = FALSE;
	dcb.fOutX = FALSE;
	dcb.fTXContinueOnXoff = FALSE;
	if (SetCommState(fd, &dcb))
		return fd;
error:
	if (fd != INVALID_HANDLE_VALUE)
		CloseHandle(fd);
	return INVALID_HANDLE_VALUE;
#else
	struct termios tio;					// シリアル通信設定
	if ((fd = open(SERIAL_PORT, O_RDWR|O_NOCTTY)) < 0)	// デバイスをオープンする
		return INVALID_HANDLE_VALUE;
	tcgetattr( fd, &tio );
	cfmakeraw( &tio );					// RAWモード
	cfsetspeed( &tio, baudRate );
	tio.c_cflag  = CREAD;				// 受信有効
	tio.c_cflag += CLOCAL;				// ローカルライン（モデム制御なし）
	tio.c_cflag += CS8;					// データビット:8bit
	tio.c_cflag += 0;					// ストップビット:1bit
	tio.c_cflag += 0;					// パリティ:None
	tio.c_iflag = 0;
	tio.c_lflag = 0;
	tio.c_oflag = 0;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
	tcsetattr( fd, TCSANOW, &tio );     // デバイスに設定を行う
#ifdef __linux__
	ioctl(fd, TCSETS, &tio);            // ポートの設定を有効にする
#endif
	return fd;
#endif
}
