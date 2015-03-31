/**
 * \file ihkosdump.c
 *  License details are found in the file LICENSE.
 * \brief
 *  dump IHK os memory
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011 - 2012  Taku Shimosawa
 * \author Gou Nakamura  <go.nakamura.yw@hitachi-solutions.com> \par
 * 	Copyright (C) 2015  RIKEN AICS
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

int __argc;
char **__argv;

static int usage(char **arg)
{
	char	*cmd;

	cmd = strrchr(arg[0], '/');
	if(cmd)
		cmd++;
	else
		cmd = arg[0];
	fprintf(stderr, "Usage: %s (dev #) (action)\n", cmd);
	fprintf(stderr, "action:\n");
	fprintf(stderr, "    dump [file]\n");

	return 0;
}

#include <bfd.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>

static int do_dump(int osfd) {
	static char path[PATH_MAX];
	static char hname[HOST_NAME_MAX+1];
	bfd *abfd = NULL;
	char *fname;
	bfd_boolean ok;
	asection *scn;
	dumpargs_t args;
	uintptr_t start;
	uintptr_t end;
	int error;
	size_t bsize;
	void *buf = NULL;
	uintptr_t addr;
	size_t cpsize;
	time_t t;
	struct tm *tm;
	size_t n;
	char *date;
	struct passwd *pw;

	t = time(NULL);
	if (t == (time_t)-1) {
		perror("time");
		return 1;
	}
	tm = localtime(&t);
	if (!tm) {
		perror("localtime");
		return 1;
	}
	gethostname(hname, sizeof(hname));

	pw = getpwuid(getuid());

	args.cmd = DUMP_NMI;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	if (error) {
		perror("DUMP_NMI");
		return 1;
	}

	args.cmd = DUMP_QUERY;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	if (error) {
		perror("DUMP_QUERY");
		return 1;
	}
	start = args.start;
	end = args.start + args.size;

	bsize = 0x100000;
	buf = malloc(bsize);
	if (!buf) {
		perror("malloc");
		return 1;
	}

	bfd_init();

	if (__argc >= 4) {
		fname = __argv[3];
	}
	else {
		n = strftime(path, sizeof(path), "mcdump_%Y%m%d_%H%M%S", tm);
		if (!n) {
			perror("strftime");
			return 1;
		}
		fname = path;
	}

	abfd = bfd_fopen(fname, "elf64-x86-64", "w", -1);
	if (!abfd) {
		bfd_perror("bfd_fopen");
		return 1;
	}

	ok = bfd_set_format(abfd, bfd_object);
	if (!ok) {
		bfd_perror("bfd_set_format");
		return 1;
	}

	date = asctime(tm);
	if (date) {
		cpsize = strlen(date) - 1;	/* exclude trailing '\n' */
		scn = bfd_make_section_anyway(abfd, "date");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(date)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	error = gethostname(hname, sizeof(hname));
	if (!error) {
		cpsize = strlen(hname);
		scn = bfd_make_section_anyway(abfd, "hostname");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(hostname)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	pw = getpwuid(getuid());
	if (pw) {
		cpsize = strlen(pw->pw_name);
		scn = bfd_make_section_anyway(abfd, "user");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(user)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	scn = bfd_make_section_anyway(abfd, "physmem");
	if (!scn) {
		bfd_perror("bfd_make_section_anyway(physmem)");
		return 1;
	}

	ok = bfd_set_section_size(abfd, scn, end-start);
	if (!ok) {
		bfd_perror("bfd_set_section_size");
		return 1;
	}

	ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	if (!ok) {
		bfd_perror("bfd_set_setction_flags");
		return 1;
	}
	scn->vma = start;

	scn = bfd_get_section_by_name(abfd, "date");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, date, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(date)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "hostname");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, hname, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(hostname)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "user");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, pw->pw_name, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(user)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "physmem");
	for (addr = start; addr < end; addr += cpsize) {
		cpsize = end - addr;
		if (cpsize > bsize) {
			cpsize = bsize;
		}

		args.cmd = DUMP_READ;
		args.start = addr;
		args.size = cpsize;
		args.buf = buf;

		error = ioctl(osfd, IHK_OS_DUMP, &args);
		if (error) {
			perror("DUMP_READ");
			return 1;
		}

		ok = bfd_set_section_contents(abfd, scn, buf, addr-start, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(physmem)");
			return 1;
		}
	}

	ok = bfd_close(abfd);
	if (!ok) {
		bfd_perror("bfd_close");
		return 1;
	}
	return 0;
}

#define HANDLER(name) if (!strcmp(argv[2], #name)) { int r = do_##name(fd); close(fd); return r; }
int main(int argc, char **argv)
{
	int fd;
	char fn[128];

	__argc = argc;
	__argv = argv;

	if (argc < 3) {
		usage(argv);
		return 1;
	}

	sprintf(fn, "/dev/mcos%d", atoi(argv[1]));

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	HANDLER(dump)
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
		usage(argv);
	}

	close(fd);
	return 0;
}
