/****************************************************************
 *								*
 * Copyright (c) 2018-2019 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include <stdarg.h>

#include "libyottadb_int.h"
#include "gtmci.h"

/* Simple YottaDB wrapper to do a call-in. Does name lookup on each call whereas "ydb_cip" does not
 *
 * Note this routine is nearly identical to gtm_ci() so any changes in either should be changed in the other. This is done by
 * copying this small routine (which calls a larger common routine) because a simple wrapper doesn't work for a variadic call
 * like this one.
 */
int ydb_ci(const char *c_rtn_name, ...)
{
	va_list		var;
	int		retval;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	VERIFY_NON_THREADED_API;	/* clears a global variable "caller_func_is_stapi" set by SimpleThreadAPI caller
					 * so needs to be first invocation after SETUP_THREADGBL_ACCESS to avoid any error
					 * scenarios from not resetting this global variable even though this function returns.
					 */
	LIBYOTTADB_INIT_NOSIMPLEAPINEST_CHECK((int));	/* Avoid SIMPLEAPINEST error check */
	/* "ydb_ci_exec" already sets up a condition handler "gtmci_ch" so we do not do an
	 * ESTABLISH_RET of ydb_simpleapi_ch here like is done for other SimpleAPI function calls.
	 */
	VAR_START(var, c_rtn_name);
	/* Note: "va_end(var)" done inside "ydb_ci_exec" */
	retval = ydb_ci_exec(c_rtn_name, NULL, var, FALSE);
	return retval;
}
