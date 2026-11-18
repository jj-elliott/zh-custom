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

// zh-pipeline @feature 2026-11-17 The drive plan (docs/PLAN.md Phase 7, "The StarCraft 2 model", M4).
//
// A path from the navigation mesh is a string with sharp corners. A unit cannot drive that: a tank turns at a
// limited rate, a wheeled unit cannot turn at all while standing. Everything this project disliked in the old
// movement (overshoot, correction, wobble) was a follower asking the locomotor for a motion it could not produce.
//
// So the string is turned, once, when the path is found, into a line the unit CAN drive exactly, from the unit's
// own numbers:
//   - each corner becomes an arc of the unit's turning circle. The circle is put on the inside of the turn so that
//     the arc passes through the corner point: the unit swings round the corner and never cuts inside the string
//     (the string is already as close to the wall as the unit may be);
//   - the straight parts are the tangents between consecutive circles;
//   - a unit that can turn on the spot (treads, legs) starts by pivoting to face its first straight; one that cannot
//     (wheels) starts on its turning circle, from the heading it has;
//   - where two corners are too close for full-size arcs the arcs are made smaller and taken slower, down to a stop
//     and a pivot;
//   - the speed along it is limited on each arc, and braking for an arc or for the end starts as early as the
//     unit's brakes need.
// A unit then moves along this plan like a train on rails: step() advances a distance and returns the pose. It turns
// only where the plan turns, so every turn has a corner of the path as its cause. Other traffic may only make it
// slower (the speed cap), never push it sideways; a different line means a new plan.
//
// Units: world units, logic frames, radians, single-precision floats. DETERMINISM: + - * / sqrt atan2 sin cos in a
// fixed order, compiled once into the one exe both machines run, as the engine's own locomotor code is.
// Standard library only: tests/navmesh/drivetest.cpp drives random paths with real units' numbers and checks that
// no step ever exceeds the unit's turn rate, acceleration or braking, and that it never turns without a cause.

#pragma once

#include <vector>

namespace NavDrive
{
	struct Pt
	{
		float x, y;
	};

	/// The unit's own numbers (Locomotor), per logic frame.
	struct Kinematics
	{
		float maxSpeed;
		float turnRate;		///< radians per frame at speed
		float accel;
		float brake;
		float turnSpeed;	///< wheels: > 0, the speed at and below which the turning circle has its fixed radius
											///< turnSpeed / turnRate, and the most a corner may be taken at. 0: turns on the spot.
		float cornerSpeed;	///< speed to take corners at where there is room: the arc's radius is this / turnRate
											///< (for wheels never less than their fixed circle)
	};

	enum PrimType { LINE = 0, ARC = 1, PIVOT = 2 };

	struct Prim
	{
		int type;
		float x0, y0, x1, y1;				///< LINE: from, to. PIVOT: x0, y0 is where.
		float cx, cy, r, a0, sweep;	///< ARC: centre, radius, start angle of the radius, signed sweep (+ = counter-clockwise)
		float h0, turn;							///< PIVOT: heading before, signed turn
		float length;								///< distance travelled on it (0 for a pivot)
		float vmax;									///< most speed allowed on it
		int next;										///< the first of the plan's points not yet passed while on this primitive
	};

	struct Plan
	{
		std::vector<Prim> prims;
		float length;
		int smallerArcs;	///< corners whose arc had to be made smaller than the unit's normal one
		int pivots;				///< corners (not counting the start) reduced to stop-and-pivot
		int cheats;				///< wheeled unit given a turn tighter than its turning circle (no room for the real one)
		int brakingRun;		///< 1: made for a rolling unit that could not take the line at its speed; it first runs straight
											///< on, braking, and the line starts where it could have stopped
		std::vector<Pt> points;	///< the points the line was laid through: the path's, less the corners that were dropped
	};

	struct State
	{
		int prim;
		float along;			///< distance along the current primitive (pivot: radians turned)
		float v;					///< speed after the last step
		float moved;			///< distance covered in the last step (less than v when the step ended on a stop)
		float x, y, heading;
		bool done;
	};

	/// pts: start, corners, goal (at least 2 points). heading: the unit's now.
	/// rollingSpeed: the unit's speed now, when a plan is made for a unit already under way. Such a unit does not
	/// stop to pivot: whatever its drive, it starts on the turning circle it has at that speed.
	void build( const std::vector<Pt>& pts, float heading, const Kinematics& kin, Plan& out, float rollingSpeed = 0.0f );

	/// The points of the plan the unit has not passed yet (for making a new plan from where it is).
	void remaining( const Plan& plan, const State& st, std::vector<Pt>& out );

	void start( const Plan& plan, float x, float y, float heading, float speed, State& st );

	/// One logic frame. speedCap: the most speed other traffic allows right now (a large number for none). The unit
	/// slows for it no faster than its brakes can.
	void step( const Plan& plan, const Kinematics& kin, float speedCap, State& st );
}
