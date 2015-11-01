/****************************************************************
 *								*
 *	Copyright 2001, 2004 Sanchez Computer Associates, Inc.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "error.h"

error_def(ERR_ASSERT);
error_def(ERR_GTMCHECK);
error_def(ERR_GTMASSERT);
error_def(ERR_STACKOFLOW);
error_def(ERR_OUTOFSPACE);

CONDITION_HANDLER(fntext_ch)
{
	START_CH;
	if (!DUMPABLE)
	{
		UNWIND(NULL, NULL);	/* As per the standard, $TEXT returns null string if there are errors while		*/
					/* loading/linking with the entryref. So, we ignore non-fatal errors.			*/
	} else
	{
		NEXTCH;			/* But, we don't want to ignore fatal errors as these may be indicative of serious	*/
					/* issues that may need investigation.							*/
	}
}