#define _POSIX_C_SOURCE 200112L

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "server.h"
#include "terminal.h"


typedef enum size_var
{
	SIZE_ADDR = 64,
	SIZE_PORT = 16,
	SIZE_OTH = 255
}
size_var_t;


static void usage()
{
	LOG_WARN("Usage:\n");
	LOG_WARN("-o<optional> : logfile name\n");
	LOG_WARN("-a : ip address\n");
	LOG_WARN("-p : port\n");
	LOG_WARN("-u<optional> : username\n");
	LOG_WARN("-k<optional> : password");
}

static void parse_args(int n, char **args,
	char addr[64], char port[16],
	char username[255],	char passwd[255],
	char outfile[255])
{
	char option;
	while((option = getopt(n, args, "a:p:u:k:o:")) > 0)
	{
		switch(option)
		{
			case 'a':
			{
				strncpy(addr, optarg, SIZE_ADDR);
				break;
			}
			case 'p':
			{
				strncpy(port, optarg, SIZE_PORT);
				break;
			}
			case 'u':
			{
				strncpy(username, optarg, SIZE_OTH);
				break;
			}
			case 'k':
			{
				strncpy(passwd, optarg, SIZE_OTH);
				break;
			}
			case 'o':
			{
				strncpy(outfile, optarg, SIZE_OTH);
				break;
			}
		}
	}
}

int main(int n, char **args)
{
	char addr[SIZE_ADDR] = "";
	char port[SIZE_PORT] = "";
	char username[SIZE_OTH] = "";
	char passwd[SIZE_OTH] = "";
	char outfile[SIZE_OTH] = "";

	parse_args(n, args,
		addr, port,
		username, passwd,
		outfile);

	log_init(outfile, LOG_LEVEL_INFO);

	if (strcmp(port, "") == 0 || strcmp(addr, "") == 0)
	{
		usage();
		return EXIT_FAILURE;
	}

	sigign();
	terminal_start();

	LOG_INFO("Configured server at %s:%s (user=%s)", addr, port, username);

	if (server_init(addr, port, username, passwd) < 0)
	{
		return EXIT_FAILURE;
	}

	LOG_INFO("Server initialization OK on %s:%s", addr, port);

	if (server_start() < 0)
	{
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;

}
