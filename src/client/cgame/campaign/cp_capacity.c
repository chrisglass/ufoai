/**
 * @file cp_capacity.c
 */

/*
Copyright (C) 2002-2011 UFO: Alien Invasion.

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

#include "../../client.h" /* cl, cls */
#include "cp_campaign.h"
#include "cp_capacity.h"
#include "../../cl_inventory.h" /* INV_GetEquipmentDefinitionByID */
#include "../../../shared/parse.h"
#include "cp_aircraft.h"
#include "cp_missions.h"
#include "cp_map.h"
#include "cp_popup.h"
#include "cp_ufo.h"

/**
 * @brief Actions to perform when destroying one hangar.
 * @param[in] base Pointer to the base where hangar is destroyed.
 * @param[in] buildingType Type of hangar: B_SMALL_HANGAR for small hangar, B_HANGAR for large hangar
 * @note called when player destroy its building or hangar is destroyed during base attack.
 * @note These actions will be performed after we actually remove the building.
 * @pre we checked before calling this function that all parameters are valid.
 * @pre building is not under construction.
 * @sa B_BuildingDestroy_f
 * @todo If player choose to destroy the building, a popup should ask him if he wants to sell aircraft in it.
 */
void B_RemoveAircraftExceedingCapacity (base_t* base, buildingType_t buildingType)
{
	baseCapacities_t capacity = B_GetCapacityFromBuildingType(buildingType);
	linkedList_t *awayAircraft = NULL;
	int numAwayAircraft;
	int randomNum;
	aircraft_t *aircraft;

	/* destroy aircraft only if there's not enough hangar (hangar is already destroyed) */
	if (B_GetFreeCapacity(base, capacity) >= 0)
		return;

	/* destroy one aircraft (must not be sold: may be destroyed by aliens) */
	AIR_ForeachFromBase(aircraft, base) {
		const int aircraftSize = aircraft->size;

		switch (aircraftSize) {
		case AIRCRAFT_SMALL:
			if (buildingType != B_SMALL_HANGAR)
				continue;
			break;
		case AIRCRAFT_LARGE:
			if (buildingType != B_HANGAR)
				continue;
			break;
		default:
			Com_Error(ERR_DROP, "B_RemoveAircraftExceedingCapacity: Unknown type of aircraft '%i'", aircraftSize);
		}

		/* Only aircraft in hangar will be destroyed by hangar destruction */
		if (!AIR_IsAircraftInBase(aircraft)) {
			if (AIR_IsAircraftOnGeoscape(aircraft))
				LIST_AddPointer(&awayAircraft, (void*)aircraft);
			continue;
		}

		/* Remove aircraft and aircraft items, but do not fire employees */
		AIR_DeleteAircraft(aircraft);
		LIST_Delete(&awayAircraft);
		return;
	}
	numAwayAircraft = LIST_Count(awayAircraft);

	if (!numAwayAircraft)
		return;
	/* All aircraft are away from base, pick up one and change it's homebase */
	randomNum = rand() % numAwayAircraft;
	if (!CL_DisplayHomebasePopup((aircraft_t*)LIST_GetByIdx(awayAircraft, randomNum), qfalse)) {
		aircraft_t *aircraft = (aircraft_t*)LIST_GetByIdx(awayAircraft, randomNum);
		/* No base can hold this aircraft */
		UFO_NotifyPhalanxAircraftRemoved(aircraft);
		if (!MapIsWater(MAP_GetColor(aircraft->pos, MAPTYPE_TERRAIN)))
			CP_SpawnRescueMission(aircraft, NULL);
		else {
			/* Destroy the aircraft and everything onboard - the aircraft pointer
			 * is no longer valid after this point */
			AIR_DestroyAircraft(aircraft);
		}
	}
	LIST_Delete(&awayAircraft);
}

/**
 * @brief Remove exceeding antimatter if an antimatter tank has been destroyed.
 * @param[in] base Pointer to the base.
 */
void B_RemoveAntimatterExceedingCapacity (base_t *base)
{
	const int amount = base->capacities[CAP_ANTIMATTER].cur - base->capacities[CAP_ANTIMATTER].max;
	if (amount <= 0)
		return;

	B_ManageAntimatter(base, amount, qfalse);
}

/**
 * @brief Remove items until everything fits in storage.
 * @note items will be randomly selected for removal.
 * @param[in] base Pointer to the base
 */
void B_RemoveItemsExceedingCapacity (base_t *base)
{
	int i;
	int objIdx[MAX_OBJDEFS];	/**< Will contain idx of items that can be removed */
	int num, cnt;

	if (base->capacities[CAP_ITEMS].cur <= base->capacities[CAP_ITEMS].max)
		return;

	for (i = 0, num = 0; i < csi.numODs; i++) {
		const objDef_t *obj = INVSH_GetItemByIDX(i);

		if (!B_ItemIsStoredInBaseStorage(obj))
			continue;

		/* Don't count item that we don't have in base */
		if (B_ItemInBase(obj, base) <= 0)
			continue;

		objIdx[num++] = i;
	}

	cnt = E_CountHired(base, EMPL_ROBOT);
	/* UGV takes room in storage capacity: we store them with a value MAX_OBJDEFS that can't be used by objIdx */
	for (i = 0; i < cnt; i++) {
		objIdx[num++] = MAX_OBJDEFS;
	}

	while (num && base->capacities[CAP_ITEMS].cur > base->capacities[CAP_ITEMS].max) {
		/* Select the item to remove */
		const int randNumber = rand() % num;
		if (objIdx[randNumber] >= MAX_OBJDEFS) {
			/* A UGV is destroyed: get first one */
			employee_t* employee = E_GetHiredRobot(base, 0);
			/* There should be at least a UGV */
			assert(employee);
			E_DeleteEmployee(employee);
		} else {
			/* items are destroyed. We guess that all items of a given type are stored in the same location
			 *	=> destroy all items of this type */
			const int idx = objIdx[randNumber];
			objDef_t *od = INVSH_GetItemByIDX(idx);
			B_UpdateStorageAndCapacity(base, od, -B_ItemInBase(od, base), qfalse, qfalse);
		}
		REMOVE_ELEM(objIdx, randNumber, num);

		/* Make sure that we don't have an infinite loop */
		if (num <= 0)
			break;
	}
	Com_DPrintf(DEBUG_CLIENT, "B_RemoveItemsExceedingCapacity: Remains %i in storage for a maximum of %i\n",
		base->capacities[CAP_ITEMS].cur, base->capacities[CAP_ITEMS].max);
}

/**
 * @brief Update Storage Capacity.
 * @param[in] base Pointer to the base
 * @sa B_ResetAllStatusAndCapacities_f
 */
void B_UpdateStorageCap (base_t *base)
{
	int i;

	base->capacities[CAP_ITEMS].cur = 0;

	for (i = 0; i < csi.numODs; i++) {
		const objDef_t *obj = INVSH_GetItemByIDX(i);

		if (!B_ItemIsStoredInBaseStorage(obj))
			continue;

		base->capacities[CAP_ITEMS].cur += B_ItemInBase(obj, base) * obj->size;
	}

	/* UGV takes room in storage capacity */
	base->capacities[CAP_ITEMS].cur += UGV_SIZE * E_CountHired(base, EMPL_ROBOT);
}

/**
 * @brief Returns the free capacity of a type
 * @param[in] base Pointer to the base to check
 * @param[in] cap Capacity type
 * @sa baseCapacities_t
 */
int B_GetFreeCapacity (const base_t *base, baseCapacities_t capacityType)
{
	const capacities_t *cap = &base->capacities[capacityType];
	return cap->max - cap->cur;
}
