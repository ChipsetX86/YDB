/****************************************************************
 *								*
 *	Copyright 2001, 2005 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include <stdarg.h>
#include "gtm_stdio.h"
#include "gtm_string.h"

#include "cmidef.h"
#include "hashtab_mname.h"	/* needed for cmmdef.h */
#include "cmmdef.h"
#include "error.h"
#include "util.h"
#include "util_out_print_vaparm.h"
#include "cmi.h"

#include "fao_parm.h"
#include "gvcmz.h"

#define PROPER(X,status) if (CMI_ERROR(status)) { ((link_info *)(lnk->usr))->neterr = TRUE ; gvcmz_error(X, status);}

static unsigned char outbuff[OUT_BUFF_SIZE];
static unsigned char *outptr;
GBLREF unsigned char *util_outptr;
GBLREF va_list	last_va_list_ptr;

void util_cm_print(clb_struct *lnk, int code, char *message, int flush, ...)
{
	va_list		var;
	int4		status, msglen, i;

	VAR_START(var, flush);

	if (outptr == outbuff)
	{
		*outptr++ = code;
	}
	if (message)
	{
		util_out_print(NULL, RESET);	/* Clear any pending messages */
		util_out_print_vaparm(message, NOFLUSH, var, MAXPOSINT4);
		msglen = (char *)util_outptr - (char *)util_outbuff;
		memcpy(outptr, util_outbuff, msglen);
		outptr += msglen;
	}
	va_end(last_va_list_ptr);
	va_end(var);
	switch (flush)
	{
		case NOFLUSH:
			break;
		case FLUSH  :
			*outptr++ = 0 ;
			lnk->mbf = outbuff ;
			lnk->cbl = outptr - outbuff ;
			lnk->ast = 0 ;
			status = cmi_write(lnk) ;
			PROPER(code, status) ;
			/* Note: fall into reset.. */
		case RESET  :
			outptr = outbuff ;
			break;
		default	    :
			break ;
	}
	return;
}