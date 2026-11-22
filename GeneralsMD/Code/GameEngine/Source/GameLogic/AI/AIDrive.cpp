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

// zh-pipeline @feature 2026-11-17 A ground unit moved along its drive plan, see AIDrive.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/AIDrive.h"

#include "Common/GameCommon.h"
#include "Common/Geometry.h"
#include "Common/KindOf.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/MoveRules.h"
#include "GameLogic/NavDrive.h"
#include "GameLogic/NavMesh.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"

#include <stdio.h>

namespace AIDrive
{
	struct Data
	{
		// What the plan was made for: a different path (or the same one changed) means a new plan.
		const Path *path;
		Coord3D pathFirst, pathLast;
		Real shownHeading;	///< the heading the object is given: the plan's, eased (see TURN_EASE)
		Bool allGround;			///< the path never leaves the ground layer (so the mesh can be asked about it)
		// Steering (drive.mode 1): the route's points, which of them is being made for, the velocity being asked
		// for (smoothed) and the speed the unit has.
		std::vector<NavDrive::Pt> route;
		std::vector<unsigned char> routeOnGround;	///< per route point: it is on the ground layer (not a bridge's deck)
		Int nextPoint;
		Real wantX, wantY;
		Real speed;
		Bool arrived;
		Int heldAtGoal;
		Int nearGoalFrames;	///< frames near its place without getting nearer
		Real lastRemaining;
		Int wallFrames;			///< (leaky) frames of standing at a wall with nowhere to slide
		Int stuckFrames;		///< frames (leaky) of being pushed back along its route by something it cannot move
		Real slideSide;			///< which way round it is going while stuck: +1 / -1, 0 = not decided
		Bool abandoned;		///< this path is left to the old follower
		Bool holding;			///< we have set OBJECT_STATUS_BRAKING
	};
}

namespace
{
	// Tuning.
	Int ENABLED = 1;
	Real FOLLOW_HEADWAY = 20.0f;		// frames: a follower's speed is the leader's plus the gap over this
	Real FOLLOW_LANE = 0.9f;				// a unit ahead counts if it is within this share of the two radii off our line
	Real FOLLOW_GAP = 1.05f;				// the gap is measured from this many radii apart
	Real SAME_WAY_COS = 0.3f;				// "going our way": the cosine between its heading and ours is above this
	// The plan's heading changes at the full turn rate the moment an arc begins and stops the moment it ends, which
	// is exact and looks weightless. The object's heading closes this share of its difference from the plan's each
	// frame instead: it leans into a turn over a few frames and settles out of it, and trails the plan's heading by
	// about (turn rate / this) in a steady turn. The line the unit travels is not changed, only how its body faces.
	Real TURN_EASE = 0.5f;
	Int SPREAD = 1;									// a group takes corners abreast (spreadCorners)
	Real SPREAD_SPACING = 1.15f;		// places across a narrow corner are this many unit widths apart
	// At most this many places abreast at a corner, however wide the group (0 = no limit). Measured on city-mixed
	// (3 variants): one file 98.1 s to arrive / 12.7 stalled / overlap 1.3; three abreast 98.4 / 5.5 / 0.6; no limit
	// 102.2 / 7.9 / 2.3 (the outside of a wide wheel is a long way round).
	Real SPREAD_MAX = 3.0f;
	Real SPEED_SCALE = 1.17157288f;	// see kinematicsOf
	Int DEBUG_UNIT = 0;
	Bool s_loaded = false;
	Int s_spread = 0, s_spreadUndone = 0;
	// ---- Steering, the micro tier of the SC2 brief.
	Real W_SEPARATE = 1.4f;					// weights of the parts of the velocity a unit asks for (seek is 1)
	Real W_COHERE = 0.08f;
	Real W_AVOID = 1.2f;
	Real W_SLIDE = 1.2f;						// going round something it is pressed against and cannot push
	Real SEP_RANGE = 1.35f;					// separation acts inside this many (sums of radii)
	Real AVOID_FRAMES = 45.0f;			// how far ahead velocities are projected for avoidance
	Real AVOID_CLEAR = 1.1f;				// ... and how many (sums of radii) a predicted miss must be to need nothing
	Real WANT_SMOOTH = 0.3f;				// share of the newly asked-for velocity taken each frame
	Real PUSH_CONTACT = 0.9f;				// units nearer than this many (sums of radii) are pushed apart
	Real PUSH_MAX = 0.6f;						// most a unit is displaced by pushing in a frame, world units
	Real CORNER_REACH = 8.0f;				// a route point counts as reached this near it (or once passed)
	Int LOOK_EVERY = 2;							// every this many frames: can the point after the next be gone to directly?
	Real TURN_GENTLE = 0.15f;				// heading errors below this (radians) ...
	Real TURN_GENTLE_SHARE = 0.06f;	// ... are closed by this share a frame
	Int s_reroutes = 0;
	Int s_steerFrames = 0, s_wallStops = 0, s_pushes = 0, s_lookAheads = 0, s_arrivedShort = 0;


	// The shapes of the groups most recently ordered to move (AIDrive::registerGroup).
	struct GroupShape
	{
		UnsignedInt key;
		std::vector<Real> offsets;	// x, y per member
	};
	const size_t MAX_SHAPES = 48;
	std::vector<GroupShape> s_shapes;
	UnsignedInt s_nextShapeKey = 1;

	Int s_plans = 0;

	void loadTuning()
	{
		if (s_loaded)
			return;
		s_loaded = true;
		MoveTune::apply("drive.enabled", ENABLED);
		MoveTune::apply("drive.followHeadway", FOLLOW_HEADWAY);
		MoveTune::apply("drive.followLane", FOLLOW_LANE);
		MoveTune::apply("drive.followGap", FOLLOW_GAP);
		MoveTune::apply("drive.turnEase", TURN_EASE);
		MoveTune::apply("drive.speedScale", SPEED_SCALE);
		MoveTune::apply("drive.spread", SPREAD);
		MoveTune::apply("drive.spreadSpacing", SPREAD_SPACING);
		MoveTune::apply("drive.spreadMax", SPREAD_MAX);
		MoveTune::apply("drive.wSeparate", W_SEPARATE);
		MoveTune::apply("drive.wCohere", W_COHERE);
		MoveTune::apply("drive.wAvoid", W_AVOID);
		MoveTune::apply("drive.sepRange", SEP_RANGE);
		MoveTune::apply("drive.avoidFrames", AVOID_FRAMES);
		MoveTune::apply("drive.wantSmooth", WANT_SMOOTH);
		MoveTune::apply("drive.pushMax", PUSH_MAX);
		MoveTune::apply("drive.lookEvery", LOOK_EVERY);
		if (LOOK_EVERY < 1) LOOK_EVERY = 1;
		MoveTune::apply("drive.cornerReach", CORNER_REACH);
		MoveTune::apply("drive.turnGentle", TURN_GENTLE);
		MoveTune::apply("drive.turnGentleShare", TURN_GENTLE_SHARE);
		MoveTune::apply("debug.unit", DEBUG_UNIT);
	}

	Real radiusOf( const Object *obj )
	{
		const GeometryInfo& geom = obj->getGeometryInfo();
		if (geom.getGeomType() == GEOMETRY_BOX)
			return (geom.getMajorRadius() + geom.getMinorRadius()) * 0.5f;
		return geom.getMajorRadius();
	}

	void kinematicsOf( const Object *obj, const Locomotor *loco, NavDrive::Kinematics& kin )
	{
		BodyDamageType damage = obj->getBodyModule()->getDamageState();
		// The speed a unit is listed with is not the speed the retail game moves it at. Retail's locomotors hold
		// the unit's speed to that figure as physics measures it, and that measure (getForwardSpeed2D, an average
		// of two components) reads low by a factor that depends on the heading: 1 along an axis, 1.41 on a
		// diagonal. Units therefore travel faster than listed, by about 17% over all headings; upstream's own
		// constant for it is DiagonalCompensation2D in Locomotor.cpp (2*sqrt(2) / (1 + sqrt(2))). A unit moved here
		// at its listed speed is that much slower than the game people know (the user saw it on a Humvee), so the
		// same factor is applied, the same on every heading.
		kin.maxSpeed = loco->getMaxSpeedForCondition(damage) * SPEED_SCALE;
		kin.turnRate = loco->getMaxTurnRate(damage);
		kin.accel = loco->getMaxAcceleration(damage);
		kin.brake = loco->getBraking();
		if (kin.brake <= 0.0f)
			kin.brake = kin.accel > 0.0f ? kin.accel : 0.05f;
		if (kin.accel <= 0.0f)
			kin.accel = 0.05f;
		kin.turnSpeed = 0.0f;
		kin.cornerSpeed = kin.maxSpeed;
		LocomotorAppearance look = loco->getAppearance();
		if (look == LOCO_WHEELS_FOUR || look == LOCO_MOTORCYCLE)
		{
			// The locomotor's own rule (moveTowardsPositionWheels): the turn is in proportion to speed up to this.
			kin.turnSpeed = loco->getMinTurnSpeed() * SPEED_SCALE;
			if (kin.turnSpeed < kin.maxSpeed * 0.25f)
				kin.turnSpeed = kin.maxSpeed * 0.25f;
			// Above that speed its turning circle grows with speed: it corners wide and fast where there is room.
		}
	}

	/// A group ordered to one place keeps its shape, so on open ground its members' paths are parallel. But every
	/// one of those strings bends round the same wall corners, which would bring the whole group into one file at
	/// each. So each member's corner is moved out from the wall by where the member is across the group: the one
	/// nearest the wall keeps the corner, the others pass outside it in the order they stand, and the group wheels
	/// round the corner abreast. Where the ground has no room for the whole width there are as many places as fit
	/// (a unit's width apart), and members who share a place follow one another: a narrow gap makes a column, a wide
	/// one does not. A moved corner whose straights would leave the unit's ground goes back where it was.
	void spreadCorners( Object *obj, AIUpdateInterface *ai, std::vector<NavDrive::Pt>& pts )
	{
		if (!SPREAD || ai->getGroupRadius() < 1.0f || ai->getGroupKey() == 0 || pts.size() < 3)
			return;
		const GroupShape *shape = nullptr;
		for (size_t g = 0; g < s_shapes.size(); ++g)
		{
			if (s_shapes[g].key == ai->getGroupKey())
				shape = &s_shapes[g];
		}
		if (shape == nullptr)
			return;
		Real offX = ai->getGroupOffsetX(), offY = ai->getGroupOffsetY();
		Real spacing = 2.0f * radiusOf(obj) * SPREAD_SPACING + 1.0f;
		std::vector<NavDrive::Pt> original = pts;
		std::vector<unsigned char> moved(pts.size(), 0);
		size_t i;
		for (i = 1; i + 1 < pts.size(); ++i)
		{
			const NavDrive::Pt& v = original[i];
			Real ix = v.x - original[i-1].x, iy = v.y - original[i-1].y;
			Real ox = original[i+1].x - v.x, oy = original[i+1].y - v.y;
			Real il = sqrt(ix*ix + iy*iy), ol = sqrt(ox*ox + oy*oy);
			if (il < 0.01f || ol < 0.01f)
				continue;
			Real bx = ox/ol - ix/il, by = oy/ol - iy/il;	// towards the inside of the turn: towards the wall
			Real bl = sqrt(bx*bx + by*by);
			if (bl < 0.02f)
				continue;
			bx /= bl; by /= bl;
			// How far this member stands from the wall side of the group, across the turn, and how wide the group
			// is that way. (The member nearest the wall keeps the corner.)
			Real nearest = -1.0e9f, furthest = 1.0e9f;
			for (size_t g = 0; g + 1 < shape->offsets.size(); g += 2)
			{
				Real across = shape->offsets[g] * bx + shape->offsets[g+1] * by;
				if (across > nearest) nearest = across;
				if (across < furthest) furthest = across;
			}
			Real width = nearest - furthest;
			Real want = nearest - (offX * bx + offY * by);
			if (want < 0.0f) want = 0.0f;
			if (want > width) want = width;
			if (want < 1.0f || width < 1.0f)
				continue;
			// How much room there is outward from the corner.
			Real room = 0.0f;
			for (Real d = width; d >= spacing * 0.5f; d *= 0.5f)
			{
				if (NavMesh::segmentOnMesh(obj, ai->getLocomotorSet(), v.x - bx * 1.0f, v.y - by * 1.0f, v.x - bx * d, v.y - by * d))
				{
					room = d;
					break;
				}
			}
			if (SPREAD_MAX > 0.0f && room > SPREAD_MAX * spacing)
				room = SPREAD_MAX * spacing;
			Real out;
			if (room >= width * 0.999f)
				out = want;
			else
			{
				Int places = REAL_TO_INT_FLOOR(room / spacing);
				if (places <= 0)
					continue;
				Int place = REAL_TO_INT_FLOOR(want / width * (Real)places + 0.5f);
				out = (Real)place * spacing;
			}
			if (out < 1.0f)
				continue;
			pts[i].x = v.x - bx * out;
			pts[i].y = v.y - by * out;
			moved[i] = 1;
			++s_spread;
		}
		// The straights between moved corners must still be on the unit's ground.
		for (Int pass = 0; pass < 4; ++pass)
		{
			Bool allClear = TRUE;
			for (i = 0; i + 1 < pts.size(); ++i)
			{
				if (!moved[i] && !moved[i+1])
					continue;
				if (NavMesh::segmentOnMesh(obj, ai->getLocomotorSet(), pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y))
					continue;
				allClear = FALSE;
				if (moved[i]) { pts[i] = original[i]; moved[i] = 0; ++s_spreadUndone; }
				if (moved[i+1]) { pts[i+1] = original[i+1]; moved[i+1] = 0; ++s_spreadUndone; }
			}
			if (allClear)
				break;
		}
	}

}

//-------------------------------------------------------------------------------------------------
UnsignedInt AIDrive::registerGroup( const Real *offsetsXY, Int members )
{
	if (s_shapes.size() >= MAX_SHAPES)
		s_shapes.erase(s_shapes.begin());
	s_shapes.push_back(GroupShape());
	GroupShape& shape = s_shapes.back();
	shape.key = s_nextShapeKey++;
	shape.offsets.assign(offsetsXY, offsetsXY + members * 2);
	return shape.key;
}

//-------------------------------------------------------------------------------------------------
void AIDrive::destroy( Data *& data )
{
	delete data;
	data = nullptr;
}

//-------------------------------------------------------------------------------------------------
void AIDrive::release( Object *obj, Data *data )
{
	if (data == nullptr || !data->holding)
		return;
	data->holding = FALSE;
	obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
	// Whatever is driving the unit now starts from rest, as the old follower would after stopping.
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics)
		physics->scrubVelocity2D(0.0f);
}

namespace
{
	inline Real wrapAngle( Real a )
	{
		while (a > PI) a -= 2.0f * PI;
		while (a <= -PI) a += 2.0f * PI;
		return a;
	}

	/// May 'who' be moved to (x, y) without being put on top of some other ground unit? (Asked before a moving unit
	/// pushes an idle one: a parked unit shoved by a late arrival must not end up on its parked neighbour.)
	Bool roomFor( Object *who, Real x, Real y, const Object *pusher )
	{
		Real radius = radiusOf(who);
		Bool infantry = who->isKindOf(KINDOF_INFANTRY);
		Coord3D at;
		at.x = x; at.y = y; at.z = who->getPosition()->z;
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &at, radius * 2.0f + 3.0f * PATHFIND_CELL_SIZE_F, FROM_CENTER_2D, nullptr, ITER_SORTED_NEAR_TO_FAR );
		MemoryPoolObjectHolder hold(iter);
		for (Object *other = iter->first(); other; other = iter->next())
		{
			if (other == who || other == pusher || other->isEffectivelyDead() || other->getContainedBy() != nullptr)
				continue;
			if (other->isKindOf(KINDOF_STRUCTURE) || other->isKindOf(KINDOF_IMMOBILE))
				continue;
			AIUpdateInterface *aiOther = other->getAI();
			if (aiOther == nullptr || !aiOther->isDoingGroundMovement() || other->isAboveTerrain())
				continue;
			if (other->isKindOf(KINDOF_INFANTRY) != infantry)
				continue;	// vehicles and infantry are not kept off each other when standing
			Real dx = other->getPosition()->x - x, dy = other->getPosition()->y - y;
			Real both = radius + radiusOf(other);
			if (dx*dx + dy*dy < sqr(both * PUSH_CONTACT))
			{
				// already that close where it stands? Then moving it must at least not make it worse.
				Real ox = other->getPosition()->x - who->getPosition()->x, oy = other->getPosition()->y - who->getPosition()->y;
				if (dx*dx + dy*dy < ox*ox + oy*oy)
					return FALSE;
			}
		}
		return TRUE;
	}

	/// Put the object where it has been moved to, facing as it has turned, and tell physics (see AIDrive.h).
	void applyPose( Object *obj, PhysicsBehavior *physics, AIDrive::Data *data, Real x, Real y, Real heading, Real moved, Real turned )
	{
		Coord3D to;
		to.x = x;
		to.y = y;
		to.z = TheTerrainLogic->getLayerHeight(x, y, obj->getLayer());
		obj->setPosition(&to);
		obj->setOrientation(heading);
		obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
		data->holding = TRUE;
		Coord3D none;
		none.zero();
		physics->applyMotiveForce(&none);
		Coord3D delta;
		const Coord3D *vel = physics->getVelocity();
		delta.x = Cos(heading) * moved - vel->x;
		delta.y = Sin(heading) * moved - vel->y;
		delta.z = 0.0f;
		physics->addVelocityTo(&delta);
		physics->scrubVelocity2D(moved * 0.9999f);
		physics->setTurning(turned > 0.002f ? TURN_POSITIVE : (turned < -0.002f ? TURN_NEGATIVE : TURN_NONE));
	}

	/// The micro tier as the SC2 brief has it. Every frame the unit asks for a velocity that is a blend of
	///   seek        towards the next point of its route (the pulled string from the navigation mesh),
	///   separation  away from units nearer than a little more than touching,
	///   cohesion    (slight) towards the units of its own group around it,
	///   avoidance   sideways out of the way of any unit whose velocity, projected a second and a half ahead,
	///               brings it too close,
	/// and then moves the way this engine's units can: it turns towards that velocity no faster than its turn rate
	/// (a wheeled unit: in proportion to its speed), slows while it is facing away from it (tracked units and
	/// infantry down to a pivot), and changes speed no faster than its acceleration and brakes. Those limits are the
	/// one place this differs from SC2, whose units have none to speak of.
	/// Units that touch are pushed apart: two moving units share it, a moving unit pushes an idle ally out of its
	/// way. No unit's centre leaves the ground of its size (its mesh).
	Bool steer( Object *obj, AIUpdateInterface *ai, AIDrive::Data *& data, Path *path, const NavDrive::Kinematics& kin,
							Real desiredSpeed, PhysicsBehavior *physics )
	{
		const Coord3D *first = path->getFirstNode()->getPosition();
		const Coord3D *last = path->getLastNode()->getPosition();
		const Coord3D *pos = obj->getPosition();
		Bool samePath = data != nullptr && data->path == path
			&& data->pathFirst.x == first->x && data->pathFirst.y == first->y
			&& data->pathLast.x == last->x && data->pathLast.y == last->y;
		if (!samePath)
		{
			if (data == nullptr)
			{
				data = new AIDrive::Data;
				data->holding = FALSE;
			}
			data->path = path;
			data->pathFirst = *first;
			data->pathLast = *last;
			data->route.clear();
			data->routeOnGround.clear();
			NavDrive::Pt pt;
			pt.x = pos->x; pt.y = pos->y;
			data->route.push_back(pt);
			data->routeOnGround.push_back(obj->getLayer() == LAYER_GROUND ? 1 : 0);
			data->allGround = obj->getLayer() == LAYER_GROUND;
			Int guard = 4096;
			for (PathNode *node = path->getFirstNode()->getNextOptimized(); node != nullptr && --guard > 0; node = node->getNextOptimized())
			{
				pt.x = node->getPosition()->x;
				pt.y = node->getPosition()->y;
				data->route.push_back(pt);
				data->routeOnGround.push_back(node->getLayer() == LAYER_GROUND ? 1 : 0);
				if (node->getLayer() != LAYER_GROUND)
					data->allGround = FALSE;
			}
			// A group's members do not all make for the same corner point: each one's is moved out from the wall by
			// where it stands across the group, as far as the ground has room (spreadCorners). What they seek is then
			// a band round the corner, not a point everyone has to squeeze past.
			// (On a route with a bridge too: a corner on or beside the deck finds no room on the ground mesh and
			// stays where it is.)
			spreadCorners(obj, ai, data->route);
			data->nextPoint = 1;
			data->wantX = 0.0f;
			data->wantY = 0.0f;
			Real rolling = physics->getForwardSpeed2D();
			data->speed = rolling < 0.0f ? 0.0f : (rolling > kin.maxSpeed ? kin.maxSpeed : rolling);
			data->arrived = FALSE;
			data->heldAtGoal = 0;
			data->stuckFrames = 0;
			data->wallFrames = 0;
			data->nearGoalFrames = 0;
			data->lastRemaining = 1.0e9f;
			data->slideSide = 0.0f;
			data->abandoned = data->route.size() < 2;
			data->shownHeading = obj->getOrientation();
			++s_plans;
		}
		if (data->abandoned || data->arrived)
			return FALSE;

		UnsignedInt now = TheGameLogic->getFrame();
		const std::vector<NavDrive::Pt>& route = data->route;
		Int lastPoint = (Int)route.size() - 1;
		Real x = pos->x, y = pos->y;
		Real heading = obj->getOrientation();
		Real hx = Cos(heading), hy = Sin(heading);
		Real ourRadius = radiusOf(obj);
		++s_steerFrames;

		// ---- Which point of the route it is making for: the next one not yet reached or passed; and, every few
		// frames, the one after if it can be gone to directly (a unit pushed off its string re-pulls it).
		while (data->nextPoint < lastPoint)
		{
			const NavDrive::Pt& c = route[data->nextPoint];
			const NavDrive::Pt& b = route[data->nextPoint - 1];
			Real dx = x - c.x, dy = y - c.y;
			Real reach = CORNER_REACH > ourRadius ? CORNER_REACH : ourRadius;
			Bool reached = dx*dx + dy*dy < reach*reach;
			Bool passed = !reached && dx * (c.x - b.x) + dy * (c.y - b.y) > 0.0f;
			// Near a corner, or level with it (pushed wide, or slid along a wall past it), counts as done with it
			// only if the point after can be gone to from here; right on the corner always does. Without that a
			// unit a few units short of the mouth of an alley turned up it early, into the wall beside the mouth,
			// and one that had slid past the end of a wall made for the next point straight through it: for ever.
			if ((reached || passed) && dx*dx + dy*dy > 1.5f * 1.5f
					&& obj->getLayer() == LAYER_GROUND && data->routeOnGround[data->nextPoint] && data->routeOnGround[data->nextPoint + 1])
			{
				const NavDrive::Pt& after = route[data->nextPoint + 1];
				if (!NavMesh::segmentOnMesh(obj, ai->getLocomotorSet(), x, y, after.x, after.y))
					reached = passed = FALSE;
			}
			if (reached || passed)
				++data->nextPoint;
			else
				break;
		}
		// The mesh is asked about wherever the unit and the stretch it is on are on the ground; on a bridge's deck
		// (and on the ground at its ends, which belongs to the bridge's own sheet) the route is simply followed.
		Bool onGround = obj->getLayer() == LAYER_GROUND && data->routeOnGround[data->nextPoint] && data->routeOnGround[data->nextPoint - 1];
		if (onGround && data->nextPoint < lastPoint && data->routeOnGround[data->nextPoint + 1]
				&& ((now + (UnsignedInt)obj->getID()) % (UnsignedInt)LOOK_EVERY) == 0)
		{
			const NavDrive::Pt& after = route[data->nextPoint + 1];
			if (NavMesh::segmentOnMesh(obj, ai->getLocomotorSet(), x, y, after.x, after.y))
			{
				++data->nextPoint;
				++s_lookAheads;
			}
		}
		const NavDrive::Pt& target = route[data->nextPoint];
		Real toX = target.x - x, toY = target.y - y;
		(void)onGround;
		Real toDist = sqrt(toX*toX + toY*toY);
		Real remaining = toDist;
		for (Int i = data->nextPoint; i < lastPoint; ++i)
			remaining += sqrt(sqr(route[i+1].x - route[i].x) + sqr(route[i+1].y - route[i].y));

		// ---- Arrived?
		// (Not onto a place that was found taken last frame: that unit stops beside it, below.)
		if (data->nextPoint == lastPoint && data->heldAtGoal == 0 && toDist <= (data->speed > 1.0f ? data->speed : 1.0f))
		{
			applyPose(obj, physics, data, target.x, target.y, heading, toDist, 0.0f);
			data->arrived = TRUE;
			AIDrive::release(obj, data);
			return TRUE;
		}

		// ---- The velocity it asks for, in shares of its top speed.
		Real seekX = toDist > 0.001f ? toX / toDist : hx, seekY = toDist > 0.001f ? toY / toDist : hy;
		Real sepX = 0.0f, sepY = 0.0f, avoidX = 0.0f, avoidY = 0.0f;
		Real cohX = 0.0f, cohY = 0.0f;
		Int cohCount = 0;
		Real pushX = 0.0f, pushY = 0.0f;
		Real followMost = 1.0e9f;
		Bool goalTaken = FALSE;
		Bool touchingStanding = FALSE;
		Bool weAreInfantry = obj->isKindOf(KINDOF_INFANTRY);
		Real myVx = hx * data->speed, myVy = hy * data->speed;
		{
			Real range = 2.0f * ourRadius + 2.0f * kin.maxSpeed * AVOID_FRAMES + 2.0f * PATHFIND_CELL_SIZE_F;
			if (range > 30.0f * PATHFIND_CELL_SIZE_F)
				range = 30.0f * PATHFIND_CELL_SIZE_F;
			SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( obj, range, FROM_CENTER_2D, nullptr, ITER_SORTED_NEAR_TO_FAR );
			MemoryPoolObjectHolder hold(iter);
			Int count = 0;
			for (Object *other = iter->first(); other && count < 24; other = iter->next())
			{
				if (other == obj || other->isEffectivelyDead() || other->getContainedBy() != nullptr)
					continue;
				if (other->isKindOf(KINDOF_STRUCTURE) || other->isKindOf(KINDOF_IMMOBILE))
					continue;
				AIUpdateInterface *aiOther = other->getAI();
				PhysicsBehavior *otherPhysics = other->getPhysics();
				if (aiOther == nullptr || otherPhysics == nullptr || !aiOther->isDoingGroundMovement() || other->isAboveTerrain())
					continue;
				Bool otherInfantry = other->isKindOf(KINDOF_INFANTRY);
				// A vehicle does not make way for infantry (it pushes its own aside and runs the enemy's over).
				if (otherInfantry && !weAreInfantry)
				{
					if (obj->getRelationship(other) != ENEMIES)
					{
						const Coord3D *ip = other->getPosition();
						Real ix = ip->x - x, iy = ip->y - y;
						Real id = sqrt(ix*ix + iy*iy);
						Real both = ourRadius + radiusOf(other);
						if (id < both * PUSH_CONTACT && id > 0.01f && ix * hx + iy * hy > 0.0f)
						{
							Real by = both * PUSH_CONTACT - id;
							if (by > PUSH_MAX) by = PUSH_MAX;
							Coord3D moved = *ip;
							// aside, not ahead: to whichever side of our line it already is
							Real side = (ix * hy - iy * hx) >= 0.0f ? 1.0f : -1.0f;
							moved.x += hy * side * by;
							moved.y += -hx * side * by;
							if (NavMesh::onMesh(other, aiOther->getLocomotorSet(), moved.x, moved.y) && roomFor(other, moved.x, moved.y, obj))
							{
								other->setPosition(&moved);
								++s_pushes;
							}
						}
					}
					continue;
				}
				if (obj->canCrushOrSquish(other))
					continue;
				++count;
				const Coord3D *op = other->getPosition();
				Real rx = op->x - x, ry = op->y - y;
				Real dist = sqrt(rx*rx + ry*ry);
				Real both = ourRadius + radiusOf(other);
				const Coord3D *ov = otherPhysics->getVelocity();
				Real otherSpeed = sqrt(ov->x * ov->x + ov->y * ov->y);
				Bool otherIdle = otherSpeed < 0.05f && aiOther->isIdle();
				Bool ally = obj->getRelationship(other) != ENEMIES;
				// Who can be pushed: an idle ally, whoever is doing the pushing (the brief: moving units displace idle
				// ones). Tried: infantry not shoving parked vehicles. They then cannot get to their places among them
				// and never arrive (desert-mixed timed out).
				Bool pushable = otherIdle && ally;
				// ... and only one that is in the way: ahead of us, on our line. One we merely brush past stays put.
				if (pushable && !(rx * hx + ry * hy > 0.0f && fabs(rx * hy - ry * hx) < both * 0.8f))
					pushable = FALSE;
				// ... and not once we know our own place is taken: then it is we who stop short, not they who move.
				if (data->heldAtGoal > 0)
					pushable = FALSE;
				if (otherSpeed < 0.05f && dist < both * PUSH_CONTACT * 0.95f)
					touchingStanding = TRUE;	// (a little inside the distance pushing holds them at, or it never settles)
				Real awayX = dist > 0.01f ? -rx / dist : -hy, awayY = dist > 0.01f ? -ry / dist : hx;

				// Separation: stronger the nearer, nothing beyond SEP_RANGE. (An idle ally is pushed instead.)
				if (dist < both * SEP_RANGE)
				{
					Real w = 1.0f - dist / (both * SEP_RANGE);
					if (pushable)
						w *= 0.3f;
					else if (ally)
					{
						// the lighter of two allies does more of the keeping apart
						Real ourMass = physics->getMass(), itsMass = otherPhysics->getMass();
						Real share = (ourMass + itsMass) > 0.0f ? itsMass / (ourMass + itsMass) : 0.5f;
						w *= 0.4f + 1.2f * share;
					}
					sepX += awayX * w;
					sepY += awayY * w;
				}
				// Cohesion: the middle of the group-mates around.
				if (ai->getGroupKey() != 0 && aiOther->getGroupKey() == ai->getGroupKey())
				{
					cohX += rx;
					cohY += ry;
					++cohCount;
				}
				// Touching: pushed apart. Two moving units each take half; an idle ally is moved out of the way.
				if (dist < both * PUSH_CONTACT)
				{
					Real by = both * PUSH_CONTACT - dist;
					if (pushable)
					{
						Real give = by * 0.8f;
						if (give > PUSH_MAX) give = PUSH_MAX;
						Coord3D moved = *op;
						moved.x -= awayX * give;
						moved.y -= awayY * give;
						if (NavMesh::onMesh(other, aiOther->getLocomotorSet(), moved.x, moved.y) && roomFor(other, moved.x, moved.y, obj))
						{
							other->setPosition(&moved);
							++s_pushes;
							by *= 0.2f;
						}
						// (no room for it to move: all of the giving way is ours)
					}
					else if (otherIdle)
					{
						// it will not move for us: all of it is ours to give
					}
					else if (ally)
					{
						// Right of way: of two moving allies the one with the lower push rank (an order from the
						// player outranks a move the unit thought of itself) gives more of the ground.
						// Same rank: by weight. The heavier unit gives less (an Overlord moves a Battlemaster, not the
						// other way about): each takes the other's share of their combined mass.
						Int mine = ai->getPushRank(), its = aiOther->getPushRank();
						if (mine != its)
							by *= mine > its ? 0.25f : 0.75f;
						else
						{
							Real ourMass = physics->getMass(), itsMass = otherPhysics->getMass();
							Real share = (ourMass + itsMass) > 0.0f ? itsMass / (ourMass + itsMass) : 0.5f;
							if (share < 0.1f) share = 0.1f;
							if (share > 0.9f) share = 0.9f;
							by *= share;
						}
					}
					else
						by *= 0.5f;
					pushX += awayX * by;
					pushY += awayY * by;
				}
				// Someone standing where we are going, and we are nearly there?
				if (otherSpeed < 0.05f && sqr(op->x - route[lastPoint].x) + sqr(op->y - route[lastPoint].y) < sqr(both * PUSH_CONTACT))
					goalTaken = TRUE;	// standing there, we would be touching it
				// Following: no faster than a unit just ahead going our way allows.
				Real ahead = rx * hx + ry * hy;
				Real lateral = rx * hy - ry * hx;
				Real along = ov->x * hx + ov->y * hy;
				if (ahead > 0.0f && fabs(lateral) < both * FOLLOW_LANE && otherSpeed >= 0.05f && along > otherSpeed * SAME_WAY_COS)
				{
					Real most = along + (ahead - both * FOLLOW_GAP) / FOLLOW_HEADWAY;
					if (most < 0.0f) most = 0.0f;
					if (most < followMost) followMost = most;
				}
				// Avoidance: where will the two be nearest, going as they are?
				Real wx = ov->x - myVx, wy = ov->y - myVy;
				Real w2 = wx*wx + wy*wy;
				Bool goingOurWay = otherSpeed >= 0.05f && along > otherSpeed * SAME_WAY_COS;
				if (w2 > 0.0004f && !pushable && !goingOurWay)
				{
					Real t = -(rx * wx + ry * wy) / w2;
					if (t > 0.0f && t < AVOID_FRAMES)
					{
						Real mx = rx + wx * t, my = ry + wy * t;	// where it is then, from us
						Real miss = sqrt(mx*mx + my*my);
						if (miss < both * AVOID_CLEAR)
						{
							Real w = (1.0f - t / AVOID_FRAMES) * (1.0f - miss / (both * AVOID_CLEAR));
							if (ally)
							{
								Int mine = ai->getPushRank(), its = aiOther->getPushRank();
								if (mine > its) w *= 0.5f;	// it is for the other to get out of our way, mostly
								else if (mine < its) w *= 1.4f;
								else
								{
									// same rank: the lighter does more of the avoiding
									Real ourMass = physics->getMass(), itsMass = otherPhysics->getMass();
									Real share = (ourMass + itsMass) > 0.0f ? itsMass / (ourMass + itsMass) : 0.5f;
									w *= 0.4f + 1.2f * share;
								}
							}
							// Sideways, away from the side it will pass on; dead ahead: to the right.
							Real side = mx * hy - my * hx;	// > 0: it passes on our right
							Real dir = (side > 0.01f) ? -1.0f : 1.0f;
							avoidX += hy * dir * w;
							avoidY += -hx * dir * w;
						}
					}
				}
			}
		}
		// Its place is taken and it is nearly there: it stops making for the place and lets separation carry it
		// clear of whoever is standing there (the brief's arrival radius: the crowd at a spot grows outward).
		Bool settling = goalTaken && remaining < 3.0f * ourRadius + 2.0f * PATHFIND_CELL_SIZE_F;
		Real seekShare = settling ? 0.0f : 1.0f;
		Real askX = seekX * seekShare + sepX * W_SEPARATE + avoidX * W_AVOID;
		Real askY = seekY * seekShare + sepY * W_SEPARATE + avoidY * W_AVOID;
		// Held back by something it cannot push (a parked unit with no room to move, a crowd) and getting nowhere:
		// it goes round. The way round is along the face it is pressed against, to whichever side its route lies
		// (to the right if the thing is dead ahead), and it keeps to the side it chose until it is free. Only after
		// half a second of being held: brushing against something is not being stuck. (Found by the benchmark: an
		// infantryman marching on the spot against a parked tank.)
		{
			Real pressed = sqrt(pushX*pushX + pushY*pushY);
			Bool held = pressed > 0.05f && !settling && (pushX * seekX + pushY * seekY) < -0.3f * pressed;
			if (held)
				++data->stuckFrames;
			else if (data->stuckFrames > 0)
				data->stuckFrames -= (data->stuckFrames > 2 ? 2 : data->stuckFrames);
			if (data->stuckFrames == 0)
				data->slideSide = 0.0f;
			if (held && data->stuckFrames > 15)
			{
				Real tx = -pushY / pressed, ty = pushX / pressed;	// the face, turned left of the push
				if (data->slideSide == 0.0f)
				{
					Real along = tx * seekX + ty * seekY;
					data->slideSide = (along < -0.05f || (along <= 0.05f && (tx * hy - ty * hx) < 0.0f)) ? -1.0f : 1.0f;
				}
				askX += tx * data->slideSide * W_SLIDE;
				askY += ty * data->slideSide * W_SLIDE;
			}
		}
		// The blend gives a direction; how fast is a separate matter. Being pushed sideways turns a unit, it does not
		// slow it (scaling the whole blend down to length 1 cost a group in close order a fifth of its speed);
		// being pushed back does: the share of full speed is how much of the blend still points along the route.
		Real askLen = sqrt(askX*askX + askY*askY);
		Real share = askX * seekX + askY * seekY;
		if (settling)
			share = touchingStanding ? 0.4f : 0.0f;	// easing clear of the crowd, then standing
		else
		{
			if (share > 1.0f) share = 1.0f;
			if (share < 0.25f) share = 0.25f;
		}
		if (askLen > 0.001f)
		{
			askX = askX / askLen * share;
			askY = askY / askLen * share;
		}
		// The speed it would like: its own, less for a leader, and no more than lets it stop at the end.
		// (What the AI wants is in listed speeds: the same factor applies, see kinematicsOf.)
		Real top = desiredSpeed * SPEED_SCALE < kin.maxSpeed ? desiredSpeed * SPEED_SCALE : kin.maxSpeed;
		if (followMost < top)
			top = followMost;
		Real stopMost = (-kin.brake + sqrt(kin.brake * kin.brake + 8.0f * kin.brake * remaining)) * 0.5f;
		if (stopMost < top)
			top = stopMost;
		if (top < 0.0f)
			top = 0.0f;
		data->wantX += (askX * top - data->wantX) * WANT_SMOOTH;
		data->wantY += (askY * top - data->wantY) * WANT_SMOOTH;

		// Its place is taken and it is as near as it will get: this is where it stops (the arrival radius grows with
		// the crowd already there).
		// Nearly there and getting no nearer (the units already standing round its place hold it off, though nobody is
		// on the place itself): after a second and a half this is where it stands.
		// ("Nearly there" for this is as wide as its group stands: the crowd round a spot is that big.)
		if (!settling && remaining < 3.0f * ourRadius + 2.0f * PATHFIND_CELL_SIZE_F + ai->getGroupRadius())
		{
			if (remaining > data->lastRemaining - 0.05f)
				++data->nearGoalFrames;
			else if (data->nearGoalFrames > 0)
				--data->nearGoalFrames;
			if (data->nearGoalFrames > 45)
				settling = TRUE, data->heldAtGoal = 100;
		}
		else
			data->nearGoalFrames = 0;
		data->lastRemaining = remaining;
		if (settling)
		{
			// (counted only while it is clear of everyone standing: it does not stop on top of anybody)
			// ... but it does stop: after two seconds of trying it stands where it is.
			++data->heldAtGoal;
			if ((!touchingStanding && data->heldAtGoal > 8) || data->heldAtGoal > 60)
			{
				Coord3D here = *pos;
				path->updateLastNode(&here);
				data->pathLast = here;
				data->arrived = TRUE;
				++s_arrivedShort;
				AIDrive::release(obj, data);
				return TRUE;
			}
		}
		else
			data->heldAtGoal = 0;

		// ---- Moving as this unit can.
		Real wantSpeed = sqrt(data->wantX * data->wantX + data->wantY * data->wantY);
		Real off = wantSpeed > 0.01f ? wrapAngle(atan2(data->wantY, data->wantX) - heading) : 0.0f;
		Real offMag = fabs(off);
		Bool wheels = kin.turnSpeed > 0.0f;
		// Facing away from where it wants to go it goes slower: tracks and legs down to a pivot, wheels down to
		// the speed they need to be able to turn at all.
		Real facing = 1.0f;
		if (offMag > 0.35f)
			facing = offMag >= 1.2f ? 0.0f : 1.0f - (offMag - 0.35f) / 0.85f;
		Real goSpeed = wantSpeed * facing;
		if (wheels && offMag > 0.1f && wantSpeed > 0.05f && goSpeed < kin.turnSpeed * 0.6f)
			goSpeed = kin.turnSpeed * 0.6f;
		// A wheeled unit whose point lies inside its own turning circle can never reach it by driving: it would
		// circle it for ever (retail has a timer for this). There it creeps and turns as tightly as it needs to, as
		// a driver would shuffle a car round.
		Bool tightSpot = FALSE;
		if (wheels)
		{
			Real circle = kin.turnSpeed / kin.turnRate;
			Real aheadOfUs = toX * hx + toY * hy;
			Real beside = fabs(toX * hy - toY * hx);
			if (aheadOfUs * aheadOfUs + (beside - circle) * (beside - circle) < circle * circle * 1.1f)
			{
				tightSpot = TRUE;
				if (goSpeed > kin.turnSpeed * 0.35f)
					goSpeed = kin.turnSpeed * 0.35f;
			}
		}
		Real speed = data->speed;
		if (goSpeed > speed)
			speed = (speed + kin.accel < goSpeed) ? speed + kin.accel : goSpeed;
		else
			speed = (speed - kin.brake > goSpeed) ? speed - kin.brake : goSpeed;
		Real turnMost = kin.turnRate;
		if (wheels && speed < kin.turnSpeed)
			turnMost *= speed / kin.turnSpeed;
		if (wheels && tightSpot)
			turnMost = kin.turnRate;
		// A small error is closed gently (a steady stream of small changes in what is asked for must not show as
		// the unit twitching); a real one with the eased turn.
		Real turn = off * TURN_EASE;
		if (offMag < TURN_GENTLE)
			turn = off * TURN_GENTLE_SHARE;
		if (turn > turnMost) turn = turnMost;
		if (turn < -turnMost) turn = -turnMost;
		Real newHeading = wrapAngle(heading + turn);
		Real nhx = Cos(newHeading), nhy = Sin(newHeading);

		Real pushLen = sqrt(pushX*pushX + pushY*pushY);
		if (pushLen > PUSH_MAX)
		{
			pushX *= PUSH_MAX / pushLen;
			pushY *= PUSH_MAX / pushLen;
		}
		Real nx = x + nhx * speed + pushX, ny = y + nhy * speed + pushY;
		if (onGround && !NavMesh::onMesh(obj, ai->getLocomotorSet(), nx, ny)
				&& NavMesh::onMesh(obj, ai->getLocomotorSet(), x, y))
		{
			// The step would take it off its ground: it slides along the edge instead. The step is turned, a little
			// at a time and to whichever side keeps more of its way towards the route, until it stays on; failing
			// that it stands this frame. (A unit already off its mesh, built there or against a wall, is let move.)
			Real sx = nx - x, sy = ny - y;
			static const Real TURNED[6] = { 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f };
			Bool found = FALSE;
			Real bestGain = -1.0e9f, bestX = x, bestY = y;
			for (Int k = 0; k < 6; ++k)
			{
				Real c = Cos(TURNED[k]), sn = Sin(TURNED[k]);
				Real tx = sx * c - sy * sn, ty = sx * sn + sy * c;
				if (!NavMesh::onMesh(obj, ai->getLocomotorSet(), x + tx, y + ty))
					continue;
				Real gain = tx * seekX + ty * seekY;
				if (!found || gain > bestGain + 0.001f)
				{
					found = TRUE;
					bestGain = gain;
					bestX = x + tx;
					bestY = y + ty;
				}
				if ((k & 1) == 1 && found)
					break;	// the smallest turn that works, on its better side
			}
			if (found)
			{
				nx = bestX;
				ny = bestY;
			}
			else
			{
				speed = 0.0f;
				nx = x;
				ny = y;
				++s_wallStops;
				data->wallFrames += 2;
				if (DEBUG_UNIT != 0 && (Int)obj->getID() == DEBUG_UNIT && (now % 30) == 0)
					printf("DBGWALL f=%u at=(%.1f,%.1f) step=(%.2f,%.2f) target=(%.1f,%.1f) pt=%d/%d routeEnd=(%.1f,%.1f)\n", now, x, y, sx, sy, target.x, target.y, data->nextPoint, lastPoint, route[lastPoint].x, route[lastPoint].y);
				if (DEBUG_UNIT != 0 && (Int)obj->getID() == DEBUG_UNIT && (now % 30) == 0)
				{
					char line[80];
					Int n = 0;
					for (Int k = -6; k <= 14 && n < 70; ++k)
						line[n++] = NavMesh::onMesh(obj, ai->getLocomotorSet(), x + seekX * 4.0f * (Real)k, y + seekY * 4.0f * (Real)k) ? '#' : '.';
					line[n] = 0;
					printf("DBGWALL2 along the way to the target, 4 apart, from 24 behind: %s seg=%d\n", line,
						NavMesh::segmentOnMesh(obj, ai->getLocomotorSet(), x, y, target.x, target.y) ? 1 : 0);
				}
			}
		}
		// Held at a wall for a second: the route cannot be followed from where it has ended up (pushed into a
		// pocket, or handed a route whose next point is not reachable from here). It gets a new one from where it
		// stands. (Found by the benchmark: an infantryman facing a wall for the rest of the run.)
		if (data->wallFrames > 0)
			--data->wallFrames;
		if (data->wallFrames > LOGICFRAMES_PER_SECOND)
		{
			data->wallFrames = 0;
			Coord3D here = *pos, to;
			to.x = route[lastPoint].x;
			to.y = route[lastPoint].y;
			to.z = data->pathLast.z;
			Path *fresh = NavMesh::findPath(obj, ai->getLocomotorSet(), &here, &to, nullptr);
			if (fresh != nullptr)
			{
				std::vector<NavDrive::Pt> newRoute;
				std::vector<unsigned char> newGround;
				NavDrive::Pt pt;
				pt.x = here.x; pt.y = here.y;
				newRoute.push_back(pt);
				newGround.push_back(obj->getLayer() == LAYER_GROUND ? 1 : 0);
				Int guard = 4096;
				for (PathNode *node = fresh->getFirstNode()->getNextOptimized(); node != nullptr && --guard > 0; node = node->getNextOptimized())
				{
					pt.x = node->getPosition()->x;
					pt.y = node->getPosition()->y;
					newRoute.push_back(pt);
					newGround.push_back(node->getLayer() == LAYER_GROUND ? 1 : 0);
				}
				deleteInstance(fresh);
				if (DEBUG_UNIT != 0 && (Int)obj->getID() == DEBUG_UNIT)
				{
					printf("DBGREROUTE f=%u", now);
					for (size_t q = 0; q < newRoute.size(); ++q)
						printf(" (%.1f,%.1f)", newRoute[q].x, newRoute[q].y);
					printf("\n");
				}
				++s_reroutes;
				if (newRoute.size() >= 2)
				{
					data->route.swap(newRoute);
					data->routeOnGround.swap(newGround);
					data->nextPoint = 1;
					data->speed = speed;
					data->shownHeading = newHeading;
					applyPose(obj, physics, data, nx, ny, newHeading, speed, turn);
					return TRUE;
				}
			}
		}
		data->speed = speed;
		data->shownHeading = newHeading;
		applyPose(obj, physics, data, nx, ny, newHeading, speed, turn);
		if (DEBUG_UNIT != 0 && (Int)obj->getID() == DEBUG_UNIT)
		{
			printf("DBGSTEER2 f=%u pt=%d/%d x=%.1f y=%.1f h=%.3f v=%.3f want=(%.2f,%.2f) sep=(%.2f,%.2f) avoid=(%.2f,%.2f) push=(%.2f,%.2f) follow=%.2f\n",
				now, data->nextPoint, lastPoint, nx, ny, newHeading, speed, data->wantX, data->wantY, sepX, sepY, avoidX, avoidY, pushX, pushY,
				followMost > 1.0e8f ? -1.0f : followMost);
		}
		return TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool AIDrive::update( Object *obj, AIUpdateInterface *ai, Data *& data, Real desiredSpeed )
{
	loadTuning();
	if (!ENABLED || !MoveRules::navmesh())
		return FALSE;
	Path *path = ai->getPath();
	Locomotor *loco = ai->getCurLocomotor();
	PhysicsBehavior *physics = obj->getPhysics();
	if (path == nullptr || loco == nullptr || physics == nullptr || path->getFirstNode() == nullptr || path->getLastNode() == nullptr)
		return FALSE;
	if (obj->isKindOf(KINDOF_AIRCRAFT) || obj->isKindOf(KINDOF_PROJECTILE) || obj->isAboveTerrain())
		return FALSE;

	NavDrive::Kinematics kin;
	kinematicsOf(obj, loco, kin);
	if (kin.maxSpeed <= 0.0f || kin.turnRate <= 0.0f)
		return FALSE;
	return steer(obj, ai, data, path, kin, desiredSpeed, physics);
}

//-------------------------------------------------------------------------------------------------
void AIDrive::printStats()
{
	if (!MoveRules::navmesh())
		return;
	printf("AIDrive: routes=%d cornersSpread=%d spreadUndone=%d steerFrames=%d lookAheads=%d pushes=%d wallStops=%d arrivedShort=%d reroutes=%d\n",
		s_plans, s_spread, s_spreadUndone, s_steerFrames, s_lookAheads, s_pushes, s_wallStops, s_arrivedShort, s_reroutes);
}
