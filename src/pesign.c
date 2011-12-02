/*
 * Copyright 2011 Red Hat, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s): Peter Jones <pjones@redhat.com>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <popt.h>

#include <nss3/cert.h>
#include <nss3/pkcs7t.h>

#include "pesign.h"

int main(int argc, char *argv[])
{
	int rc;

	char *infile = NULL, *outfile = NULL, *certfile = NULL;
	int infd = -1, outfd = -1;

	Pe *inpe = NULL, *outpe = NULL;
	CERTCertificate *cert = NULL;

	int force = 0;
	int hashgaps = 1;

	poptContext optCon;
	struct poptOption options[] = {
		{"in", 'i', POPT_ARG_STRING, &infile, 0,
			"specify input file", "<infile>"},
		{"out", 'o', POPT_ARG_STRING, &outfile, 0,
			"specify output file", "<outfile>" },
		{"certficate", 'c', POPT_ARG_STRING, &certfile, 0,
			"specify certificate file", "<certificate>" },
		{"force", 'f', POPT_ARG_NONE|POPT_ARG_VAL, &force,  1,
			"force overwriting of output file", NULL },
		{"nogaps", 'n', POPT_ARG_NONE|POPT_ARG_VAL, &hashgaps, 0,
			"skip gaps between sections", NULL },
		POPT_AUTOHELP
		POPT_TABLEEND
	};
	mode_t outmode = 0644;
	struct stat statbuf;

	optCon = poptGetContext("pesign", argc, (const char **)argv, options,0);

	while ((rc = poptGetNextOpt(optCon)) > 0)
		;

	if (rc < -1) {
		fprintf(stderr, "pesign: Invalid argument: %s\n",
			poptStrerror(rc));
		exit(1);
	}

	if (!infile) {
		fprintf(stderr, "No input file specified.\n");
		exit(1);
	}
	if (!outfile) {
		fprintf(stderr, "No output file specified.\n");
		exit(1);
	}

	if (!strcmp(infile, outfile) && strcmp(infile,"-")) {
		fprintf(stderr, "pesign: in-place file editing is not yet "
				"supported\n");
		exit(1);
	}

	if (!strcmp(infile, "-")) {
		infd = STDIN_FILENO;
	} else {
		infd = open(infile, O_RDONLY|O_CLOEXEC);
		stat(infile, &statbuf); 
		outmode = statbuf.st_mode;
	}
	if (infd < 0) {
		fprintf(stderr, "Error opening input: %m\n");
		exit(1);
	}

	if (!strcmp(outfile, "-")) {
		outfd = STDOUT_FILENO;
	} else {
		if (access(outfile, F_OK) == 0 && force == 0) {
			fprintf(stderr, "pesign: \"%s\" exits and --force was "
					"not given.\n", outfile);
			exit(1);
		}
		outfd = open(outfile, O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC,outmode);
	}
	if (outfd < 0) {
		fprintf(stderr, "Error opening output: %m\n");
		exit(1);
	}

	rc = crypto_init();
	if (rc < 0) {
		fprintf(stderr, "Could not initialize cryptographic library\n");
		exit(1);
	}

	if (certfile) {
		int certfd = open(certfile, O_RDONLY|O_CLOEXEC);

		if (certfd < 0) {
			fprintf(stderr, "pesign: could not open certificate "
					"\"%s\": %m\n", certfile);
			exit(1);
		}

		rc = read_cert(certfd, &cert);
		if (rc < 0) {
			fprintf(stderr, "pesign: could not read certificate\n");
			exit(1);
		}
	}

	Pe_Cmd cmd = infd == STDIN_FILENO ? PE_C_READ : PE_C_READ_MMAP;
	inpe = pe_begin(infd, cmd, NULL);
	if (!inpe) {
		fprintf(stderr, "pesign: could not load input file: %s\n",
			pe_errmsg(pe_errno()));
		exit(1);
	}

	cmd = outfd == STDOUT_FILENO ? PE_C_WRITE : PE_C_WRITE_MMAP;
	outpe = pe_begin(outfd, cmd, inpe);
	if (!outpe) {
		fprintf(stderr, "pesign: could not load output file: %s\n",
			pe_errmsg(pe_errno()));
		exit(1);
	}

	if (cert) {
		pe_sign(outpe, cert);
	}
	pe_end(inpe);
	pe_end(outpe);

	close(infd);
	close(outfd);
	crypto_fini();
	return 0;
}
