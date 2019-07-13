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

static volatile HANDLE fd = INVALID_HANDLE_VALUE;

static void usage(void) {
	printf("Usage: ft245tools [-d device name] COMMAND [Parameters]\n");
	printf(" SEND:\n");
	printf("  rawsend filename.*\n");
	printf("    - Send data without no headers.\n");
	printf("      (with FT245DRV)\n");
	printf("  binsend filename.* dest-address [exec-address]\n");
	printf("    - Send data to specified destination address, and automatic exec.\n");
	printf("      (with FT245TRN)\n");
	printf("  d77send filename.D77\n");
	printf("    - Send D77 image data and write it to FD.\n");
	printf("      (with FT245D77)\n");
	printf("  ascsend filename.*\n");
	printf("    - Send ascii text data (EOF = $1a).\n");
	printf("      (with MMASCRCV)\n");
	printf(" RECEIVE:\n");
	printf("  binrecv filename.*\n");
	printf("    - Receive data from FM-8/7/77.\n");
	printf("      (with FT245TRN)\n");
	printf("  d77recv filename.D77\n");
	printf("    - Receive D77 image data and write as D77.\n");
	printf("      (with MMD77SND)\n");
	printf("  ascrecv filename.*\n");
	printf("    - Receive ascii text data (EOF = $1a).\n");
	printf("      (with MMASCSND)\n");
	printf(" DRIVE-EMULATION:\n");
	printf("  bubemul directory\n");
	printf("    - Special BUBR command filesystem emulation.\n");
	printf("      (with BUBEMUL)\n");
	printf("  d77emul filename.D77\n");
	printf("    - FD simulation on FM-7 with D77 disk image.\n");
	printf("      (with DRVEMUL)\n");
}

static int rawsend(HANDLE fd, int argc, const char **argv) {
	printf("Send as binary: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if (send_file(fd, argv[2], -1, -1) < 0) {
		printf("Error: failed to send file.\n");
		return 1;
	}
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int binsend(HANDLE fd, int argc, const char **argv) {
	printf("Send as binary: ");
	int start = 0, exec = 0, sent = 0;
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	if (argc >= 4)
		start = htoi(argv[3]);
	if (argc >= 5)
		exec = htoi(argv[4]);
	if (start <= 0 || start > 0xffff) {
		printf("Start address is invalid, or not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if ((sent = send_file(fd, argv[2], start, exec)) < 0) {
 		printf("Error: failed to send file.\n");
		return 1;
	}
	printf("start = $%04x, end = $%04x\n", start, start + sent - 1);
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int binrecv(HANDLE fd, int argc, const char **argv) {
	int size = 0;
	printf("Receive as binary: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if ((size = recv_file(fd, argv[2])) < 0) {
 		printf("Error: failed to receive file.\n");
		return 1;
	}
	printf("%d bytes received.\n", size);
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int d77send(HANDLE fd, int argc, const char **argv) {
	printf("Send as D77 disk image: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if (send_d77(fd, argv[2]) < 0) {
		printf("Error: failed to send disk image.\n");
		return 1;
	}
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int d77recv(HANDLE fd, int argc, const char **argv) {
	printf("Receive as D77 disk image: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if (recv_d77(fd, argv[2]) < 0) {
		printf("Error: failed to write disk image.\n");
		return 1;
	}
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int d77emul(HANDLE fd, int argc, const char **argv) {
	printf("D77 disk emulation: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	const char *file1 = argv[2], *file2 = NULL;
	printf("%s", file1);
	if (argc > 3) {
		printf(", %s", file2);
		file2 = argv[3];
	}
	printf("\n");
	if (emul_d77(fd, file1, file2) < 0) {
		printf("Error: failed to open disk image.\n");
		return 1;
	}
	return 0;
}

static int bubemul(HANDLE fd, int argc, const char **argv) {
	printf("BUBR command filesystem emulation: ");
	if (argc < 3) {
		printf("Base directory path is not specified.\n");
		return -1;
	}
	const char *dirname = argv[2];
	if (!is_dir(dirname)) {
		printf("Directory path should be specified.\n");
		return -1;
	}
	printf("%s\n", dirname);
	if (emul_bub(fd, dirname) < 0) {
		printf("Error: Failed, anything goes wrong...\n");
		return 1;
	}
	return 0;
}

static int ascsend(HANDLE fd, int argc, const char **argv) {
	int sent = 0;
	printf("Send as ascii: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if ((sent = send_ascii_file(fd, argv[2])) < 0) {
 		printf("Error: failed to send file.\n");
		return 1;
	}
	printf("%d bytes sent.\n", sent);
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

static int ascrecv(HANDLE fd, int argc, const char **argv) {
	int size = 0;
	printf("Receive as ascii: ");
	if (argc < 3) {
		printf("File name is not specified.\n");
		return -1;
	}
	printf("%s\n", argv[2]);
	if ((size = recv_ascii_file(fd, argv[2])) < 0) {
 		printf("Error: failed to receive file.\n");
		return 1;
	}
	printf("%d bytes received.\n", size);
	printf("Completed. (CTRL-C to quit)\n");
	char b[1];
	block_read(fd, b, 1);
	return 0;
}

#ifdef WIN32
static int set_priority(void) {
	HANDLE hndl = GetCurrentProcess();
	return SetPriorityClass(hndl, REALTIME_PRIORITY_CLASS) ? 0 : 1;
}

static int other_settings(void) {
	return set_priority();
}
#else
static void sig_action(int sig, siginfo_t *info, void *ctx) {
	if (fd >= 0)
		close(fd);
	fd = -1;
	exit(0);
}

static void signal_setting(void) {
	struct sigaction sig;
	sigset_t sigset;
	int res = sigemptyset(&sigset);
	memset(&sig, 0, sizeof(sig));
	sig.sa_sigaction = sig_action;
	if (res >= 0)
		sig.sa_mask = sigset;
	sig.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sig, NULL);
	sigaction(SIGHUP, &sig, NULL);
	sigaction(SIGQUIT, &sig, NULL);
	sigaction(SIGKILL, &sig, NULL);
}

static int other_settings(void) {
	signal_setting();
	return 0;
}
#endif

int main(int argc, const char *argv[]) {
	int ret = 1, stp = 1;
	const char *device_name = SERIAL_PORT;
	struct {
		const char *command;
		int (*func)(HANDLE, int, const char **);
	} *cmd = NULL, cmdtbl[] = {
		{ "rawsend", rawsend },
		{ "binsend", binsend },
		{ "binrecv", binrecv },
		{ "d77send", d77send },
		{ "d77recv", d77recv },
		{ "d77emul", d77emul },
		{ "bubemul", bubemul },
		{ "ascsend", ascsend },
		{ "ascrecv", ascrecv },
		{ NULL, NULL }
	};

	printf("ft245tools Version 1.0\n");
	printf("Copyright (C) 2019 by odaman68000. All rights reserved.\n\n");

	if (argc >= 3)
		if (strcasecmp("-d", argv[1]) == 0) {
			device_name = argv[2];
			stp += 2;
		}
	if (argc <= stp) {
		usage();
		goto error;
	}
	for (int i = 0; cmdtbl[i].command != NULL; i++)
		if (strcasecmp(cmdtbl[i].command, argv[stp]) == 0) {
			cmd = &cmdtbl[i];
			break;
		}
	if (cmd == NULL) {
		usage();
		goto error;
	}

	other_settings();

	if ((fd = open_serial_device(device_name, B38400)) == INVALID_HANDLE_VALUE) {	// デバイスをオープンする
        printf("Error: serial device open error. (%s)\n", device_name);
		goto error;
    }
	// okay, here we gooooooo!!
	if ((ret = cmd->func(fd, argc - stp + 1, argv + stp - 1)) < 0)
		usage();
error:
	if (fd != INVALID_HANDLE_VALUE)
		close(fd);									// デバイスのクローズ
	fd = INVALID_HANDLE_VALUE;
    return ret < 0 ? -ret : ret;
}
