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

// zh-pipeline @feature 2026-11-16 Unit movement rules switch (docs/PLAN.md Phase 7).
//
// Two settings: RETAIL (0), the untouched 2003 behaviour, and NAVMESH (2, the default), our movement system
// (docs/PLAN.md Phase 7, "The StarCraft 2 model"):
//   where to go   a navigation mesh per unit size, A* over its triangles and the funnel (GameLogic/NavMesh.h)
//   how to go     per-tick velocity steering within the unit's own turn rate, acceleration and brakes; pushing,
//                 right of way by rank and weight, settling at the destination (GameLogic/AIDrive.h)
// At NAVMESH none of retail's contact rules run for ground units. Level 1 (UNIFIED: a traffic map, AISteering and
// group lanes on the grid pathfinder) was the first attempt; it is deleted, and asking for 1 gives 2. unified() is
// kept as a name for "not retail" where a branch written for level 1 still serves level 2.
//
// RETAIL stays because the pre-Phase-7 replay baseline (tests/replays/skirmish-01.rep) proves, by still reproducing
// at level 0, that the switch leaks nothing, and because it is the comparison when judging feel.
//
// THIS IS SIM STATE. Both machines of a network game must run the same setting or they desync. "-moverules 0"
// selects retail and exists for replay verification and comparison. It is fixed for the life of the process and is
// not part of a save game or a replay (scripts/verify.ps1 reads it from "<replay>.rep.args").

#pragma once

#include "Lib/BaseType.h"

class AIGroup;
struct Coord3D;

namespace MoveRules
{
	enum Level CPP_11(: Int)
	{
		RETAIL = 0,		///< unchanged behaviour: reproduces replays recorded before Phase 7
		UNIFIED = 1,	///< (deleted; selecting it gives NAVMESH)
		NAVMESH = 2,	///< navigation mesh paths + per-tick steering (GameLogic/NavMesh.h, GameLogic/AIDrive.h)

		LATEST = NAVMESH,	///< the default (since 2026-11-22; level 1 can no longer be selected: asking for it gives 2)
		HIGHEST = NAVMESH	///< the most "-moverules" accepts
	};

	Int getLevel();
	void setLevel( Int level );	///< clamped to [RETAIL, HIGHEST]; call before a game starts, never during one

	inline Bool unified() { return getLevel() >= UNIFIED; }
	inline Bool navmesh() { return getLevel() >= NAVMESH; }

	// ---- Movement benchmark plumbing (GameLogic/MoveBench.h lives in the Zero Hour tree; this is the shared part). ----
	/// Called by the logic for every move / attack-move order given to a group, if set. (group, destination, attackMove)
	typedef void (*MoveOrderHook)( AIGroup *group, const Coord3D *dest, Bool attackMove );
	extern MoveOrderHook moveOrderHook;
	void setBenchRecordFile( const char *path );	///< "-movebenchrecord <file>"
	void setBenchRunFile( const char *path );			///< "-movebench <file>"
	const char *benchRecordFile();								///< "" if not given
	const char *benchRunFile();
}

// Tuning constants of the unified movement system, overridable per run: "-movetune name=value,name=value".
// Each constant keeps its default in the source file that uses it and registers itself by name on first use; an
// override given on the command line wins. THIS IS SIM STATE like the rules level: for benchmark runs and
// experiments on one machine, never for a network game. Names: see the loadTuning() functions in AISteering.cpp and
// MoveTraffic.cpp, or run with "-movetune list".
namespace MoveTune
{
	void parse( const char *spec );						///< "a=1,b=2.5"; "list" makes apply() print every name and value it sees
	void apply( const char *name, Real& value );		///< value = the override for name, if one was given
	void apply( const char *name, Int& value );
	void apply( const char *name, UnsignedInt& value );
}
