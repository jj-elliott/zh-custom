/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 zh-pipeline
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// zh-pipeline @feature 2026-11-17 The game's navigation mesh (docs/PLAN.md Phase 7, "The StarCraft 2 model").
//
// The mesh is built from the pathfinder's ground cells by NavBuild (GameLogic/NavMeshBuild.h, which has the method
// and the determinism argument). This is the engine side: which cell gets which label, and when the mesh is rebuilt.
//
// State: built at map load and kept up to date, only on request ("-movetune navmesh.build=1"); nothing reads it
// yet, so it is not sim state. From M3 on searches run on it and it becomes sim state: derived, like the pathfinder's grid,
// from the map and the objects on it, so it is rebuilt after a load rather than saved, and is not in the CRC.

#pragma once

#include "GameLogic/NavMeshBuild.h"

class Pathfinder;

namespace NavMesh
{
	/// What a triangle is, for deciding who may cross it. Several pathfinder cell types share a label where no
	/// ground unit can tell them apart.
	enum Label
	{
		NAV_CLEAR = 0,
		NAV_WATER = 1,
		NAV_CLIFF = 2,
		NAV_RUBBLE = 3,
		NAV_BLOCKED = 4,	///< structure, impassable terrain, under a low bridge, off the map
		NAV_FENCE = 5			///< a structure that crushers drive through
	};

	/// Called by Pathfinder::newMap() once the grid is classified.
	void rebuild( Pathfinder *pathfinder );

	/// Called by the pathfinder wherever it changes cell types (structure and fence footprints, a full
	/// reclassification, a bridge destroyed or repaired). Cheap: the work happens in update().
	void markDirty();

	/// Called by the pathfinder once a logic frame before it searches: rebuilds the mesh if it was marked dirty and
	/// a label really changed. A whole map takes a few milliseconds, so there is no partial rebuild.
	void update( Pathfinder *pathfinder );

	const NavBuild::Mesh& mesh();
}
