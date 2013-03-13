/*
 * fsfreeze.c -- Filesystem freeze/unfreeze IO for Linux
 *
 * Copyright (C) 2010 Hajime Taira <htaira@redhat.com>
 *                    Masatake Yamato <yamato@redhat.com>
 *
 * This program is free software.  You can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation: either version 1 or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>

#include "c.h"
#include "blkdev.h"
#include "nls.h"
#include "closestream.h"
#include "optutils.h"
#include "timer.h"
#include "strutils.h"

static int freeze_f(int fd)
{
	return ioctl(fd, FIFREEZE, 0);
}

static int unfreeze_f(int fd)
{
	return ioctl(fd, FITHAW, 0);
}


static sig_atomic_t timeout_expired = 0;

static void timeout_handler(int sig __attribute__((__unused__)))
{
	timeout_expired = 1;
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, USAGE_HEADER);
	fprintf(out,
	      _(" %s [options] <mountpoint>\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -f, --freeze                freeze the filesystem\n"), out);
	fputs(_(" -U, --auto-unfreeze <secs>  automatically unfreeze after timeout\n"), out);
	fputs(_(" -u, --unfreeze              unfreeze the filesystem\n"), out);
	fprintf(out, USAGE_SEPARATOR);
	fprintf(out, USAGE_HELP);
	fprintf(out, USAGE_VERSION);
	fprintf(out, USAGE_MAN_TAIL("fsfreeze(8)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	struct itimerval timer;
	int auto_unfreeze = 0;
	int fd = -1, c;
	int freeze = -1, rc = EXIT_FAILURE;
	char *path;
	struct stat sb;

	static const struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "freeze",    0, 0, 'f' },
	    { "auto-unfreeze", 1, 0, 'U' },
	    { "unfreeze",  0, 0, 'u' },
	    { "version",   0, 0, 'V' },
	    { NULL,        0, 0, 0 }
	};

	static const ul_excl_t excl[] = {       /* rows and cols in in ASCII order */
		{ 'U','u' },                    /* auto-unfreeze, unfreeze */
		{ 'f','u' },			/* freeze, unfreeze */
		{ 0 }
	};
	int excl_st[ARRAY_SIZE(excl)] = UL_EXCL_STATUS_INIT;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	memset(&timer, 0, sizeof timer);

	while ((c = getopt_long(argc, argv, "hfU:uV", longopts, NULL)) != -1) {

		err_exclusive_options(c, longopts, excl, excl_st);

		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'f':
			freeze = TRUE;
			break;
		case 'U':
			auto_unfreeze = 1;
			strtotimeval_or_err(optarg, &timer.it_value,
				_("invalid timeout value"));
			if (timer.it_value.tv_sec + timer.it_value.tv_usec == 0)
				errx(EXIT_FAILURE, _("timeout cannot be zero"));
			break;
		case 'u':
			freeze = FALSE;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
		default:
			usage(stderr);
			break;
		}
	}

	if (freeze == -1)
		usage(stderr);
	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified"));
	path = argv[optind++];

	if (optind != argc) {
		warnx(_("unexpected number of arguments"));
		usage(stderr);
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, _("cannot open %s"), path);

	if (fstat(fd, &sb) == -1) {
		warn(_("stat failed %s"), path);
		goto done;
	}

	if (!S_ISDIR(sb.st_mode)) {
		warnx(_("%s: is not a directory"), path);
		goto done;
	}

	if (freeze) {
		if (freeze_f(fd)) {
			warn(_("%s: freeze failed"), path);
			goto done;
		}
	}

	if (auto_unfreeze) {
		struct sigaction old_sa;
		struct itimerval old_timer;

		if (setup_timer(&timer, &old_timer, &old_sa, timeout_handler))
			warnx(_("failed to setup timeout, unfreeze %s"), path);
		else
			pause();
		cancel_timer(&old_timer, &old_sa);
		freeze = 0;
	}

	if (!freeze) {
		if (unfreeze_f(fd)) {
			warn(_("%s: unfreeze failed"), path);
			goto done;
		}
	}

	rc = EXIT_SUCCESS;
done:
	if (fd >= 0)
		close(fd);
	return rc;
}

