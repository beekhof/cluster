#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <time.h>
#include <mntent.h>
#include <poll.h>
#include <signal.h>

#include "global.h"
#include "gfs_ondisk.h"
#include "osi_list.h"
#include "libgfs.h"
#include "copyright.cf"

#define EXTERN
#include "mkfs_gfs.h"



#define OPTION_STRING               ("b:DhJ:j:Op:qr:s:t:VX")





/**
 * print_usage - print out usage information
 *
 */

static void print_usage(void)
{
  printf("Usage:\n");
  printf("\n");
  printf("%s [options] <device>\n", prog_name);
  printf("\n");
  printf("Options:\n");
  printf("\n");
  printf("  -b <bytes>       Filesystem block size\n");
  printf("  -D               Enable debugging code\n");
  printf("  -h               Print this help, then exit\n");
  printf("  -J <MB>          Size of journals\n");
  printf("  -j <num>         Number of journals\n");
  printf("  -O               Don't ask for confirmation\n");
  printf("  -p <name>        Name of the locking protocol\n");
  printf("  -q               Don't print anything\n");
  printf("  -r <MB>          Resource Group Size\n");
  printf("  -s <blocks>      Journal segment size\n");
  printf("  -t <name>        Name of the lock table\n");
  printf("  -V               Print program version information, then exit\n");
}


/**
 * decode_arguments - decode command line arguments and fill in the commandline_t
 * @argc:
 * @argv:
 * @comline: the decoded command line arguments
 *
 */

static void decode_arguments(int argc, char *argv[], commandline_t *comline)
{
  int cont = TRUE;
  int optchar;

  while (cont)
  {
    optchar = getopt(argc, argv, OPTION_STRING);

    switch (optchar)
    {
    case 'b':
      comline->bsize = atoi(optarg);
      break;


    case 'D':
      comline->debug = TRUE;
      break;


    case 'h':
      print_usage();
      exit(EXIT_SUCCESS);
      break;


    case 'J':
      comline->jsize = atoi(optarg);
      break;


    case 'j':
      comline->journals = atoi(optarg);
      break;


    case 'O':
      comline->override = TRUE;
      break;


    case 'p':
      if (strlen(optarg) >= GFS_LOCKNAME_LEN)
	die("lock protocol name %s is too long\n", optarg);
      strcpy(comline->lockproto, optarg);
      break;


    case 'q':
      comline->quiet = TRUE;
      break;


    case 'r':
      comline->rgsize_specified = TRUE;
      comline->rgsize = atoi(optarg);
      break;


    case 's':
      comline->seg_size = atoi(optarg);
      break;


    case 't':
      if (strlen(optarg) >= GFS_LOCKNAME_LEN)
	die("lock table name %s is too long\n", optarg);
      strcpy(comline->locktable, optarg);
      break;


    case 'V':
      printf("gfs_mkfs %s (built %s %s)\n", RELEASE_VERSION, __DATE__, __TIME__);
      printf("%s\n", REDHAT_COPYRIGHT);
      exit(EXIT_SUCCESS);
      break;


    case 'X':
      comline->expert = TRUE;
      break;


    case ':':
    case '?':
      fprintf(stderr, "Please use '-h' for usage.\n");
      exit(EXIT_FAILURE);      
      break;


    case EOF:
      cont = FALSE;
      break;


    default:
      die("unknown option: %c\n", optchar);
      break;
    };
  }


  if (optind < argc)
  {
    comline->device = argv[optind];
    optind++;
  }
  else
    die("no device specified (try -h for help)\n");



  if (optind < argc) 
    die("Unrecognized option: %s\n", argv[optind]);


  if (comline->debug)
  {
    printf("Command Line Arguments:\n");
    printf("  proto = %s\n", comline->lockproto);
    printf("  table = %s\n", comline->locktable);
    printf("  bsize = %u\n", comline->bsize);
    printf("  seg_size = %u\n", comline->seg_size);
    printf("  journals = %u\n", comline->journals);
    printf("  jsize = %u\n", comline->jsize);
    printf("  rgsize = %u\n", comline->rgsize);
    printf("  debug = %d\n", comline->debug);
    printf("  device = %s\n", comline->device);
  }
}

static int get_file_output(int fd, char *buffer, size_t buflen)
{
	struct pollfd pf = { .fd = fd, .events = POLLIN|POLLRDHUP };
	int flags;
	int pos = 0;
	int rv;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return flags;

	flags |= O_NONBLOCK;
	rv = fcntl(fd, F_SETFL, flags);
	if (rv < 0)
		return rv;

	while (1) {
		rv = poll(&pf, 1, 10 * 1000);
		if (rv == 0)
			break;
		if (rv < 0)
			return rv;
		if (pf.revents & POLLIN) {
			rv = read(fd, buffer + pos,
				   buflen - pos);
			if (rv < 0) {
				if (errno == EAGAIN)
					continue;
				return rv;
			}
			if (rv == 0)
				break;
			pos += rv;
			if (pos >= buflen)
				return -1;
			buffer[pos] = 0;
			continue;
		}
		if (pf.revents & (POLLRDHUP | POLLHUP | POLLERR))
			break;
	}
	return 0;
}

static void check_dev_content(const char *devname)
{
	struct sigaction sa;
	char content[1024] = { 0, };
	char * const args[] = {
		(char *)"/usr/bin/file",
		(char *)"-bs",
		(char *)devname,
		NULL };
	int p[2];
	int ret;
	int pid;

	ret = sigaction(SIGCHLD, NULL, &sa);
	if  (ret)
		return;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags |= (SA_NOCLDSTOP | SA_NOCLDWAIT);
	ret = sigaction(SIGCHLD, &sa, NULL);
	if (ret)
		goto fail;

	ret = pipe(p);
	if (ret)
		goto fail;

	pid = fork();

	if (pid < 0) {
		close(p[1]);
		goto fail;
	}

	if (pid) {
		close(p[1]);
		ret = get_file_output(p[0], content, sizeof(content));
		if (ret) {
fail:
			printf("Content of file or device unknown (do you have GNU fileutils installed?)\n");
		} else {
			if (*content == 0)
				goto fail;
			printf("It appears to contain: %s", content);
		}
		close(p[0]);
		return;
	}

	close(p[0]);
	dup2(p[1], STDOUT_FILENO);
	close(STDIN_FILENO);
	exit(execv(args[0], args));
}

/**
 * are_you_sure - protect lusers from themselves
 * @comline: the command line
 *
 */

static void are_you_sure(commandline_t *comline)
{
	char input[32];
	int fd;

	fd = open(comline->device, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		die("Error: device %s not found.\n", comline->device);
	printf("This will destroy any data on %s.\n",
	       comline->device);
	check_dev_content(comline->device);
	close(fd);
	printf("\nAre you sure you want to proceed? [y/n] ");
	if (fgets(input, 32, stdin) == NULL || input[0] != 'y')
		die("aborted\n");
	else
		printf("\n");
}


/**
 * check_mount - check to see if device is mounted/busy
 * @
 *
 */

static void check_mount(char *device)
{
	struct stat st_buf;
	int fd;

	if (stat(device, &st_buf) < 0)
		die("could not stat device %s\n", device);
	if (!S_ISBLK(st_buf.st_mode))
		die("%s is not a block device\n", device);

	fd = open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) {
		if (errno == EBUSY) {
			die("device %s is busy\n", device);
		}
	}
	else {
		close(fd);
	}

	return;
}


/**
 * print_results - print out summary information
 * @comline: the command line
 *
 */

static void print_results(commandline_t *comline)
{
  if (comline->quiet)
    return;

  if (comline->expert)
    printf("Expert mode:               on\n");

  printf("Device:                    %s\n", comline->device);

  printf("Blocksize:                 %u\n", comline->bsize);
  printf("Filesystem Size:           %"PRIu64"\n", comline->fssize);

  printf("Journals:                  %u\n", comline->journals);
  printf("Resource Groups:           %"PRIu64"\n", comline->rgrps);

  printf("Locking Protocol:          %s\n", comline->lockproto);
  printf("Lock Table:                %s\n", comline->locktable);

  printf("\nSyncing...\n");

  sync();

  printf("All Done\n");
}


/**
 * main - do everything
 * @argc:
 * @argv:
 *
 * Returns: 0 on success, non-0 on failure
 */

int main(int argc, char *argv[])
{
	commandline_t comline;
	mkfs_device_t device;
	osi_list_t rlist;
	osi_list_t jlist;
	unsigned int x;

	prog_name = argv[0];

	osi_list_init(&rlist);
	osi_list_init(&jlist);

	/*  Process the command line arguments  */

	memset(&comline, 0, sizeof(commandline_t));
	comline.bsize = MKFS_DEFAULT_BSIZE;
	comline.seg_size = MKFS_DEFAULT_SEG_SIZE;
	comline.jsize = MKFS_DEFAULT_JSIZE;
	comline.rgsize = MKFS_DEFAULT_RGSIZE;
	comline.rgsize_specified = FALSE;
	strcpy(comline.lockproto, MKFS_DEFAULT_LOCKPROTO);

	decode_arguments(argc, argv, &comline);

	check_mount(comline.device);

	if (!comline.expert) {
		char buf[256];
		if (test_locking(comline.lockproto, comline.locktable, buf, 256))
			die("%s\n", buf);
	}

	/*  Block sizes must be a power of two from 512 to 65536  */

	for (x = 512; x; x <<= 1)
		if (x == comline.bsize)
			break;

	if (!x || comline.bsize > 65536)
		die("block size must be a power of two between 512 and 65536\n");

	comline.sb_addr = GFS_SB_ADDR * GFS_BASIC_BLOCK / comline.bsize;

	if (comline.seg_size < 2)
		die("segment size too small\n");

	if (!comline.expert && (uint64)comline.seg_size * comline.bsize > 4194304)
		die("segment size too large\n");

	if (comline.expert) {
		if (1 > comline.rgsize || comline.rgsize > 2048)
			die("bad resource group size\n");
	}
	else {
		if (32 > comline.rgsize || comline.rgsize > 2048)
			die("bad resource group size\n");
	}

	/*  Get the device geometry  */

	memset(&device, 0, sizeof(mkfs_device_t));

	device_geometry(&comline, &device);
	add_journals_to_device(&comline, &device);

	fix_device_geometry(&comline, &device);

	/*  Compute the resource group layouts  */

	compute_rgrp_layout(&comline, &device, &rlist);

	compute_journal_layout(&comline, &device, &jlist);

	/*  Start writing stuff out  */

	comline.fd = open(comline.device, O_RDWR | O_CLOEXEC);
	if (comline.fd < 0)
		die("can't open device %s\n", comline.device);

	if (!comline.override)
		are_you_sure(&comline);

	write_mkfs_sb(&comline, &rlist);

	/*  Figure out where we start allocating in rgrp 0  */
	comline.rgrp0_next = comline.sbd->sd_sb.sb_root_di.no_addr + 1;

	write_jindex(&comline, &jlist);

	write_rindex(&comline, &rlist);

	write_root(&comline);

	write_quota(&comline);

	write_license(&comline);

	write_rgrps(&comline, &rlist);

	write_journals(&comline, &jlist);

	close(comline.fd);
	free(comline.sbd);
	print_results(&comline);

	exit(EXIT_SUCCESS);
}
