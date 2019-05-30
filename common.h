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

#ifndef _COMMON_H__
#define _COMMON_H__

//#define DEBUG	1

#ifdef LINUX
#define SERIAL_PORT		"/dev/ttyUSB0"
#else
#define SERIAL_PORT		"/dev/cu.usbserial-A104B2QE"
#endif

#define FT245RL_BLOCK	64
#define MAX16BIT		65536
#define ARRAY_COUNT(a)	(sizeof(a) / sizeof(a[0]))

//RCB
typedef struct {
	char command;
	char result;
	unsigned short address;
	char track;
	char sector;
	char side;
	char drive;
} RCB_DATA;

//BASIC Header (ビッグエンディアン)
typedef struct {
	unsigned char type1;		//0xff
	unsigned char type2;		//0xff
	unsigned char type3;		//0xff
} BAS_HEADER;

//BASIC Footer (ビッグエンディアン)
typedef struct {
	unsigned char type;			//0x00
	unsigned char eof;			//0x1a
} BAS_FOOTER;

//Binary Header (ビッグエンディアン)
typedef struct {
	unsigned char type;			//0x00
	unsigned short length;
	unsigned short start;
} BIN_HEADER;

//Binary Footer (ビッグエンディアン)
typedef struct {
	unsigned char type;			//0xff
	unsigned short reserved;	//0x0000
	unsigned short exec;
	unsigned char eof;			//0x1a
} BIN_FOOTER;

//FT245TRN向けファイル情報ヘッダ (ビッグエンディアン)
typedef struct {
	unsigned short start;
	unsigned short bottom;
	unsigned short exec;
} FILEINFO;

//D77の物理セクタ情報テーブル (リトルエンディアン)
typedef struct {
	char track;		//トラック (0〜39)
	char side;		//サイド (0:表, 1:裏)
	char sector;	//セクタ (1〜16)
	char reserved[11];
	unsigned short size;
} D77_SECTOR_DATA; 

//FM-7 BUBEMUL側に返却するコマンドコード
typedef enum {
	CMD_ERROR = 0,	//Syntax error
	CMD_PRINT,		//PRINT
	CMD_SAVE,		//SAVE (BASIC)
	CMD_SAVEM,		//SAVEM (Binary)
	CMD_LOAD,		//LOAD (BASIC)
	CMD_LOADA,		//ASCII LOAD (BASIC)
	CMD_LOADM,		//LOADM (Binary)
	CMD_END = 0xff	//CONTINUOUS COMMAND END
} BUBCMD;

//util.c
void dump(const void *data, int size);
int is_dir(const char *path);
int is_file(const char *path);
int is_file_in_path(const char *file, const char *path);
int htoi(const char *v);
int logical_sector_RCB(const RCB_DATA *rcb);
int logical_sector_D77(const D77_SECTOR_DATA *d77);
int split_csv(char *buffer, char **values, int count);
char *get_quoted_filename(unsigned char *value, int offset);

//io.h
int serial_read_string(int fd, char *buffer, int length);
int block_read(int fd, void *data, int len);
int block_write(int fd, const void *data, int len);
int block_write_byte(int fd, int d);
int block_write_string(int fd, const char *string);
int block_write_string_result(int fd, const char *message);
int get_sector_file(FILE *fp, int sector, unsigned char *data);
int get_sector_mem(const unsigned char *image, unsigned long len, int sector, unsigned char *data);
int put_sector_mem(unsigned char *image, unsigned long len, int sector, unsigned char *data);
void *get_file_image(const char *filename, unsigned long *filesize);
int put_file_image(const char *filename, const unsigned char *data, unsigned long length);
int recv_file(int fd, const char *filename);
int send_file(int fd, const char *filename, unsigned int start, unsigned int exec);
int recv_mem(int fd, void *buffer);
int send_mem(int fd, const void *buffer, int length, unsigned int st, unsigned int ex);
int open_serial_device(int baudRate);

//bubemul.c
int emul_bub(int fd, const char *dirname);

//d77emul.c
int send_d77(int fd, const char *filename);
int emul_d77(int fd, const char *filename1, const char *filename2);

#endif
