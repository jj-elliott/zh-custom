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

// zh-pipeline @feature 2026-11-16 Local avoidance (docs/PLAN.md Phase 7), part of MoveRules::UNIFIED.
//
// The pathfinder decides where a unit goes; this decides how it gets past the units in its way over the next few
// metres. It sits in AIUpdateInterface::doLocomotor between the point the path follower aims at and the Locomotor,
// and its whole output is a replacement aim point and speed for this frame.
//
// Each logic frame, for a ground unit following a path:
//   look     the nearest units within reach of a 1.5 s horizon (not buildings: those are the pathfinder's).
//   avoid    if carrying on would hit one, score the path direction turned 15..90 degrees either way, at full or
//            half speed, or stopping, by how soon it collides + how far it strays + how much speed it gives up, and
//            take the cheapest that is passable. A chosen side is kept for a second and the turn is eased in and
//            out, so the choice does not flicker.
//   queue    a moving unit ahead that we already touch is not dodged but followed: we go no faster than it does
//            in our direction. This is what makes a gap pass units at the rate they fit, without overlap.
//   escape   if following has held us at a standstill for 2 s, the queue is a jam; the caller then lets this one
//            unit ignore collisions for 2 s. The only place units are allowed through each other.
//   separate an allied vehicle under way that we are inside (closer than 90% of the combined radius) is not a
//            collision course, so "avoid" cannot see it: we ease our own position away from it, a little a frame.
//            Units heading our way at about our speed are left to this and to "queue", and are not avoided.
// The choice has memory (last candidate cheaper, a turn held a few frames after it was last needed, and within one
// manoeuvre the turn only grows), so a manoeuvre is out, hold, back.
//
// SIM CODE. Deterministic by construction: inputs are positions, headings and physics velocities already in the
// lockstep state; candidate directions are constant rotation pairs (no runtime trig); the neighbour scan takes a
// maximum, independent of iteration order (the separation push is a sum, taken in the partition manager's
// near-to-far order, which is itself a function of lockstep state); no RNG; State is per unit, transient, and identical on both machines.

#pragma once

#include "Lib/BaseType.h"

class Object;
class AIUpdateInterface;
struct Coord3D;

namespace AISteering
{
	/// What one unit remembers between frames. Transient: not saved, not in the CRC stream.
	struct State
	{
		Int side;									///< which way we last turned: -1 left, 0 none, +1 right.
		UnsignedInt sideFrame;		///< frame of that choice; we stay committed to the side for a while.
		Real offCos, offSin;			///< the turn currently applied, as a rotation of the path direction; eased frame to frame.
		UnsignedInt heldFrames;		///< consecutive frames queueing has kept us at a standstill.
		Int lastTurn;							///< the candidate chosen last frame (index of the turn, -1 none) and its speed step:
		Int lastSpeedStep;				///< staying with it is cheaper than changing, so the choice does not flicker.
		UnsignedInt lastChoiceFrame;
		UnsignedInt activeFrame;	///< last frame the aim point was bent or the speed cut. Read by MoveBench only.

		State() : side(0), sideFrame(0), offCos(1.0f), offSin(0.0f), heldFrames(0), lastTurn(0), lastSpeedStep(0), lastChoiceFrame(0), activeFrame(0) { }
	};

	/**
		Adjust this frame's aim point and speed so that obj gets past the units around it.
		goalPos / speed: in = what the path follower wants, out = what to give the Locomotor.
		Returns TRUE if the unit has been held at a standstill long enough that the caller should let it escape.
	*/
	Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, State& state );
}
