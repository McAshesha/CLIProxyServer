#define _POSIX_C_SOURCE 200112L

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "server.h"


static void usage()
{
	LOG_WARN("Usage:\n"
			"-o<optional> : logfile name\n"
			"-a : ip address\n"
			"-p : port \n"
			"-u<optional> : username\n"
			"-k<optional> : password\n"
			);
}

int main(int n, char **args)
{
	sigign();

	char option;
	char addr[64] = "";
	char port[16] = "";
	char username[255] = "";
	char passwd[255] = "";
	char outfile[255] = "";

	while((option = getopt(n, args, "a:p:u:k:o:")) > 0)
	{
		switch(option)
		{
			case 'a':
			{
				strncpy(addr, optarg, sizeof(addr));
				break;
			}
			case 'p':
			{
				strncpy(port, optarg, sizeof(port));
				break;
			}
			case 'u':
			{
				strncpy(username, optarg, sizeof(username));
				break;
			}
			case 'k':
			{
				strncpy(passwd, optarg, sizeof(passwd));
				break;
			}
			case 'o':
			{
				strncpy(outfile, optarg, sizeof(outfile));
				break;
			}
			default:
			{
				usage();
				return EXIT_FAILURE;
			}
		}
	}

	if (strcmp(port, "") == 0 || strcmp(addr, "") == 0)
	{
		usage();
		return EXIT_FAILURE;
	}

	log_init(outfile, LOG_LEVEL_INFO);
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
