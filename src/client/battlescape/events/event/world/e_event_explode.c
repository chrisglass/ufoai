/**
 * @file e_event_explode.c
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../../../../client.h"
#include "../../../cl_localentity.h"
#include "e_event_explode.h"

/**
 * @note e.g. func_breakable or func_door with health
 * @sa EV_MODEL_EXPLODE
 */
void CL_Explode (const eventRegister_t *self, struct dbuffer *msg)
{
	const int entnum = NET_ReadShort(msg);
	le_t *le = LE_Get(entnum);
	if (!le)
		LE_NotFoundError(entnum);

	le->inuse = qfalse;

	/* Recalc the client routing table because this le (and the inline model) is now gone */
	CL_RecalcRouting(le);
}
