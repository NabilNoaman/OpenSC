/*
 * opensc-tool.c: Tool for accessing SmartCards with libopensc
 *
 * Copyright (C) 2001  Juha Yrj�l� <juha.yrjola@iki.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <opensc.h>
#include <sys/stat.h>
#include <ctype.h>

#define OPT_CHANGE_PIN	0x100
#define OPT_LIST_PINS	0x101
#define OPT_READER	0x102
#define OPT_PIN_ID	0x103
#define OPT_NO_CACHE	0x104

int opt_reader = 0, opt_no_cache = 0, opt_debug = 0;
char * opt_apdus[8];
int opt_apdu_count = 0;
int quiet = 0;

const struct option options[] = {
	{ "list-readers",	0, 0, 		'l' },
	{ "list-drivers",	0, 0,		'D' },
	{ "list-files",		0, 0,		'f' },
	{ "send-apdu",		1, 0,		's' },
	{ "reader",		1, 0,		'r' },
	{ "card-driver",	1, 0,		'c' },
	{ "quiet",		0, 0,		'q' },
	{ "debug",		0, 0,		'd' },
	{ 0, 0, 0, 0 }
};

const char *option_help[] = {
	"Lists all configured readers",
	"Lists all installed card drivers",
	"Recursively lists files stored on card",
	"Sends an APDU in format AA:BB:CC:DD:EE:FF...",
	"Uses reader number <arg> [0]",
	"Forces the use of driver <arg> [auto-detect]",
	"Quiet operation",
	"Debug output -- may be supplied several times",
};

struct sc_context *ctx = NULL;
struct sc_card *card = NULL;

int list_readers(void)
{
	int i;
	
	if (ctx->reader_count == 0) {
		printf("No readers configured!\n");
		return 0;
	}
	printf("Configured readers:\n");
	for (i = 0; i < ctx->reader_count; i++) {
		printf("  %d - %s\n", i, ctx->readers[i]);
	}
	return 0;
}

int list_drivers(void)
{
	int i;
	
	if (ctx->card_drivers[0] == NULL) {
		printf("No card drivers installed!\n");
		return 0;
	}
	printf("Configured card drivers:\n");
	for (i = 0; ctx->card_drivers[i] != NULL; i++) {
		printf("  %-16s %s\n", ctx->card_drivers[i]->short_name,
		       ctx->card_drivers[i]->name);
	}
	return 0;
}

int print_file(struct sc_card *card, const struct sc_file *file, const struct sc_path *path, int depth)
{
	int r;
	const char *tmps;
	const char *ac_ops_df[] = {
		"select", "lock", "delete", "create", "rehab", "inval",
		"list"
	};
	const char *ac_ops_ef[] = {
		"read", "update", "write", "erase", "rehab", "inval"
	};
	
	for (r = 0; r < depth; r++)
		printf("  ");
	for (r = 0; r < path->len; r++) {
		printf("%02X", path->value[r]);
		if (r && (r & 1) == 1)
			printf(" ");
	}
	if (file->namelen) {
		printf("[");
		print_binary(stdout, file->name, file->namelen);
		printf("] ");
	}
	switch (file->type) {
	case SC_FILE_TYPE_WORKING_EF:
		tmps = "wEF";
		break;
	case SC_FILE_TYPE_INTERNAL_EF:
		tmps = "iEF";
		break;
	case SC_FILE_TYPE_DF:
		tmps = " DF";
		break;
	default:
		tmps = "unknown";
		break;
	}
	printf("type: %-3s, ", tmps);
	if (file->type != SC_FILE_TYPE_DF) {
		const char *structs[] = {
			"unknown", "transpnt", "linrfix", "linrfix(TLV)",
			"linvar", "linvar(TLV)", "lincyc", "lincyc(TLV)"
		};
		printf("ef structure: %s, ", structs[file->ef_structure]);
	}
	printf("size: %d\n", file->size);
	for (r = 0; r < depth; r++)
		printf("  ");
	if (file->type == SC_FILE_TYPE_DF)
		for (r = 0; r < sizeof(ac_ops_df)/sizeof(ac_ops_df[0]); r++)
			printf("%s[%s] ", ac_ops_df[r], acl_to_str(file->acl[r]));
	else
		for (r = 0; r < sizeof(ac_ops_ef)/sizeof(ac_ops_ef[0]); r++)
			printf("%s[%s] ", ac_ops_ef[r], acl_to_str(file->acl[r]));

	if (file->sec_attr_len) {
		printf("sec: ");
		/* Octets are as follows:
		 *   DF: select, lock, delete, create, rehab, inval
		 *   EF: read, update, write, erase, rehab, inval
		 * 4 MSB's of the octet mean:			 
		 *  0 = ALW, 1 = PIN1, 2 = PIN2, 4 = SYS,
		 * 15 = NEV */
		hex_dump(stdout, file->sec_attr, file->sec_attr_len);
	}
	if (file->prop_attr_len) {
		printf("\n");
		for (r = 0; r < depth; r++)
			printf("  ");
		printf("prop: ");
		hex_dump(stdout, file->prop_attr, file->prop_attr_len);
	}
	printf("\n\n");
#if 1
	if (file->type != SC_FILE_TYPE_DF) {
		u8 buf[2048];
		if (file->ef_structure == SC_FILE_EF_TRANSPARENT) {
			r = sc_read_binary(card, 0, buf, file->size, 0);
			if (r > 0)
				hex_dump_asc(stdout, buf, r, 0);
		} else {
			r = sc_read_record(card, 0, buf, file->size, 0);
			if (r > 0)
				hex_dump_asc(stdout, buf, r, 0);
		}
	}
#endif
	return 0;
}

int enum_dir(struct sc_path path, int depth)
{
	struct sc_file file;
	int r;
	u8 files[MAX_BUFFER_SIZE];

	file.magic = 0;  /* make sure the file is invalid */
	r = sc_select_file(card, &path, &file);
	if (r) {
		fprintf(stderr, "SELECT FILE failed: %s\n", sc_strerror(r));
		return 1;
	}
	if (sc_file_valid(&file))
		print_file(card, &file, &path, depth);
	if (!sc_file_valid(&file) || file.type == SC_FILE_TYPE_DF) {
		int i;

		r = sc_list_files(card, files, sizeof(files));
		if (r <= 0) {
			fprintf(stderr, "sc_list_files() failed: %s\n", sc_strerror(r));
			return 1;
		}
		for (i = 0; i < r/2; i++) {
			struct sc_path tmppath;

			memcpy(&tmppath, &path, sizeof(path));
			memcpy(tmppath.value + tmppath.len, files + 2*i, 2);
			tmppath.len += 2;
			enum_dir(tmppath, depth + 1);
		}
	}
	return 0;
}	

int list_files(void)
{
	struct sc_path path;
	int r;
	
	sc_format_path("3F00", &path);
	r = enum_dir(path, 0);
	return r;
}

int send_apdu(void)
{
	struct sc_apdu apdu;
	u8 buf[MAX_BUFFER_SIZE], sbuf[MAX_BUFFER_SIZE],
	   rbuf[MAX_BUFFER_SIZE], *p;
	size_t len, len0, r;
	int c;

	for (c = 0; c < opt_apdu_count; c++) {
		len0 = sizeof(buf);
		sc_hex_to_bin(opt_apdus[c], buf, &len0);
		if (len0 < 4) {
			fprintf(stderr, "APDU too short (must be at least 4 bytes).\n");
			return 2;
		}
		len = len0;
		p = buf;
		apdu.cla = *p++;
		apdu.ins = *p++;
		apdu.p1 = *p++;
		apdu.p2 = *p++;
		apdu.resp = rbuf;
		apdu.resplen = sizeof(rbuf);
		apdu.data = NULL;
		apdu.datalen = 0;
		len -= 4;
		if (len > 1) {
			apdu.lc = *p++;
			len--;
			memcpy(sbuf, p, apdu.lc);
			apdu.data = sbuf;
			apdu.datalen = apdu.lc;
			if (len < apdu.lc) {
				fprintf(stderr, "APDU too short (need %d bytes).\n",
					apdu.lc-len);
				return 2;
			}
			len -= apdu.lc;
			if (len) {
				apdu.le = *p++;
				len--;
				apdu.cse = SC_APDU_CASE_4_SHORT;
			} else
				apdu.cse = SC_APDU_CASE_3_SHORT;
			if (len) {
				fprintf(stderr, "APDU too long (%d bytes extra).\n", len);
				return 2;
			}
		} else if (len == 1) {
			apdu.le = *p++;
			len--;
			apdu.cse = SC_APDU_CASE_2_SHORT;
		} else
			apdu.cse = SC_APDU_CASE_1;
		printf("Sending: ");
		for (r = 0; r < len0; r++)
			printf("%02X ", buf[r]);
		printf("\n");
	#if 0
		ctx->debug = 5;
	#endif
		r = sc_transmit_apdu(card, &apdu);
	#if 0
		ctx->debug = opt_debug;
	#endif
		if (r) {
			fprintf(stderr, "APDU transmit failed: %s\n", sc_strerror(r));
			return 1;
		}
		printf("Received (SW1=0x%02X, SW2=0x%02X)%s\n", apdu.sw1, apdu.sw2,
		       apdu.resplen ? ":" : "");
		if (apdu.resplen)
			hex_dump_asc(stdout, apdu.resp, apdu.resplen, -1);
	}
	return 0;
}

int main(int argc, char * const argv[])
{
	int err = 0, r, c, long_optind = 0;
	int do_list_readers = 0;
	int do_list_drivers = 0;
	int do_list_files = 0;
	int do_send_apdu = 0;
	int action_count = 0;
	const char *opt_driver = NULL;
		
	while (1) {
		c = getopt_long(argc, argv, "lfr:qds:Dc:", options, &long_optind);
		if (c == -1)
			break;
		if (c == '?')
			print_usage_and_die("opensc-tool");
		switch (c) {
		case 'l':
			do_list_readers = 1;
			action_count++;
			break;
		case 'D':
			do_list_drivers = 1;
			action_count++;
			break;
		case 'f':
			do_list_files = 1;
			action_count++;
			break;
		case 's':
			opt_apdus[opt_apdu_count] = optarg;
			do_send_apdu++;
			if (opt_apdu_count == 0)
				action_count++;
			opt_apdu_count++;
			break;
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 'q':
			quiet++;
			break;
		case 'd':
			opt_debug++;
			break;
		case 'c':
			opt_driver = optarg;
			break;
		}
	}
	if (action_count == 0)
		print_usage_and_die("opensc-tool");
	r = sc_establish_context(&ctx);
	if (r) {
		fprintf(stderr, "Failed to establish context: %s\n", sc_strerror(r));
		return 1;
	}
	ctx->use_std_output = 1;
	ctx->debug = opt_debug;
	if (opt_no_cache)
		ctx->use_cache = 0;
	if (do_list_readers) {
		if ((err = list_readers()))
			goto end;
		action_count--;
	}
	if (do_list_drivers) {
		if ((err = list_drivers()))
			goto end;
		action_count--;
	}
	if (action_count <= 0)
		goto end;
	if (opt_reader >= ctx->reader_count || opt_reader < 0) {
		fprintf(stderr, "Illegal reader number. Only %d reader(s) configured.\n", ctx->reader_count);
		err = 1;
		goto end;
	}
	if (sc_detect_card(ctx, opt_reader) != 1) {
		fprintf(stderr, "Card not present.\n");
		err = 3;
		goto end;
	}
	if (opt_driver != NULL) {
		err = sc_set_default_card_driver(ctx, opt_driver);
		if (err) {
			fprintf(stderr, "Driver '%s' not found!\n", opt_driver);
			err = 1;
			goto end;
		}
	}
	if (!quiet)
		fprintf(stderr, "Connecting to card in reader %s...\n", ctx->readers[opt_reader]);
	r = sc_connect_card(ctx, opt_reader, &card);
	if (r) {
		fprintf(stderr, "Failed to connect to card: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	printf("Using card driver: %s\n", card->driver->name);
	r = sc_lock(card);
	if (r) {
		fprintf(stderr, "Unable to lock card: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	
	if (do_send_apdu) {
		if ((err = send_apdu()))
			goto end;
		action_count--;
	}
	
	if (do_list_files) {
		if ((err = list_files()))
			goto end;
		action_count--;
	}
end:
	if (card) {
		sc_unlock(card);
		sc_disconnect_card(card);
	}
	if (ctx)
		sc_destroy_context(ctx);
	return err;
}
