/****************************************************************
 *								*
 * Copyright 2001, 2006 Fidelity Information Services, Inc	*
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

/* ionl_rdone.c */

#include "mdef.h"
#include "io.h"

int	ionl_rdone(mint *val, uint8 timeout)
{
	mval	tmp;

	*val = -1;
	return ionl_readfl(&tmp, 1, timeout);
}
