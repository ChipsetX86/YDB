/****************************************************************
 *								*
 * Copyright (c) 2001-2010 Fidelity National Information 	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2020 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef INVOCATION_MODE_H
#define INVOCATION_MODE_H

GBLREF	unsigned int     invocation_mode;
GBLREF	char		*invocation_exe_str;

/* Flags for invocation_mode */
#define MUMPS_COMPILE	(1 << 0) /* compile-only invocation - mumps <file.m> */
#define MUMPS_RUN	(1 << 1) /* mumps -run file */
#define MUMPS_DIRECT	(1 << 2) /* mumps -direct */
#define MUMPS_CALLIN	(1 << 3) /* current environment is created by ydb_ci()/SimpleAPI/SimpleThreadAPI */
#define MUMPS_UTILTRIGR	(1 << 4) /* current environment is created by one of the utilities that needs to run triggers
				  * (the only one currently supported is MUPIP)
				  */

#endif
