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

#include "gtm_string.h"
#include "gdsroot.h"
#include "gdskill.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "collseq.h"
#include "gdsfhead.h"
#include "gdscc.h"
#include "copy.h"
#include "filestruct.h"
#include "jnl.h"
#include "buddy_list.h"		/* needed for tp.h */
#include "hashtab_mname.h"
#include "hashtab_int4.h"	/* needed for tp.h */
#include "tp.h"
#include "change_reg.h"
#include "targ_alloc.h"
#include "gvcst_protos.h"	/* for gvcst_root_search prototype */
#include "min_max.h"
#include "hashtab.h"

GBLREF gv_namehead	*gv_target;
GBLREF short            dollar_trestart;
GBLREF gv_key		*gv_currkey;
GBLREF gd_region	*gv_cur_region;
GBLREF gd_binding	*gd_map, *gd_map_top;

void gv_bind_name(gd_addr *addr, mstr *targ)
{
	gd_binding	*map;
	ht_ent_mname	*tabent;
	mname_entry	 gvent;
	int		res;
	boolean_t	added;

	gd_map = addr->maps;
	gd_map_top = gd_map + addr->n_maps;
	gvent.var_name.addr = targ->addr;
	gvent.var_name.len = MIN(targ->len, MAX_MIDENT_LEN);
	COMPUTE_HASH_MNAME(&gvent);
	if ((NULL != (tabent = lookup_hashtab_mname((hash_table_mname *)addr->tab_ptr, &gvent))) &&
							(NULL != (gv_target = (gv_namehead *)tabent->value)))
	{
		if (!gv_target->gd_reg->open)
		{
			gv_target->clue.end = 0;
			gv_init_reg(gv_target->gd_reg);
		}
		gv_cur_region = gv_target->gd_reg;
		if (dollar_trestart)
			gv_target->clue.end = 0;
	} else
	{
		map = gd_map + 1;	/* get past local locks */
		for (; (res = memcmp(gvent.var_name.addr, &(map->name[0]), gvent.var_name.len)) >= 0; map++)
		{
			assert(map < gd_map_top);
			if (0 == res && 0 != map->name[gvent.var_name.len])
				break;
		}
		if (!map->reg.addr->open)
			gv_init_reg(map->reg.addr);
		gv_cur_region = map->reg.addr;
		if ((dba_cm == gv_cur_region->dyn.addr->acc_meth) || (dba_usr == gv_cur_region->dyn.addr->acc_meth))
		{
			gv_target = malloc(sizeof(gv_namehead) + gvent.var_name.len);
			memset(gv_target, 0, sizeof(gv_namehead) + gvent.var_name.len);
			gv_target->gvname.var_name.addr = (char *)gv_target + sizeof(gv_namehead);
			gv_target->nct = 0;
			gv_target->collseq = NULL;
			memcpy(gv_target->gvname.var_name.addr, gvent.var_name.addr, gvent.var_name.len);
			gv_target->gvname.var_name.len = gvent.var_name.len;
			gv_target->gvname.hash_code = gvent.hash_code;
		} else
		{
			assert(gv_cur_region->max_key_size <= MAX_KEY_SZ);
			gv_target = (gv_namehead *)targ_alloc(gv_cur_region->max_key_size, &gvent);
		}
		gv_target->gd_reg = gv_cur_region;
		if (NULL != tabent)
		{	/* Since the global name was found but gv_target was null and now we created a new gv_target,
			 * the hash table key must point to the newly created gv_target->gvname. */
			tabent->key = gv_target->gvname;
			tabent->value = (char *)gv_target;
		} else
		{
			added = add_hashtab_mname((hash_table_mname *)addr->tab_ptr, &gv_target->gvname, gv_target, &tabent);
			assert(added);
		}
	}
	change_reg();
	memcpy(gv_currkey->base, gvent.var_name.addr, gvent.var_name.len);
	gv_currkey->base[gvent.var_name.len] = 0;
	gvent.var_name.len++;
	gv_currkey->base[gvent.var_name.len] = 0;
	gv_currkey->end = gvent.var_name.len;
	gv_currkey->prev = 0;
	if ((dba_bg == gv_cur_region->dyn.addr->acc_meth) || (dba_mm == gv_cur_region->dyn.addr->acc_meth))
	{
		if ((0 == gv_target->root) || (DIR_ROOT == gv_target->root))
			gvcst_root_search();
	}
	return;
}