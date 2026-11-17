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
	Real timeToCollision( const Neighbour& n, Real vx, Real vy )
	{
		Real ux = vx - n.vx;	// our velocity relative to it.
		Real uy = vy - n.vy;
		Real closing = n.x*ux + n.y*uy;	// > 0 = getting closer.
		if (closing <= 0.0f)
			return HORIZON;
		Real c = n.x*n.x + n.y*n.y - n.radius*n.radius;
		if (c <= 0.0f)
			return 0.0f;	// already inside it and still closing.
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
Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, Int& side )
{
	const Coord3D *pos = obj->getPosition();
	Real dx = goalPos.x - pos->x;
	Real dy = goalPos.y - pos->y;
	Real aimDist = WWMath::Sqrtf(dx*dx + dy*dy);
	if (aimDist < 1.0f || speed < STANDING_SPEED)
	{
		side = 0;
		return FALSE;
	}
	Real dirX = dx / aimDist;	// the direction the path follower wants.
	Real dirY = dy / aimDist;

	const Locomotor *loco = ai->getCurLocomotor();
	Bool treads = (loco != nullptr && loco->getAppearance() == LOCO_TREADS);
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
		if (other->isKindOf(KINDOF_INFANTRY))
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
	{
		side = 0;
		return FALSE;
	}

	//
	// Is there anything to do? Carrying straight on at full speed must hit something within the horizon.
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
	{
		side = 0;
		return FALSE;
	}

	//
	// Score the candidates. A standing neighbour is always scored at full speed: slowing down never gets us past
	// something that isn't moving, and if we stopped short of it we would never touch it, so the contact rules
	// (ask an idle ally to step aside, repath around a shooter) would never get their turn.
	//
	LocomotorSurfaceTypeMask surfaces = ai->getLocomotorSet().getValidSurfaces();
	Real probe = 2.0f*PATHFIND_CELL_SIZE_F + ourRadius;	// how far a turned direction must be passable.
	Real bestCost = straightCost;
	Int bestTurn = 0;
	Real bestSpeedFactor = 1.0f;
	Int numTurns = treads ? NUM_TURNS_TREADS : NUM_TURNS;
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
			if (turn.side != 0 && turn.side == side)
				cost -= BONUS_SAME_SIDE;
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

	if (bestTurn == 0 && bestSpeedFactor >= 1.0f)
	{
		side = 0;
		return FALSE;	// nothing beats carrying on; the contact rules take it from here.
	}

	const Turn& chosen = TURNS[bestTurn];
	side = chosen.side;
	Real outX = dirX*chosen.cosA - dirY*chosen.sinA;
	Real outY = dirX*chosen.sinA + dirY*chosen.cosA;
	goalPos.x = pos->x + outX*aimDist;	// same distance away, so the locomotor's braking sums are unchanged.
	goalPos.y = pos->y + outY*aimDist;
	speed *= bestSpeedFactor;
	return TRUE;
}

}
