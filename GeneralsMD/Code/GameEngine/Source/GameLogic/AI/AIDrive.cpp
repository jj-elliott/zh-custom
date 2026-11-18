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
		NavDrive::Plan plan;
		NavDrive::State state;
		// What the plan was made for: a different path (or the same one changed) means a new plan.
		const Path *path;
		Coord3D pathFirst, pathLast;
		Real shownHeading;	///< the heading the object is given: the plan's, eased (see TURN_EASE)
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
	Real OFF_PLAN = 3.0f;						// world units the object may be from where the plan has it before the plan is dropped
	Real ARC_SAMPLE = 4.0f;					// world units between the points of an arc that are checked against the mesh
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
	Int DEBUG_UNIT = 0;
	Bool s_loaded = false;
	Int s_spread = 0, s_spreadUndone = 0;

	// The shapes of the groups most recently ordered to move (AIDrive::registerGroup).
	struct GroupShape
	{
		UnsignedInt key;
		std::vector<Real> offsets;	// x, y per member
	};
	const size_t MAX_SHAPES = 48;
	std::vector<GroupShape> s_shapes;
	UnsignedInt s_nextShapeKey = 1;

	Int s_plans = 0, s_smaller = 0, s_pivots = 0, s_cheats = 0, s_offMesh = 0, s_abandoned = 0;
	Int s_frames = 0, s_followFrames = 0;

	void loadTuning()
	{
		if (s_loaded)
			return;
		s_loaded = true;
		MoveTune::apply("drive.enabled", ENABLED);
		MoveTune::apply("drive.followHeadway", FOLLOW_HEADWAY);
		MoveTune::apply("drive.followLane", FOLLOW_LANE);
		MoveTune::apply("drive.followGap", FOLLOW_GAP);
		MoveTune::apply("drive.offPlan", OFF_PLAN);
		MoveTune::apply("drive.turnEase", TURN_EASE);
		MoveTune::apply("drive.spread", SPREAD);
		MoveTune::apply("drive.spreadSpacing", SPREAD_SPACING);
		MoveTune::apply("drive.spreadMax", SPREAD_MAX);
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
		kin.maxSpeed = loco->getMaxSpeedForCondition(damage);
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
			kin.turnSpeed = loco->getMinTurnSpeed();
			if (kin.turnSpeed < kin.maxSpeed * 0.25f)
				kin.turnSpeed = kin.maxSpeed * 0.25f;
			// Above that speed its turning circle grows with speed: it corners wide and fast where there is room.
		}
	}

	/// Are the plan's arcs on ground this unit may be on? (The straights are the path's own, give or take the
	/// tangents; the arcs are where the line leaves the string, by up to a turning radius.)
	Bool arcsOnMesh( Object *obj, AIUpdateInterface *ai, const NavDrive::Plan& plan )
	{
		for (size_t i = 0; i < plan.prims.size(); ++i)
		{
			const NavDrive::Prim& p = plan.prims[i];
			if (p.type != NavDrive::ARC || i == 0)	// (a first arc is the unit turning where it stands: it is where it is)
				continue;
			Int steps = REAL_TO_INT_FLOOR(p.length / ARC_SAMPLE) + 1;
			for (Int k = 1; k <= steps; ++k)
			{
				Real a = p.a0 + p.sweep * (Real)k / (Real)(steps + 1);
				if (!NavMesh::onMesh(obj, ai->getLocomotorSet(), p.cx + p.r * Cos(a), p.cy + p.r * Sin(a)))
					return FALSE;
			}
		}
		return TRUE;
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

	Bool makePlan( Object *obj, AIUpdateInterface *ai, Path *path, const NavDrive::Kinematics& kin, AIDrive::Data *data )
	{
		// The string: where the unit is, then the path's corners (its optimized nodes) after the first.
		std::vector<NavDrive::Pt> pts;
		NavDrive::Pt pt;
		const Coord3D *pos = obj->getPosition();
		pt.x = pos->x; pt.y = pos->y;
		pts.push_back(pt);
		Bool allGround = obj->getLayer() == LAYER_GROUND;
		Int guard = 4096;
		for (PathNode *node = path->getFirstNode()->getNextOptimized(); node != nullptr && --guard > 0; node = node->getNextOptimized())
		{
			pt.x = node->getPosition()->x;
			pt.y = node->getPosition()->y;
			pts.push_back(pt);
			if (node->getLayer() != LAYER_GROUND)
				allGround = FALSE;
		}
		if (pts.size() < 2)
			return FALSE;
		if (allGround)
			spreadCorners(obj, ai, pts);

		// Full-size arcs if they are on the unit's ground, else smaller ones, else none (stop and pivot). Paths that
		// use a bridge are not checked: bridges are not in the mesh yet.
		static const Real SCALE[4] = { 1.0f, 0.5f, 0.25f, 0.0f };
		for (Int s = 0; s < 4; ++s)
		{
			NavDrive::Kinematics k = kin;
			k.cornerSpeed *= SCALE[s];
			k.turnSpeed *= SCALE[s];
			if (s == 3)
				k.turnSpeed = 0.0f;	// a wheeled unit pivots too when there is no room for anything else.
			NavDrive::build(pts, obj->getOrientation(), k, data->plan);
			if (data->plan.prims.empty())
				return FALSE;
			if (s == 3 || !allGround || arcsOnMesh(obj, ai, data->plan))
			{
				if (s > 0)
					++s_offMesh;
				break;
			}
		}
		PhysicsBehavior *physics = obj->getPhysics();
		Real speed = physics ? physics->getForwardSpeed2D() : 0.0f;
		if (speed < 0.0f)
			speed = 0.0f;
		if (speed > kin.maxSpeed)
			speed = kin.maxSpeed;
		NavDrive::start(data->plan, pos->x, pos->y, obj->getOrientation(), speed, data->state);
		data->shownHeading = obj->getOrientation();
		++s_plans;
		s_smaller += data->plan.smallerArcs;
		s_pivots += data->plan.pivots;
		s_cheats += data->plan.cheats;
		return TRUE;
	}

	/// The most speed the units ahead allow: behind one going our way we go its speed plus what closes the gap.
	Real followCap( Object *obj, const NavDrive::State& st, const NavDrive::Kinematics& kin )
	{
		Real cap = 1.0e9f;
		Real hx = Cos(st.heading), hy = Sin(st.heading);
		Real ourRadius = radiusOf(obj);
		Bool weAreInfantry = obj->isKindOf(KINDOF_INFANTRY);
		Real range = ourRadius + kin.maxSpeed * FOLLOW_HEADWAY + 6.0f * PATHFIND_CELL_SIZE_F;
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
			if (aiOther == nullptr || otherPhysics == nullptr || !aiOther->isDoingGroundMovement())
				continue;
			// Infantry walk through each other, and vehicles do not queue behind infantry.
			if (other->isKindOf(KINDOF_INFANTRY))
				continue;
			if (weAreInfantry)
				continue;
			++count;
			const Coord3D *op = other->getPosition();
			Real rx = op->x - st.x, ry = op->y - st.y;
			Real ahead = rx * hx + ry * hy;
			Real lateral = rx * hy - ry * hx;
			Real both = ourRadius + radiusOf(other);
			if (ahead <= 0.0f || fabs(lateral) > both * FOLLOW_LANE)
				continue;
			// Going our way, and actually going?
			const Coord3D *ov = otherPhysics->getVelocity();
			Real along = ov->x * hx + ov->y * hy;
			Real otherSpeed = sqrt(ov->x * ov->x + ov->y * ov->y);
			if (otherSpeed < 0.05f || along < otherSpeed * SAME_WAY_COS)
				continue;
			Real gap = ahead - both * FOLLOW_GAP;
			Real most = along + gap / FOLLOW_HEADWAY;
			if (most < 0.0f)
				most = 0.0f;
			if (most < cap)
				cap = most;
		}
		return cap;
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
			data = new Data;
			data->holding = FALSE;
		}
		data->path = path;
		data->pathFirst = *first;
		data->pathLast = *last;
		data->abandoned = !makePlan(obj, ai, path, kin, data);
	}
	if (data->abandoned)
		return FALSE;

	// Is the object where the plan left it? If something else has moved it, the plan no longer describes it.
	NavDrive::State& st = data->state;
	{
		Real dx = pos->x - st.x, dy = pos->y - st.y;
		if (dx*dx + dy*dy > OFF_PLAN * OFF_PLAN)
		{
			data->abandoned = TRUE;
			++s_abandoned;
			return FALSE;
		}
	}
	if (st.done)
		return FALSE;	// at the end of the line: the old code does the arriving.

	Real cap = desiredSpeed;
	Real follow = followCap(obj, st, kin);
	++s_frames;
	if (follow < cap)
	{
		cap = follow;
		++s_followFrames;
	}
	NavDrive::step(data->plan, kin, cap, st);

	Coord3D to;
	to.x = st.x;
	to.y = st.y;
	to.z = TheTerrainLogic->getLayerHeight(st.x, st.y, obj->getLayer());
	obj->setPosition(&to);
	// The body's heading follows the plan's with a little lag (TURN_EASE), never past it.
	Real behind = st.heading - data->shownHeading;
	while (behind > PI) behind -= 2.0f * PI;
	while (behind <= -PI) behind += 2.0f * PI;
	Real turnNow = behind * TURN_EASE;
	if (TURN_EASE >= 1.0f || fabs(behind) < 0.002f || st.done)
		turnNow = behind;
	data->shownHeading += turnNow;
	while (data->shownHeading > PI) data->shownHeading -= 2.0f * PI;
	while (data->shownHeading <= -PI) data->shownHeading += 2.0f * PI;
	obj->setOrientation(data->shownHeading);
	// Physics does not move a unit with this status (the engine's own rule for a braking unit), and it is told the
	// velocity the unit has so that whatever reads it sees the truth.
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
	data->holding = TRUE;
	// Physics is kept awake and "driven" the way the locomotor keeps it (a motive force, here of nothing), because
	// the wheels, treads and dust are drawn from its speed and from whether it is driven; then its velocity is made
	// the unit's, and its cached speed thrown away (scrubVelocity2D is the call that does that).
	Coord3D none;
	none.zero();
	physics->applyMotiveForce(&none);
	Coord3D delta;
	const Coord3D *vel = physics->getVelocity();
	delta.x = Cos(st.heading) * st.moved - vel->x;
	delta.y = Sin(st.heading) * st.moved - vel->y;
	delta.z = 0.0f;
	physics->addVelocityTo(&delta);
	physics->scrubVelocity2D(st.moved * 0.9999f);
	physics->setTurning(turnNow > 0.002f ? TURN_POSITIVE : (turnNow < -0.002f ? TURN_NEGATIVE : TURN_NONE));

	if (DEBUG_UNIT != 0 && (Int)obj->getID() == DEBUG_UNIT)
	{
		printf("DBGDRIVE f=%u prim=%d/%d along=%.2f v=%.3f cap=%.3f x=%.1f y=%.1f h=%.3f\n", TheGameLogic->getFrame(), st.prim,
			(Int)data->plan.prims.size(), st.along, st.v, cap, st.x, st.y, st.heading);
	}
	if (st.done)
		release(obj, data);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AIDrive::printStats()
{
	if (!MoveRules::navmesh())
		return;
	printf("AIDrive: plans=%d smallerArcs=%d pivots=%d cheats=%d arcsOffMesh=%d abandoned=%d followPct=%.1f cornersSpread=%d spreadUndone=%d\n",
		s_plans, s_smaller, s_pivots, s_cheats, s_offMesh, s_abandoned, s_frames ? 100.0f * (Real)s_followFrames / (Real)s_frames : 0.0f,
		s_spread, s_spreadUndone);
}
