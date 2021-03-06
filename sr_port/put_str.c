/****************************************************************
 *								*
 * Copyright (c) 2001-2019 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"

#include "compiler.h"
#include "stringpool.h"

GBLREF spdesc stringpool;

oprtype put_str(char *pt, mstr_len_t n)
{
	mval p;

	ENSURE_STP_FREE_SPACE(n);
	memcpy(stringpool.free, pt, n);
	memset(&p, 0, SIZEOF(mval));
	p.mvtype = MV_STR;
	p.str.len = n;
	p.str.addr = (char *) stringpool.free;
	stringpool.free += n;
	return put_lit(&p);
}
