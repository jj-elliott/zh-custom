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

// zh-pipeline @feature 2026-11-16 Local avoidance (docs/PLAN.md Phase 7 step 4), movement rules level 7.
//
// The collision rules of steps 1-3 only act once two units already touch, and can only slow, wait or repath, so
// every traffic situation needed its own rule. This layer acts before contact. Each logic frame a ground unit that
// is following a path looks at the units around it, and if carrying on as it is would hit one within the next
// 1.5 s it scores a small fixed set of alternative velocities (the path direction turned 15..90 degrees either way,
// at full or half speed, or stopping) and takes the cheapest: cost = how soon that velocity collides + how far it
// strays from the path direction + how much speed it gives up. The pathfinder still decides where to go; this only
// bends the next few metres. Passing a slower unit, flowing around a stopped firing line and funnelling through a
// gap all fall out of the one rule.
//
// It sits in AIUpdateInterface::doLocomotor between the point the path follower aims at and the Locomotor, and its
// whole output is a replacement aim point and speed for this frame.
//
// SIM CODE. Deterministic by construction: inputs are positions, headings and physics velocities already in the
// lockstep state; the candidate directions are constant rotation pairs (no runtime trig); the neighbour scan takes
// a minimum, which does not depend on iteration order; there is no RNG and no state beyond one remembered side.

#pragma once

#include "Lib/BaseType.h"

class Object;
class AIUpdateInterface;
struct Coord3D;

namespace AISteering
{
	/**
		Adjust this frame's aim point and speed so that obj avoids the units around it.
		goalPos / speed: in = what the path follower wants, out = what to give the Locomotor.
		side: the caller's memory of which way we last turned (-1 left, 0 none, +1 right); keeps a choice stable.
		Returns TRUE if anything was changed.
	*/
	Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, Int& side );
}
