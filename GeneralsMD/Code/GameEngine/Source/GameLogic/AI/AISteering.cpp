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

// zh-pipeline @feature 2026-11-16 Local avoidance, see AISteering.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/AISteering.h"

#include "Common/GameCommon.h"
#include "Common/Geometry.h"
#include "Common/KindOf.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

namespace
{
	// ---- Tuning. Everything that decides how movement feels is here. ----
	const Real HORIZON = 1.5f * LOGICFRAMES_PER_SECOND;	// frames we look ahead.
	const Real CLEARANCE = 1.1f;												// combined radius is padded by 10%.
	const Real STANDING_SPEED = 0.05f;									// world units per frame; slower counts as standing.
	const Int MAX_NEIGHBOURS = 24;

	const Real COST_COLLISION = 10.0f;	// a collision right now; falls off with the square of the time left.
	const Real COST_HALF_SPEED = 1.5f;
	const Real COST_STOP = 3.0f;
	const Real START_COST = 0.6f;				// a threat milder than this (about 1.1 s away) doesn't start a manoeuvre...
	const UnsignedInt STEERING_RECENT = LOGICFRAMES_PER_SECOND/2;	// ...but one under way is seen through.
	const UnsignedInt COMMIT_FRAMES = LOGICFRAMES_PER_SECOND;			// how long a chosen side stays chosen.
	const Real COST_CHANGE_SIDE = 4.0f;	// switching side while committed: only to dodge something imminent.
	const Real BONUS_SAME_SIDE = 0.8f;
	const Real EASE = 0.25f;						// share of the wanted turn applied per frame; the rest is last frame's.
	const UnsignedInt ESCAPE_AFTER = 2*LOGICFRAMES_PER_SECOND;		// held at a standstill this long = jammed.

	// Candidate directions: the path direction rotated by these angles. cos/sin as constants, so no runtime trig.
	struct Turn
	{
		Real cosA, sinA;	// rotation of the path direction; sinA > 0 turns left (counter-clockwise).
		Real cost;				// what straying this far from the path costs.
		Int side;					// -1 left, +1 right, 0 straight.
	};
	// Right before left at each angle: on a dead tie both units of a head-on pair pick right and pass like traffic.
	const Turn TURNS[] =
	{
		{ 1.0f,         0.0f,         0.0f,  0 },
		{ 0.96592583f, -0.25881905f,  0.4f,  1 },	// 15
		{ 0.96592583f,  0.25881905f,  0.4f, -1 },
		{ 0.86602540f, -0.5f,         0.9f,  1 },	// 30
		{ 0.86602540f,  0.5f,         0.9f, -1 },
		{ 0.70710678f, -0.70710678f,  1.5f,  1 },	// 45
		{ 0.70710678f,  0.70710678f,  1.5f, -1 },
		{ 0.5f,        -0.86602540f,  2.2f,  1 },	// 60
		{ 0.5f,         0.86602540f,  2.2f, -1 },
		{ 0.0f,        -1.0f,         3.5f,  1 },	// 90
		{ 0.0f,         1.0f,         3.5f, -1 },
	};
	const Int NUM_TURNS_INFANTRY = 11;	// up to 90 degrees.
	const Int NUM_TURNS_VEHICLE = 9;		// up to 60: a vehicle sent 90 degrees off its path tends to orbit what it is avoiding.

	struct Neighbour
	{
		Real x, y;		// position relative to us.
		Real vx, vy;	// its velocity.
		Real radius;	// combined, padded.
		Bool standing;
	};

	Real steerRadius( const Object *obj )
	{
		const GeometryInfo& geom = obj->getGeometryInfo();
		if (geom.getGeomType() == GEOMETRY_BOX)
			return 0.5f * (geom.getMajorRadius() + geom.getMinorRadius());
		return geom.getMajorRadius();
	}

	inline Bool touching( const Neighbour& n )
	{
		return n.x*n.x + n.y*n.y <= n.radius*n.radius;
	}

	// Frames until we touch n when moving with velocity (vx, vy); HORIZON or more = nothing to avoid. Units that
	// already touch are not avoided at all (dodging something you are inside means "directly away", and as the path
	// direction swings round with the unit it ends up circling); they are queued behind or left to the contact rules.
	Real timeToCollision( const Neighbour& n, Real vx, Real vy )
	{
		Real ux = vx - n.vx;	// our velocity relative to it.
		Real uy = vy - n.vy;
		Real closing = n.x*ux + n.y*uy;	// > 0 = getting closer.
		if (closing <= 0.0f)
			return HORIZON;
		Real c = n.x*n.x + n.y*n.y - n.radius*n.radius;
		if (c <= 0.0f)
			return HORIZON;
		Real a = ux*ux + uy*uy;
		Real disc = closing*closing - a*c;
		if (disc < 0.0f || a <= 0.0f)
			return HORIZON;	// passes clear.
		Real t = (closing - WWMath::Sqrtf(disc)) / a;
		return (t < HORIZON) ? t : HORIZON;
	}

	Real collisionCost( Real t )
	{
		Real urgency = 1.0f - t / HORIZON;
		return COST_COLLISION * urgency * urgency;
	}
}

namespace AISteering
{

//-------------------------------------------------------------------------------------------------
// Look around and decide. Returns FALSE for "carry on as the path says"; otherwise bestTurn (index into TURNS) and
// bestSpeedFactor hold the choice. followCap is lowered to the most we may do behind a moving unit we already
// touch. dirX/dirY is the direction the path follower wants.
static Bool choose( Object *obj, AIUpdateInterface *ai, Real dirX, Real dirY, Real speed, const State& state,
										UnsignedInt now, Int& bestTurn, Real& bestSpeedFactor, Real& followCap )
{
	const Coord3D *pos = obj->getPosition();
	Bool committed = state.side != 0 && (now - state.sideFrame) <= COMMIT_FRAMES;
	Bool alreadySteering = state.offSin != 0.0f || (now - state.sideFrame) <= STEERING_RECENT;

	Bool weAreInfantry = obj->isKindOf(KINDOF_INFANTRY);
	Real ourRadius = steerRadius(obj);
	ObjectID ignoreID = ai->getIgnoredObstacleID();
	const Object *goalObj = ai->getGoalObject();

	bestTurn = 0;
	bestSpeedFactor = 1.0f;

	//
	// Collect the units that matter.
	//
	Neighbour neighbours[MAX_NEIGHBOURS];
	Int count = 0;
	Real range = speed * HORIZON * 2.0f + ourRadius + 4.0f*PATHFIND_CELL_SIZE_F;
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( obj, range, FROM_CENTER_2D, nullptr, ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold(iter);
	for (Object *other = iter->first(); other && count < MAX_NEIGHBOURS; other = iter->next())
	{
		if (other == obj || other == goalObj || other->getID() == ignoreID)
			continue;
		if (other->isEffectivelyDead() || other->getContainedBy() != nullptr)
			continue;
		// Buildings and other fixed things are the pathfinder's business; the path already goes around them.
		if (other->isKindOf(KINDOF_STRUCTURE) || other->isKindOf(KINDOF_IMMOBILE))
			continue;
		AIUpdateInterface *aiOther = other->getAI();
		PhysicsBehavior *otherPhysics = other->getPhysics();
		if (aiOther == nullptr || otherPhysics == nullptr || !aiOther->isDoingGroundMovement())
			continue;
		// Infantry walk through infantry, as they always have. Nothing avoids what it can run over.
		if (weAreInfantry && other->isKindOf(KINDOF_INFANTRY))
			continue;
		if (obj->canCrushOrSquish(other, TEST_CRUSH_OR_SQUISH))
			continue;

		Neighbour& n = neighbours[count];
		n.x = other->getPosition()->x - pos->x;
		n.y = other->getPosition()->y - pos->y;
		const Coord3D *vel = otherPhysics->getVelocity();
		n.vx = vel->x;
		n.vy = vel->y;
		n.standing = (n.vx*n.vx + n.vy*n.vy) < STANDING_SPEED*STANDING_SPEED;
		n.radius = (ourRadius + steerRadius(other)) * CLEARANCE;

		Real ahead = n.x*dirX + n.y*dirY;

		// Queueing: a unit under way that we already touch and that is in front of us sets our pace. A unit with
		// nowhere to go doesn't (slowing never gets us past it; the contact rules deal with it).
		if (ahead > 0.0f && touching(n) && aiOther->isMoving())
		{
			Real pace = n.vx*dirX + n.vy*dirY;	// its speed in our direction.
			if (pace < 0.0f)
				pace = 0.0f;
			if (pace < followCap)
				followCap = pace;
		}

		// A moving unit behind us is its own problem: it sees us ahead and goes around. Without this every column
		// would have its front units dodging the ones catching up.
		if (!n.standing && ahead < 0.0f)
			continue;
		++count;
	}
	if (count == 0)
		return FALSE;

	//
	// Is there anything to avoid? Carrying straight on at full speed must hit something within the horizon.
	//
	Real straightCost = 0.0f;
	Int i;
	for (i = 0; i < count; ++i)
	{
		Real cost = collisionCost(timeToCollision(neighbours[i], dirX*speed, dirY*speed));
		if (cost > straightCost)
			straightCost = cost;
	}
	if (straightCost <= 0.0f)
		return FALSE;
	// A far-off, mild threat doesn't start a manoeuvre (at that range the best answer flips with every small change
	// in the picture); one already under way is seen through.
	if (!alreadySteering && straightCost < START_COST)
		return FALSE;

	//
	// Score the candidates. A standing neighbour is always scored at full speed: slowing down never gets us past
	// something that isn't moving, and if we stopped short of it we would never touch it, so the contact rules
	// (ask an idle ally to step aside, repath around a shooter) would never get their turn.
	//
	LocomotorSurfaceTypeMask surfaces = ai->getLocomotorSet().getValidSurfaces();
	Real probe = 2.0f*PATHFIND_CELL_SIZE_F + ourRadius;	// how far a turned direction must be passable.
	Real bestCost = straightCost;
	Int numTurns = weAreInfantry ? NUM_TURNS_INFANTRY : NUM_TURNS_VEHICLE;
	for (Int t = 0; t < numTurns; ++t)
	{
		const Turn& turn = TURNS[t];
		Real cx = dirX*turn.cosA - dirY*turn.sinA;
		Real cy = dirX*turn.sinA + dirY*turn.cosA;

		Bool passableChecked = (t == 0);	// straight ahead is the path: known good.
		for (Int s = 0; s < 3; ++s)
		{
			// s: 0 full speed, 1 half speed, 2 stop (stop only makes sense straight ahead).
			if (s == 2 && t != 0)
				break;
			Real factor = (s == 0) ? 1.0f : ((s == 1) ? 0.5f : 0.0f);
			Real cost = turn.cost;
			if (s == 1) cost += COST_HALF_SPEED;
			if (s == 2) cost += COST_STOP;
			if (committed && turn.side != 0)
				cost += (turn.side == state.side) ? -BONUS_SAME_SIDE : COST_CHANGE_SIDE;
			if (cost >= bestCost)
				continue;	// cannot win even with no collision at all.

			Real worst = 0.0f;
			for (i = 0; i < count && cost + worst < bestCost; ++i)
			{
				const Neighbour& n = neighbours[i];
				Real v = n.standing ? speed : speed*factor;
				Real c = collisionCost(timeToCollision(n, cx*v, cy*v));
				if (c > worst)
					worst = c;
			}
			cost += worst;
			if (cost >= bestCost)
				continue;

			// Only now pay for the terrain check: never steer a unit off its path into something it can't cross.
			if (!passableChecked)
			{
				Coord3D probeEnd = *pos;
				probeEnd.x += cx*probe;
				probeEnd.y += cy*probe;
				if (!TheAI->pathfinder()->isLinePassable(obj, surfaces, obj->getLayer(), *pos, probeEnd, false, true))
					break;	// this direction is out at any speed.
				passableChecked = TRUE;
			}
			bestCost = cost;
			bestTurn = t;
			bestSpeedFactor = factor;
		}
	}

	// FALSE: nothing beats carrying on.
	return !(bestTurn == 0 && bestSpeedFactor >= 1.0f);
}

//-------------------------------------------------------------------------------------------------
Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, State& state )
{
	UnsignedInt now = TheGameLogic->getFrame();
	const Coord3D *pos = obj->getPosition();
	Real dx = goalPos.x - pos->x;
	Real dy = goalPos.y - pos->y;
	Real aimDist = WWMath::Sqrtf(dx*dx + dy*dy);
	if (aimDist < 1.0f || speed < STANDING_SPEED)
	{
		state.side = 0;
		state.offCos = 1.0f;
		state.offSin = 0.0f;
		state.heldFrames = 0;
		return FALSE;
	}
	Real dirX = dx / aimDist;	// the direction the path follower wants.
	Real dirY = dy / aimDist;

	const Real NO_CAP = 1.0e9f;
	Int bestTurn = 0;
	Real bestSpeedFactor = 1.0f;
	Real followCap = NO_CAP;
	Bool chose = choose(obj, ai, dirX, dirY, speed, state, now, bestTurn, bestSpeedFactor, followCap);

	// Remember the side for COMMIT_FRAMES, and ease the applied turn toward the wanted one instead of jumping to
	// it, both on the way in and on the way back to the path direction.
	Real wantCos = 1.0f;
	Real wantSin = 0.0f;
	if (chose)
	{
		const Turn& chosen = TURNS[bestTurn];
		wantCos = chosen.cosA;
		wantSin = chosen.sinA;
		if (chosen.side != 0)
		{
			state.side = chosen.side;
			state.sideFrame = now;
		}
	}
	else if ((now - state.sideFrame) > COMMIT_FRAMES)
	{
		state.side = 0;
	}

	state.offCos += (wantCos - state.offCos) * EASE;
	state.offSin += (wantSin - state.offSin) * EASE;
	Real len = WWMath::Sqrtf(state.offCos*state.offCos + state.offSin*state.offSin);
	if (len > 0.0001f)
	{
		state.offCos /= len;
		state.offSin /= len;
	}
	else
	{
		state.offCos = wantCos;
		state.offSin = wantSin;
	}
	if (wantSin == 0.0f && WWMath::Fabs(state.offSin) < 0.02f)
	{
		state.offCos = 1.0f;	// close enough to straight: done.
		state.offSin = 0.0f;
	}

	if (state.offSin != 0.0f)
	{
		Real outX = dirX*state.offCos - dirY*state.offSin;
		Real outY = dirX*state.offSin + dirY*state.offCos;
		goalPos.x = pos->x + outX*aimDist;	// same distance away, so the locomotor's braking sums are unchanged.
		goalPos.y = pos->y + outY*aimDist;
	}
	if (chose && bestSpeedFactor < 1.0f)
		speed *= bestSpeedFactor;

	// Queueing, and the escape from a queue that has stopped moving.
	Bool held = FALSE;
	if (followCap < speed)
	{
		speed = followCap;
		held = (followCap < STANDING_SPEED);
	}
	if (held)
	{
		if (++state.heldFrames > ESCAPE_AFTER)
		{
			state.heldFrames = 0;
			return TRUE;
		}
	}
	else
	{
		state.heldFrames = 0;
	}
	return FALSE;
}

}
