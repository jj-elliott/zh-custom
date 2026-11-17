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
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/MoveRules.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

namespace
{
	const Real HORIZON = 1.5f * LOGICFRAMES_PER_SECOND;	// frames we look ahead.
	const Real CLEARANCE = 1.1f;												// combined radius is padded by 10%.
	const Real STANDING_SPEED = 0.05f;									// world units per frame; slower counts as standing.
	const Int MAX_NEIGHBOURS = 24;

	const Real COST_COLLISION = 10.0f;	// a collision right now; falls off with the square of the time left.
	const Real COST_HALF_SPEED = 1.5f;
	const Real COST_STOP = 3.0f;
	const Real BONUS_SAME_SIDE = 0.3f;	// keep turning the way we already turn.

	// Level 8 (fifth play-test: units flicked left/right from frame to frame on curves, one spun in circles).
	const Real START_COST = 0.6f;										// don't start steering for a threat milder than this (about 1.1 s away).
	const UnsignedInt STEERING_RECENT = LOGICFRAMES_PER_SECOND/2;	// ... but keep steering once started until clear this long.
	const UnsignedInt COMMIT_FRAMES = LOGICFRAMES_PER_SECOND;			// how long a chosen side stays chosen.
	const Real COST_CHANGE_SIDE = 4.0f;							// switching side while committed: only to dodge something imminent.
	const Real BONUS_SAME_SIDE_STICKY = 0.8f;
	const Real EASE = 0.25f;												// share of the wanted turn applied per frame; the rest is last frame's.
	const Int NUM_TURNS_VEHICLE = 9;								// wheeled and other vehicles: nothing beyond 60 degrees (90 made them orbit).

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
	const Int NUM_TURNS = 11;
	const Int NUM_TURNS_TREADS = 7;	// tracked vehicles lose speed fast when turning: nothing beyond 45 degrees.

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

	// Frames until we touch n when moving with velocity (vx, vy); HORIZON or more = no collision worth caring about.
	Real timeToCollision( const Neighbour& n, Real vx, Real vy, Bool sticky )
	{
		Real ux = vx - n.vx;	// our velocity relative to it.
		Real uy = vy - n.vy;
		Real closing = n.x*ux + n.y*uy;	// > 0 = getting closer.
		if (closing <= 0.0f)
			return HORIZON;
		Real c = n.x*n.x + n.y*n.y - n.radius*n.radius;
		if (c <= 0.0f)
		{
			// Already inside it and still closing. Level 7 scored that as a collision now, which made every direction
			// except "directly away" maximally bad: the unit turned 90 degrees, and as its path direction swung with it,
			// kept turning - the spinning unit of the fifth play-test. Level 8 leaves units that already touch to the
			// contact rules, which exist for exactly that.
			return sticky ? HORIZON : 0.0f;
		}
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
// Decide what to do this frame. Returns FALSE for "carry on as the path says"; otherwise bestTurn (index into TURNS)
// and bestSpeedFactor hold the choice. dirX/dirY is the direction the path follower wants.
static Bool choose( Object *obj, AIUpdateInterface *ai, Real dirX, Real dirY, Real speed, const State& state,
										Bool sticky, UnsignedInt now, Int& bestTurn, Real& bestSpeedFactor )
{
	const Coord3D *pos = obj->getPosition();
	Int side = state.side;
	Bool committed = sticky && side != 0 && (now - state.sideFrame) <= COMMIT_FRAMES;
	Bool alreadySteering = sticky && (state.offSin != 0.0f || (now - state.sideFrame) <= STEERING_RECENT);

	const Locomotor *loco = ai->getCurLocomotor();
	Bool treads = (loco != nullptr && loco->getAppearance() == LOCO_TREADS);
	Bool weAreInfantry = obj->isKindOf(KINDOF_INFANTRY);
	Real ourRadius = steerRadius(obj);
	ObjectID ignoreID = ai->getIgnoredObstacleID();
	const Object *goalObj = ai->getGoalObject();

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
		// As in the retail collision rules: infantry walk through infantry, vehicles don't steer around infantry
		// (allied infantry step aside for them, enemy infantry get run over), and nothing avoids what it can crush.
		// So infantry is never an obstacle here; infantry themselves do steer around vehicles.
		// Level 8: vehicles do steer around infantry they cannot crush. Leaving a mixed group to "vehicle bumps
		// infantry, infantry is told to scatter" was the worst-flowing case of the fifth play-test. Infantry still
		// walk through infantry.
		if (other->isKindOf(KINDOF_INFANTRY) && (!sticky || weAreInfantry))
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

		// A moving unit behind us is its own problem: it sees us ahead and goes around. Without this every column
		// would have its front units dodging the ones catching up.
		if (!n.standing && (n.x*dirX + n.y*dirY) < 0.0f)
			continue;
		++count;
	}
	if (count == 0)
		return FALSE;

	//
	// Is there anything to do? Carrying straight on at full speed must hit something within the horizon.
	//
	Real straightCost = 0.0f;
	Int i;
	for (i = 0; i < count; ++i)
	{
		Real cost = collisionCost(timeToCollision(neighbours[i], dirX*speed, dirY*speed, sticky));
		if (cost > straightCost)
			straightCost = cost;
	}
	if (straightCost <= 0.0f)
		return FALSE;
	// Level 8: a far-off, mild threat doesn't start a manoeuvre (at that range the best answer flips with every small
	// change in the picture); one already under way is seen through.
	if (sticky && !alreadySteering && straightCost < START_COST)
		return FALSE;

	//
	// Score the candidates. A standing neighbour is always scored at full speed: slowing down never gets us past
	// something that isn't moving, and if we stopped short of it we would never touch it, so the contact rules
	// (ask an idle ally to step aside, repath around a shooter) would never get their turn.
	//
	LocomotorSurfaceTypeMask surfaces = ai->getLocomotorSet().getValidSurfaces();
	Real probe = 2.0f*PATHFIND_CELL_SIZE_F + ourRadius;	// how far a turned direction must be passable.
	Real bestCost = straightCost;
	bestTurn = 0;
	bestSpeedFactor = 1.0f;
	Int numTurns = treads ? NUM_TURNS_TREADS : NUM_TURNS;
	if (sticky && !treads && !weAreInfantry)
		numTurns = NUM_TURNS_VEHICLE;
	// Level 9: tracked vehicles now drive through turns, so they get the same +-60 degrees as other vehicles.
	if (MoveRules::groupLanes() && treads)
		numTurns = NUM_TURNS_VEHICLE;
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
			Real cost = turn.cost * (treads ? 1.5f : 1.0f);
			if (s == 1) cost += COST_HALF_SPEED;
			if (s == 2) cost += COST_STOP;
			if (!sticky)
			{
				if (turn.side != 0 && turn.side == side)
					cost -= BONUS_SAME_SIDE;
			}
			else if (committed && turn.side != 0)
			{
				// Level 8: having picked a side we stay on it for a second, unless the other side is the only way out.
				cost += (turn.side == side) ? -BONUS_SAME_SIDE_STICKY : COST_CHANGE_SIDE;
			}
			if (cost >= bestCost)
				continue;	// cannot win even with no collision at all.

			Real worst = 0.0f;
			for (i = 0; i < count && cost + worst < bestCost; ++i)
			{
				const Neighbour& n = neighbours[i];
				Real v = n.standing ? speed : speed*factor;
				Real c = collisionCost(timeToCollision(n, cx*v, cy*v, sticky));
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

	// FALSE: nothing beats carrying on; the contact rules take it from here.
	return !(bestTurn == 0 && bestSpeedFactor >= 1.0f);
}

//-------------------------------------------------------------------------------------------------
Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, State& state )
{
	Bool sticky = MoveRules::steeringSticky();
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
		return FALSE;
	}
	Real dirX = dx / aimDist;	// the direction the path follower wants.
	Real dirY = dy / aimDist;

	Int bestTurn = 0;
	Real bestSpeedFactor = 1.0f;
	Bool chose = choose(obj, ai, dirX, dirY, speed, state, sticky, now, bestTurn, bestSpeedFactor);

	if (!sticky)
	{
		// Level 7: apply the choice as is, forget it the moment there is none.
		if (!chose)
		{
			state.side = 0;
			return FALSE;
		}
		const Turn& chosen = TURNS[bestTurn];
		state.side = chosen.side;
		Real outX = dirX*chosen.cosA - dirY*chosen.sinA;
		Real outY = dirX*chosen.sinA + dirY*chosen.cosA;
		goalPos.x = pos->x + outX*aimDist;	// same distance away, so the locomotor's braking sums are unchanged.
		goalPos.y = pos->y + outY*aimDist;
		speed *= bestSpeedFactor;
		return TRUE;
	}

	// Level 8: remember the side for COMMIT_FRAMES, and ease the applied turn toward the wanted one instead of
	// jumping to it, both on the way in and on the way back to the path direction.
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

	Bool changed = FALSE;
	if (state.offSin != 0.0f)
	{
		Real outX = dirX*state.offCos - dirY*state.offSin;
		Real outY = dirX*state.offSin + dirY*state.offCos;
		goalPos.x = pos->x + outX*aimDist;
		goalPos.y = pos->y + outY*aimDist;
		changed = TRUE;
	}
	if (chose && bestSpeedFactor < 1.0f)
	{
		speed *= bestSpeedFactor;
		changed = TRUE;
	}
	return changed;
}

}
