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

// zh-pipeline @feature 2026-11-17 MoveRules::NAVMESH: a ground unit is moved along a drive plan (docs/PLAN.md
// Phase 7, "M4"; the plan itself is GameLogic/NavDrive.h, which says why).
//
// For a ground unit with a path, at level 2, this replaces what happens between "here is the path" and "the object
// moved": the aim point on the path, AISteering, and the locomotor's move-towards-position. The first frame it
// sees a path it turns the path's corners into a NavDrive plan from the unit's locomotor numbers; every frame after
// it advances the unit one step along the plan and puts the object there: position and heading are set directly
// (the object carries OBJECT_STATUS_BRAKING meanwhile, which is the engine's own way of telling physics not to move
// it), and physics is given the velocity so that everything that reads it (treads, wheels, dust) still sees the
// unit moving.
//
// What other units may do to it, for now: a unit ahead that is going the same way slows it (it follows, at a gap
// that closes with speed). Nothing moves it sideways. Units standing still or coming the other way are not avoided
// yet: it drives through them. That is the next piece of M4, not an oversight.
//
// If the unit is not where its plan says (something else moved it), the plan is dropped and the old follower drives
// the rest of that path.
//
// SIM STATE, level 2 only. Deterministic for the reasons NavDrive.h gives; neighbours come from the partition
// manager sorted near to far. The plan is transient: not saved (after a load the old follower finishes the path).

#pragma once

#include "Lib/BaseType.h"

class AIUpdateInterface;
class Object;

namespace AIDrive
{
	struct Data;	///< one unit's plan and where it is on it; owned by its AIUpdateInterface

	/// One logic frame for a unit whose locomotor goal is "position on path". desiredSpeed: the most the AI wants.
	/// TRUE: the unit was moved along its plan. FALSE: not driven by a plan (the caller uses the old follower).
	Bool update( Object *obj, AIUpdateInterface *ai, Data *& data, Real desiredSpeed );

	/// The unit did not drive this frame (it stopped, it is doing something else): let physics have it back.
	void release( Object *obj, Data *data );

	void destroy( Data *& data );

	/// One line for the benchmark.
	void printStats();
}
