/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "locklits.h"
#include "op.h"

/*
 * -----------------------------------------------
 * Arguments:
 *	timeout	- max. time to wait for locks before giving up
 *
 * Return:
 *	1 - if not timeout specified
 *	if timeout specified:
 *		!= 0 - all the locks int the list obtained, or
 *		0 - blocked
 *	The return result is suited to be placed directly into
 *	the $T variable by the caller if timeout is specified.
 * -----------------------------------------------
 */
int op_incrlock(mval *timeout)
{
	return (op_lock2(timeout, INCREMENTAL));
}

/*
 * -----------------------------------------------
 * See op_incrlock above. This function is exactly the
 * same as op_incrlock except it calls op_lock2_common
 * directly because the timeout value is directly passed
 * to it in nanoseconds rather than seconds.
 */
int op_incrlock_common(uint8 timeout)
{
	return (op_lock2_common(timeout, INCREMENTAL));
}
