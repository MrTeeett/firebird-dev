/*
 *
 *      PROGRAM:        InterBase server manager
 *      MODULE:         ibmgr.cpp
 *      DESCRIPTION:    server manager's routines
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 * $Id: srvrmgr.cpp,v 1.11 2004-05-02 23:06:21 skidder Exp $
 */

#include "firebird.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>
#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif


#include "../jrd/common.h"
#include "../jrd/ibase.h"
#include "../jrd/gds_proto.h"
#include "../jrd/gdsassert.h"
#include "../jrd/svc_undoc.h"
#include "../utilities/ibmgr/ibmgr.h"
#include "../utilities/ibmgr/srvrmgr_proto.h"

#define STUFF_WORD(p, value)    {*p++ = value; *p++ = value >> 8;}

#define SPB_BUFLEN              128	/* service params buffer length */
#define SEND_BUFLEN             32	/* length of send and resp */
#define RESP_BUFLEN             128	/* used by isc_service_query */

/* After we fork and exec a guardian process, to determine
   if the server have started we wait ATTACH_PAUSE seconds
   and try to attach to it. This happens ATTACH_RETRY number
   of times
*/
#define ATTACH_PAUSE		1	/* seconds to pause before attach */
#define ATTACH_RETRY		10	/* Number of attach retries */


static bool attach_service(ibmgr_data_t*);
static bool detach_service(ibmgr_data_t*);
static bool print_pool(ibmgr_data_t*);
static bool start_shutdown(ibmgr_data_t*);
static bool start_server(ibmgr_data_t*);
#ifdef NOT_USED_OR_REPLACED
static bool server_is_ok(ibmgr_data_t*);
#endif
static bool server_is_up(ibmgr_data_t*);

void SRVRMGR_cleanup( ibmgr_data_t* data)
{
/**************************************
 *
 *      S R V R M G R _ c l e a n u p
 *
 **************************************
 *
 * Functional description
 *      If we are attached to a service, detaches
 *
 **************************************/
	if (data->attached)
		detach_service(data);
}


USHORT SRVRMGR_exec_line(ibmgr_data_t* data)
{
/**************************************
 *
 *      S R V R M G R _ e x e c _ l i n e
 *
 **************************************
 *
 * Functional description
 *      Executes command line
 *
 **************************************/
	fb_assert(data->attached || data->reattach);

/* If reattach is true and we currently attached, then we
   will detach from service. This is potentially dangerous
   situation, because if shutdown is true (server shutdown 
   was initiated) server will be shutdowned.
   I do not check the shutdown flag here because reattach
   could be true only if shutdown has not been initiated.
*/
	if (data->operation != OP_START)
		if (data->reattach) {
			fb_assert(!data->shutdown);
			if (data->attached)

				/* Attached flag should be NULL after detach_service
				 */
				detach_service(data);
			if (attach_service(data) == false)
				return MSG_ATTFAIL;
			data->reattach = 0;
		}

	switch (data->operation) {
	case OP_START:
		if (start_server(data) == false)
			return MSG_STARTFAIL;
		break;

	case OP_SHUT:
		switch (data->suboperation) {
		case SOP_NONE:
		case SOP_SHUT_NOW:
			data->shutdown = true;
			if (start_shutdown(data) == false) {
				data->shutdown = false;
				return MSG_SSHUTFAIL;
			}
			detach_service(data);
			data->shutdown = false;
			data->reattach |= (REA_HOST | REA_USER | REA_PASSWORD);
			return MSG_SHUTOK;

		case SOP_SHUT_NOAT:
			printf("SHUTDOWN NO ATTACHMENTS\n");
			data->shutdown = true;
			break;

		case SOP_SHUT_NOTR:
			printf("SHUTDOWN NO TRANSACTIONS\n");
			data->shutdown = true;
			break;

		case SOP_SHUT_IGN:
			printf("SHUTDOWN IGNORE\n");
			data->shutdown = false;
			break;
		}
		break;

	case OP_PRINT:
		switch (data->suboperation) {
		case SOP_PRINT_POOL:
			if (print_pool(data) == false)
				return MSG_PRPOOLFAIL;
			return MSG_PRPOOLOK;
		}
		break;

	default:
#ifdef DEV_BUILD
		fprintf(OUTFILE,
				   "ASSERT: file %s line %"LINEFORMAT": unknown operation %d\n",
				   __FILE__, __LINE__, data->operation);
#endif
		;
	}

	return FB_SUCCESS;
}


void SRVRMGR_msg_get( USHORT number, TEXT * msg)
{
/**************************************
 *
 *      S R V R M G R _ m s g _ g e t
 *
 **************************************
 *
 * Functional description
 *      Retrieve a message from the error file
 *
 **************************************/

/* The following line will be the future of this function

gds__msg_format (0, MSG_FAC, number, MSG_LEN, msg,
    NULL, NULL, NULL, NULL, NULL);
*/

	switch (number) {
	case MSG_PROMPT:
		strcpy(msg, "IBMGR>");
		break;
	case MSG_OPSPEC:
		strcpy(msg, "operation already specified");
		break;
	case MSG_NOOPSPEC:
		strcpy(msg, "no operation specified");
		break;
	case MSG_INVSWOP:
		strcpy(msg, "illegal operation/switch combination");
		break;
	case MSG_SHUTDOWN:
		strcpy(msg, "warning: shutdown is in progress");
		break;
	case MSG_CANTCHANGE:
		strcpy(msg, "can not change host, password or user");
		break;
	case MSG_VERSION:
		strcpy(msg, "ibmgr version");
		break;
	case MSG_INVSW:
		strcpy(msg, "invalid switch");
		break;
	case MSG_AMBSW:
		strcpy(msg, "ambiguous switch");
		break;
	case MSG_INVSWSW:
		strcpy(msg, "invalid switch combination");
		break;
	case MSG_WARNPASS:
		strcpy(msg, "warning: only 8 significant bytes of password used");
		break;
	case MSG_INVUSER:
		strcpy(msg, "invalid user (only 32 bytes allowed");
		break;
	case MSG_INVPAR:
		strcpy(msg, "invalid parameter, no switch specified");
		break;
	case MSG_SWNOPAR:
		strcpy(msg, "switch does not take any parameter");
		break;
	case MSG_REQPAR:
		strcpy(msg, "switch requires parameter");
		break;
	case MSG_SYNTAX:
		strcpy(msg, "syntax error in command line");
		break;
	case MSG_GETPWFAIL:
		strcpy(msg, "can not get password entry");
		break;
	case MSG_ATTFAIL:
		strcpy(msg, "can not attach to server");
		break;
	case MSG_CANTSHUT:
		strcpy(msg, "can not start another shutdown");
		break;
	case MSG_SSHUTFAIL:
		strcpy(msg, "can not start server shutdown");
		break;
	case MSG_SHUTOK:
		strcpy(msg, "server shutdown completed");
		break;
	case MSG_CANTQUIT:
		strcpy(msg, "can not quit now, use shut -ign");
		break;
	case MSG_STARTERR:
		strcpy(msg, "check $FIREBIRD/firebird.log file for errors");
		break;
	case MSG_STARTFAIL:
		strcpy(msg, "can not start server");
		break;
	case MSG_SRVUP:
		strcpy(msg, "server is already running");
		break;
	case MSG_SRVUPOK:
		strcpy(msg, "server has been successfully started");
		break;
	case MSG_NOPERM:
		strcpy(msg, "no permissions to perform operation");
		break;
	case MSG_PRPOOLFAIL:
		strcpy(msg, "Failed to print pool info");
		break;
	case MSG_PRPOOLOK:
		strcpy(msg, "Print pool Successfull");
		break;
	case MSG_FLNMTOOLONG:
		strcpy(msg, "File name too long");
		break;
	default:
		strcpy(msg, "can not get an error message");
	}
}


static bool attach_service( ibmgr_data_t* data)
{
/**************************************
 *
 *      a t t a c h _ s e r v i c e
 *
 **************************************
 *
 * Functional description
 *      Attaches to "anonymous" or "query_server"
 *	service depending on thr user name.
 *
 **************************************/
	ISC_STATUS_ARRAY status;
	TEXT svc_name[128];

/* Obviously we should not be already attached to service
*/
	fb_assert(!data->attached);

	strcpy(svc_name, data->host);

	TEXT spb[SPB_BUFLEN];
	TEXT* p = spb;

	if (!strcmp(data->user, SYSDBA_USER_NAME)) {
		*p++ = isc_spb_version1;
		*p++ = isc_spb_user_name;
		*p++ = strlen(SYSDBA_USER_NAME);
		strcpy(p, SYSDBA_USER_NAME);
		p += strlen(p);
		*p++ = isc_spb_password;
		*p++ = strlen(data->password);
		strcpy(p, data->password);

		strcat(svc_name, ":query_server");

#ifdef DEBUG
		fprintf(OUTFILE, "spb: \"%s\"\nsvc_name: \"%s\"\n", spb, svc_name);
#endif

		isc_service_attach(status, 0, svc_name, &data->attached, strlen(spb),
						   spb);
	}
	else {
		strcat(svc_name, ":anonymous");
		isc_service_attach(status, 0, svc_name, &data->attached, 0, "");
	}

	if (status[0] == 1 && status[1] > 0) {
#ifdef DEBUG
		fprintf(OUTFILE, "ERROR: %lu\n", status[1]);
#endif
		fb_assert(status[1] != isc_svcnotdef);
		isc_print_status(status);
		return false;
	}
	return true;
}


static bool detach_service( ibmgr_data_t* data)
{
/**************************************
 *
 *      d e t a c h _ s e r v i c e
 *
 **************************************
 *
 * Functional description
 *      Detaches from service.
 *      If IBMGR_shutdown_start was called
 *      before, shutdowns the server.
 *
 **************************************/
	ISC_STATUS_ARRAY status;

/* We should be attached if we want to detach
*/
	fb_assert(data->attached);

	isc_service_detach(status, &(data->attached));
	data->attached = NULL;

	if (status[0] == 1 && status[1] > 0) {
/* If as a result of detach_service server has been
   shutdowned we will get an error.
   MMM - need to check for that error and return true
*/
#ifdef DEBUG
		fprintf(OUTFILE, "ERROR: %lu\n", status[1]);
#endif
		if (!data->shutdown)
			isc_print_status(status);
		return false;
	}
	return true;
}


static bool start_shutdown( ibmgr_data_t* data)
{
/**************************************
 *
 *      s t a r t _ s h u t d o w n
 *
 **************************************
 *
 * Functional description
 *      Initiate shutdown process
 *
 **************************************/
	ISC_STATUS_ARRAY status;
	char respbuf[2];

/* We should be attached to ask for any service
*/
	fb_assert(data->attached);

	const char sendbuf[] = { isc_info_svc_svr_offline };
	isc_service_query(status, &data->attached, 0, 0, NULL,
					  1, sendbuf, sizeof(respbuf), respbuf);

	if (status[0] == 1 && status[1] > 0) {
		isc_print_status(status);
		return false;
	}
	return true;
}


static bool start_server( ibmgr_data_t* data)
{
/**************************************
 *
 *      s t a r t _ s e r v e r
 *
 **************************************
 *
 * Functional description
 *      start the superserver using guardian process
 *
 **************************************/
	TEXT msg[MSG_LEN];

/* If we are currently attached and host has not been changed,
   server on this host is up and running.
*/
	if (data->attached && !(data->reattach & REA_HOST)) {
		SRVRMGR_msg_get(MSG_SRVUP, msg);
		fprintf(OUTFILE, "%s\n", msg);
		return true;
	}

	if (data->attached) {
		detach_service(data);
		data->reattach |= (REA_HOST | REA_USER | REA_PASSWORD);
	}

	fb_assert(data->reattach);

/* Let's see if server is already running, try to attach to it
*/
	if (server_is_up(data) == true) {
		SRVRMGR_msg_get(MSG_SRVUP, msg);
		fprintf(OUTFILE, "%s\n", msg);
		return true;
	}

/* We failed to attach to service, thus server might not be running
   You know what? We'll try to start it.
*/
	TEXT path[MAXPATHLEN];
	gds__prefix(path, SERVER_GUARDIAN);

	TEXT *argv[4];
	argv[0] = path;
	if (data->suboperation == SOP_START_ONCE)
		argv[1] = "-o";
	else if (data->suboperation == SOP_START_SIGNORE)
		argv[1] = "-s";
	else
		argv[1] = "-f";
	argv[2] = NULL;


#ifdef DEBUG
	printf("Argument list:\n\"%s\"\n\"%s\"\n", argv[0], argv[1]);
#endif

	pid_t pid;
	if (!(pid = vfork())) {
		execv(path, argv);
		_exit(FINI_ERROR);
	}

/* Wait a little bit to let the server start
*/
	sleep(ATTACH_PAUSE);
	for (int retry = ATTACH_RETRY; retry; retry--) {
		sleep(ATTACH_PAUSE);

		/* What we want to do here is to find out if the server has
		   started or not. We do it by trying to attach to the server
		   by calling isc_service_attach (in server_is_up()).
		   But in a local server startup (and this the way it works
		   currently) before calling isc_service_attach, we can check
		   if the child process (ibguard) exited or not. If it did,
		   then the server exited with startup error and there is no
		   need to try to attach to it.
		 */
		int exit_status; // unused after the call below.
		const pid_t ret_value = waitpid(pid, &exit_status, WNOHANG);

		/* waitpid() returns guardian process pid if the server has
		   exited (or killed by a signal), -1 if error happened,
		   0 if an exit status of a child process is unavailable (that
		   means in our case that the server is running).
		 */
		if (ret_value == pid) {
#ifdef DEBUG
			printf("Guardian process %ld terminated\n", pid);
#endif
			break;
		}
#ifdef DEBUG
		else if (ret_value == -1) {
			printf("waitpid returned error, errno = %ld\n", errno);
		}
#endif

#ifdef DEBUG
		printf("Attach retries left: %d\n", retry);
#endif
		if (server_is_up(data) == true) {
			SRVRMGR_msg_get(MSG_SRVUPOK, msg);
			fprintf(OUTFILE, "%s\n", msg);
			return true;
		}
	}
	SRVRMGR_msg_get(MSG_STARTERR, msg);
	fprintf(OUTFILE, "%s\n", msg);
	return false;
}


#ifdef NOT_USED_OR_REPLACED
static bool server_is_ok( ibmgr_data_t* data)
{
/**************************************
 *
 *      s e r v e r _ i s _ o k
 *
 **************************************
 *
 * Functional description
 *      checks if superserver is running and
 *	functional by trying to attach to the
 *	security database.
 *
 **************************************/
	ISC_STATUS_ARRAY status;
	TEXT path[MAXPATHLEN];
	TEXT db_name[MAXPATHLEN];
	isc_db_handle db_handle = 0L;

	strcpy(db_name, data->host);
	gds__prefix(path, USER_INFO_NAME);
	sprintf(db_name, "%s:%s", db_name, path); // May be buffer overrun

	isc_attach_database(status, strlen(db_name), db_name, &db_handle, 0,
						NULL);
	if (status[0] == 1 && status[1] > 0) {
#ifdef DEBUG
		fprintf(OUTFILE, "server_is_ok/isc_attach_database ERROR: %lu\n",
				   status[1]);
#endif
		return false;
	}
	isc_detach_database(status, &db_handle);
	if (status[0] == 1 && status[1] > 0) {
#ifdef DEBUG
		fprintf(OUTFILE, "server_is_ok/isc_detach_database ERROR: %lu\n",
				   status[1]);
#endif
		return false;
	}
	return true;
}
#endif

static bool server_is_up( ibmgr_data_t* data)
{
/**************************************
 *
 *      s e r v e r _ i s _ u p
 *
 **************************************
 *
 * Functional description
 *      checks if superserver is running
 *	trying to attach to "anonymous" service.
 *
 **************************************/
	ISC_STATUS_ARRAY status;
	TEXT svc_name[128];
	isc_svc_handle svc_handle = NULL;

/* Obviously we should not be already attached to service
*/
	fb_assert(!data->attached);

	bool up = true;

/* To find out if we the server is already running we
   will try to attach to it. We are going to use "anonymous"
   service in order not to get an error like wrong user/password
*/
	strcpy(svc_name, data->host);
	strcat(svc_name, ":anonymous");
	isc_service_attach(status, 0, svc_name, &svc_handle, 0, "");

	if (status[0] == 1 && status[1] > 0) {
#ifdef DEBUG
		fprintf(OUTFILE, "server_is_up ERROR: %lu\n", status[1]);
#endif
		fb_assert(status[1] != isc_svcnotdef);

		/* Server can be running but attach could fail for
		   other reasons. For example, attach could return
		   not enough memory error. Let's take care of it.
		 */
		if (status[1] == isc_virmemexh)
			up = true;
		else
			up = false;
	}
	isc_service_detach(status, &svc_handle);
	return up;
}


static bool print_pool( ibmgr_data_t* data)
{
/**************************************
 *
 *      p r i n t _ p o o l
 *
 **************************************
 *
 * Functional description
 *      Make the server print memory pools
 *
 **************************************/

/* We should be attached to ask for any service
*/
	fb_assert(data->attached);

	char sendbuf[512];
	char* sptr = sendbuf;
	const USHORT path_length = strlen(data->print_file);
	*sptr = isc_info_svc_dump_pool_info;
	++sptr;
	STUFF_WORD(sptr, path_length);
	strcpy(sptr, data->print_file);
	sptr += path_length;

	ISC_STATUS_ARRAY status;
	char respbuf[2];
	isc_service_query(status, &data->attached, 0, 0, NULL,
					  sptr - sendbuf, sendbuf, sizeof(respbuf), respbuf);
	if (status[0] == 1 && status[1] > 0) {
		isc_print_status(status);
		return false;
	}
	return true;
}

