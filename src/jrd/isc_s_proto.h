/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		isc_s_proto.h
 *	DESCRIPTION:	Prototype header file for isc_sync.cpp
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
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 */

#ifndef JRD_ISC_S_PROTO_H
#define JRD_ISC_S_PROTO_H

#include "../jrd/isc.h"
// If that file wouldn't be included, we do a FW declaration of struct event_t here.

SLONG	ISC_event_clear(event_t*);
void	ISC_event_fini(event_t*);
int		ISC_event_init(event_t*);

int		ISC_event_post(event_t*);
int		ISC_event_wait(event_t*, const SLONG, const SLONG);

typedef void (*FPTR_INIT_GLOBAL_REGION)(void*, struct sh_mem*, bool);
UCHAR*	ISC_map_file(Firebird::Arg::StatusVector&, const TEXT*, FPTR_INIT_GLOBAL_REGION,
					 void*, ULONG, struct sh_mem*);
#if defined(WIN_NT)
int		ISC_mutex_init(struct mtx*, const TEXT*);
#else
int		ISC_mutex_init(struct mtx*);
#endif

int		ISC_mutex_lock(struct mtx*);
int		ISC_mutex_lock_cond(struct mtx*);
int		ISC_mutex_unlock(struct mtx*);
void	ISC_mutex_fini(struct mtx*);

#if defined HAVE_MMAP || defined WIN_NT
UCHAR*	ISC_map_object(Firebird::Arg::StatusVector&, sh_mem*, ULONG, ULONG);
void	ISC_unmap_object(Firebird::Arg::StatusVector&, UCHAR**, ULONG);
#endif

#ifdef UNIX
void	ISC_exception_post(ULONG, const TEXT*);
void	ISC_sync_signals_set(void*);
void	ISC_sync_signals_reset();
#endif

#ifdef WIN_NT
ULONG	ISC_exception_post(ULONG, const TEXT*);
#endif

UCHAR*	ISC_remap_file(Firebird::Arg::StatusVector&, struct sh_mem*, ULONG, bool);
void	ISC_unmap_file(Firebird::Arg::StatusVector&, struct sh_mem*);

void	ISC_remove_map_file(const TEXT* filename);

#endif // JRD_ISC_S_PROTO_H
