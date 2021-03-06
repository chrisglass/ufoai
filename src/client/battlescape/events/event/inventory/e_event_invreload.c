/**
 * @file e_event_invreload.c
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
#include "../../../../cgame/cl_game.h"
#include "e_event_invreload.h"

int CL_InvReloadTime (const eventRegister_t *self, struct dbuffer *msg, const int dt)
{
#if 0
	/* let the reload sound play */
	nextTime += 600;
#else
	return cl.time + 600;
#endif
}

/**
 * @sa CL_InvReload
 */
void CL_InvReload (const eventRegister_t *self, struct dbuffer *msg)
{
	invList_t	*ic;
	le_t	*le;
	int		number;
	int		ammo, type, x, y;
	containerIndex_t container;
	equipDef_t *ed;

	NET_ReadFormat(msg, self->formatString, &number, &ammo, &type, &container, &x, &y);

	le = LE_Get(number);
	if (!le)
		return;

	if (le->team != cls.team)
		return;

	assert(container >= 0);
	assert(container < MAX_INVDEFS);
	ic = INVSH_SearchInInventory(&le->i, INVDEF(container), x, y);
	if (!ic)
		return;

	/**
	 * @todo reload sound should depend on the weapon we reload
	 */
	S_LoadAndPlaySample("weapons/reload-pistol", le->origin, SOUND_ATTN_IDLE, SND_VOLUME_WEAPONS);

	/* if the displaced clip had any remaining bullets
	 * store them as loose, unless the removed clip was full */
	ed = GAME_GetEquipmentDefinition();
	if (ed && ic->item.a > 0 && ic->item.a != ic->item.t->ammo) {
		assert(ammo == ic->item.t->ammo);
		ed->numItemsLoose[ic->item.m->idx] += ic->item.a;
		/* Accumulate loose ammo into clips (only accessible post-mission) */
		if (ed->numItemsLoose[ic->item.m->idx] >= ic->item.t->ammo) {
			ed->numItemsLoose[ic->item.m->idx] -= ic->item.t->ammo;
			ed->numItems[ic->item.m->idx]++;
		}
	}

	/* set new ammo */
	ic->item.a = ammo;
	ic->item.m = INVSH_GetItemByIDX(type);
}
