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
#include "GameLogic/MoveRules.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	// ---- Tuning. Everything that decides how movement feels is here. ----
	Real HORIZON = 1.5f * LOGICFRAMES_PER_SECOND;	// frames we look ahead.
	Real CLEARANCE = 1.1f;												// combined radius is padded by 10%.
	const Real STANDING_SPEED = 0.05f;									// world units per frame; slower counts as standing.
	const Int MAX_NEIGHBOURS = 24;

	Real COST_COLLISION = 10.0f;	// a collision right now; falls off with the square of the time left.
	// Second play-test of the unified system: "sticky, not flowing enough". Slowing and stopping were too cheap next
	// to going round (was 1.5 / 3.0), so units braked for each other where they had room to pass.
	Real COST_HALF_SPEED = 2.5f;
	Real COST_STOP = 5.0f;
	Real START_COST = 0.6f;				// a threat milder than this (about 1.1 s away) doesn't start a manoeuvre...
	UnsignedInt STEERING_RECENT = LOGICFRAMES_PER_SECOND/2;	// ...but one under way is seen through.
	UnsignedInt COMMIT_FRAMES = LOGICFRAMES_PER_SECOND;			// how long a chosen side stays chosen.
	Real COST_CHANGE_SIDE = 4.0f;	// switching side while committed: only to dodge something imminent.
	Real BONUS_SAME_SIDE = 0.8f;
	Real EASE = 0.12f;						// share of the wanted turn applied per frame; the rest is last frame's (was 0.25).
	UnsignedInt ESCAPE_AFTER = LOGICFRAMES_PER_SECOND;			// held at a standstill this long = jammed (was 2 s).
	// Queueing applies to a unit we are really into, not merely beside: within 85% of the bare combined radius and
	// inside 45 degrees of dead ahead. (It used the padded avoidance radius and the whole front half-plane, so units
	// travelling abreast were permanently "queued" behind whichever neighbour was a nose in front, and any that
	// turned or slowed dragged its neighbours down with it.)
	// Friendly infantry in a vehicle's way are slid sideways out of its track; the vehicle neither steers nor slows.
	Real NUDGE_LOOKAHEAD = 1.5f * PATHFIND_CELL_SIZE_F;	// how far ahead of its nose the vehicle starts moving them.
	Real NUDGE_MARGIN = 1.0f;														// clearance left beside the hull.
	Real NUDGE_PER_FRAME = 1.5f;												// world units a frame: brisk walking pace, not a shove.

	Real QUEUE_CONTACT = 0.85f;										// of the bare combined radius.
	Real QUEUE_CONE = 0.70710678f;								// cos 45.
	Real TURN_COST_SCALE = 1.0f;									// multiplies every Turn::cost below.
	// A man nudged out of a vehicle's track is put this much beyond its edge, so that walking back toward his own
	// path does not put him straight back in it (third play-test: infantry jittering along a hull).
	Real NUDGE_OVERSHOOT = 3.0f;

	// Separation: two allied vehicles under way that are inside each other are eased apart, each moving itself half
	// the way. The suite showed that nearly all overlap (44 of 45 pair-seconds) is between units of one group
	// travelling together, which avoidance cannot see: they are not on a collision course, they are on the same line.
	Real SEP_SHARE = 0.8f;		// of the bare combined radius: closer than this is "inside" (two tanks abreast touch at about 0.8).
	Real SEP_GAIN = 0.25f;		// share of our half of the penetration removed per frame.
	Real SEP_MAX = 0.5f;			// world units a frame at most.

	// THE INVARIANT (2026-11-17, after the play-test "units that have no business freaking out"): a unit does what
	// its path says unless it has a reason not to, and each reason is one of exactly three, each a fact about this
	// frame, none remembered:
	//   follow    a unit ahead of us, in our lane, going our way, slower than we would go: we slow down. Never turn.
	//   avoid     carrying on AS WE ARE ACTUALLY MOVING would hit a unit that is not going our way: least turn that
	//             clears it, let go the moment it is not needed.
	//   separate  we are inside an allied vehicle: ease apart.
	// What broke it before, all found by reading the code against that sentence:
	//   - collisions were predicted at the unit's top speed, not its speed. A Humvee keeping pace behind a Crusader
	//     "was about to ram it" every frame, for ever, and danced left and right to avoid a collision that was not
	//     going to happen.
	//   - a manoeuvre under way continued for any threat at all, however far off (the start threshold dropped to 0).
	//   - a turn was held 8 frames after its reason had gone and could only grow while it lasted (added the same
	//     morning to make a number go down; it made units swing wide).
	//   - separation began at 90% of the combined radius, which two tanks abreast are inside of without touching.
	// MoveBench's steerPct / pushPct (share of moving unit-frames with a deviation) are the measure of this.
	Real SAME_DIR_COS = 0.5f;				// "going our way": within 60 degrees of our path direction.
	Real FOLLOW_LANE = 0.8f;				// of the bare combined radius: closer to our line than this = in our lane.
	Real FOLLOW_GAP = 1.1f;					// of the bare combined radius: the gap we keep is measured from here.
	Real FOLLOW_HEADWAY = 20.0f;		// frames: we may go faster than the leader by gap / headway.
	Real RELEASE_SHARE = 0.5f;			// a manoeuvre under way ends when the threat is below this share of START_COST.

	// The candidate chosen last frame is this much cheaper than the others. Traced on a Humvee tailing a Crusader:
	// "straight at half speed" and "15 degrees left at half speed" were within 0.05 of each other and it took them
	// alternately, every frame, for seconds (99 turn reversals for that one unit).
	Real STICK = 0.6f;
	Real EASE_OUT = 0.12f;				// EASE on the way back to the path direction.
	UnsignedInt DEBUG_UNIT = 0;

	// "-movetune" overrides, read once (GameLogic/MoveRules.h). Names are what a tuning sweep refers to.
	void loadTuning()
	{
		static Bool loaded = false;
		if (loaded)
			return;
		loaded = true;
		MoveTune::apply("steer.horizon", HORIZON);
		MoveTune::apply("steer.clearance", CLEARANCE);
		MoveTune::apply("steer.costCollision", COST_COLLISION);
		MoveTune::apply("steer.costHalfSpeed", COST_HALF_SPEED);
		MoveTune::apply("steer.costStop", COST_STOP);
		MoveTune::apply("steer.startCost", START_COST);
		MoveTune::apply("steer.recentFrames", STEERING_RECENT);
		MoveTune::apply("steer.commitFrames", COMMIT_FRAMES);
		MoveTune::apply("steer.costChangeSide", COST_CHANGE_SIDE);
		MoveTune::apply("steer.bonusSameSide", BONUS_SAME_SIDE);
		MoveTune::apply("steer.ease", EASE);
		MoveTune::apply("steer.turnCostScale", TURN_COST_SCALE);
		MoveTune::apply("queue.escapeFrames", ESCAPE_AFTER);
		MoveTune::apply("queue.contact", QUEUE_CONTACT);
		MoveTune::apply("queue.cone", QUEUE_CONE);
		MoveTune::apply("nudge.lookahead", NUDGE_LOOKAHEAD);
		MoveTune::apply("nudge.margin", NUDGE_MARGIN);
		MoveTune::apply("nudge.perFrame", NUDGE_PER_FRAME);
		MoveTune::apply("nudge.overshoot", NUDGE_OVERSHOOT);
		MoveTune::apply("sep.share", SEP_SHARE);
		MoveTune::apply("sep.gain", SEP_GAIN);
		MoveTune::apply("sep.max", SEP_MAX);
		MoveTune::apply("steer.sameDirCos", SAME_DIR_COS);
		MoveTune::apply("follow.lane", FOLLOW_LANE);
		MoveTune::apply("follow.gap", FOLLOW_GAP);
		MoveTune::apply("follow.headway", FOLLOW_HEADWAY);
		MoveTune::apply("steer.releaseShare", RELEASE_SHARE);
		MoveTune::apply("steer.easeOut", EASE_OUT);
		MoveTune::apply("steer.stick", STICK);
		MoveTune::apply("debug.unit", DEBUG_UNIT);
	}

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

	// In front of us (direction dirX, dirY) and overlapping enough to queue behind.
	inline Bool queuedBehind( const Neighbour& n, Real dirX, Real dirY )
	{
		Real distSqr = n.x*n.x + n.y*n.y;
		Real reach = n.radius * QUEUE_CONTACT / CLEARANCE;	// n.radius is padded by CLEARANCE.
		if (distSqr > reach*reach)
			return FALSE;
		Real ahead = n.x*dirX + n.y*dirY;
		return ahead > 0.0f && ahead*ahead >= QUEUE_CONE*QUEUE_CONE*distSqr;
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
// A vehicle makes room for itself among friendly infantry, as in StarCraft: anyone inside the strip its hull is
// about to sweep is slid sideways, out of the nearer edge, a little each frame. No AI state changes on either side:
// the infantryman keeps doing what he was doing, a step to the side; if he was walking somewhere his path follower
// carries on from where he now is. He is never moved onto ground he couldn't walk on (then the other side is tried,
// and failing that the vehicle simply passes through him, as it would have before). Men who are busy with something
// that depends on where they stand are left alone.
static void nudgeAside( Object *vehicle, Object *man, AIUpdateInterface *aiMan )
{
	if (aiMan->isBusy() || man->testStatus(OBJECT_STATUS_IS_USING_ABILITY))
		return;

	const Coord3D *heading = vehicle->getUnitDirectionVector2D();
	const GeometryInfo& hull = vehicle->getGeometryInfo();
	Real halfLength = hull.getMajorRadius();
	Real halfWidth = (hull.getGeomType() == GEOMETRY_BOX) ? hull.getMinorRadius() : hull.getMajorRadius();

	Real relX = man->getPosition()->x - vehicle->getPosition()->x;
	Real relY = man->getPosition()->y - vehicle->getPosition()->y;
	Real forward = relX*heading->x + relY*heading->y;
	if (forward < -halfLength || forward > halfLength + NUDGE_LOOKAHEAD)
		return;
	Real leftX = -heading->y;	// unit vector to the vehicle's left.
	Real leftY = heading->x;
	Real lateral = relX*leftX + relY*leftY;
	Real clear = halfWidth + man->getGeometryInfo().getMajorRadius() + NUDGE_MARGIN;
	Real inside = clear - WWMath::Fabs(lateral);
	if (inside <= 0.0f)
		return;
	Real step = inside + NUDGE_OVERSHOOT;
	if (step > NUDGE_PER_FRAME)
		step = NUDGE_PER_FRAME;

	Real side = (lateral >= 0.0f) ? 1.0f : -1.0f;
	for (Int attempt = 0; attempt < 2; ++attempt, side = -side)
	{
		Coord3D to = *man->getPosition();
		to.x += leftX * side * step;
		to.y += leftY * side * step;
		if (!TheAI->pathfinder()->validMovementPosition(FALSE, man->getLayer(), aiMan->getLocomotorSet(), &to))
			continue;
		to.z = TheTerrainLogic->getLayerHeight(to.x, to.y, man->getLayer());
		man->setPosition(&to);
		if (aiMan->isIdle())
		{
			// A standing man holds a cell in the pathfinder's map; move his claim with him.
			TheAI->pathfinder()->updatePos(man, &to);
			TheAI->pathfinder()->updateGoal(man, &to, man->getLayer());
		}
		return;
	}
}

//-------------------------------------------------------------------------------------------------
// Look around and decide. Returns FALSE for "carry on as the path says"; otherwise bestTurn (index into TURNS) and
// bestSpeedFactor hold the choice. followCap is lowered to the most we may do behind a moving unit we already
// touch. dirX/dirY is the direction the path follower wants.
static Bool choose( Object *obj, AIUpdateInterface *ai, Real dirX, Real dirY, Real speed, const State& state,
										UnsignedInt now, Int& bestTurn, Real& bestSpeedFactor, Real& followCap,
										Real& pushX, Real& pushY, Int& bestSpeedStep )
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
	bestSpeedStep = 0;
	Bool haveLast = state.lastTurn > 0 && (now - state.lastChoiceFrame) <= 2;

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
		// A vehicle does not go round friendly infantry and does not slow for them: it moves them out of its track.
		if (!weAreInfantry && other->isKindOf(KINDOF_INFANTRY) && obj->getRelationship(other) == ALLIES)
		{
			nudgeAside(obj, other, aiOther);
			continue;
		}
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
		Real bareRadius = n.radius / CLEARANCE;

		Real ahead = n.x*dirX + n.y*dirY;

		// Separation from an allied vehicle under way that we are inside.
		if (!weAreInfantry && SEP_GAIN > 0.0f && aiOther->isMoving() && obj->getRelationship(other) == ALLIES)
		{
			Real target = bareRadius * SEP_SHARE;
			Real distSqr = n.x*n.x + n.y*n.y;
			if (distSqr < target*target)
			{
				Real dist = WWMath::Sqrtf(distSqr);
				Real awayX, awayY;
				if (dist > 0.01f)
				{
					awayX = -n.x / dist;
					awayY = -n.y / dist;
				}
				else
				{
					awayX = (obj->getID() < other->getID()) ? 1.0f : -1.0f;	// exactly stacked: any fixed rule will do.
					awayY = 0.0f;
				}
				Real share = 0.5f * (target - dist) * SEP_GAIN;
				pushX += awayX * share;
				pushY += awayY * share;
			}
		}

		// Follow: a unit ahead, in our lane, going our way, sets our pace: its own speed in our direction plus what
		// the gap allows. Slowing is the whole response; a fellow traveller is never turned away from.
		Bool fellow = FALSE;
		if (!n.standing && aiOther->isMoving())
		{
			Real pace = n.vx*dirX + n.vy*dirY;
			Real otherSpeedSqr = n.vx*n.vx + n.vy*n.vy;
			if (pace > 0.0f && pace*pace >= SAME_DIR_COS*SAME_DIR_COS*otherSpeedSqr)
			{
				fellow = TRUE;
				Real lateral = -n.x*dirY + n.y*dirX;
				// It leads only if we are behind it by its own heading too: two units converging on one line each see
				// the other "ahead", and would slow each other to a stop.
				Bool weAreBehindIt = (n.x*n.vx + n.y*n.vy) > 0.0f;
				if (ahead > 0.0f && weAreBehindIt && WWMath::Fabs(lateral) < bareRadius*FOLLOW_LANE)
				{
					Real gap = ahead - bareRadius*FOLLOW_GAP;
					if (gap < 0.0f)
						gap = 0.0f;
					Real cap = pace + gap / FOLLOW_HEADWAY;
					if (cap < followCap)
						followCap = cap;
				}
			}
		}
		// A unit that is under orders but not getting anywhere (a jam), touched and dead ahead: we wait behind it. This
		// is the one case that can hold us at a standstill, and the caller's escape is for it.
		else if (n.standing && aiOther->isMoving() && queuedBehind(n, dirX, dirY))
		{
			followCap = 0.0f;
		}

		// A moving unit behind us is its own problem: it sees us ahead.
		if (!n.standing && ahead < 0.0f)
			continue;
		if (fellow)
			continue;
		++count;
	}
	if (count == 0)
		return FALSE;

	//
	// Is there anything to avoid? Carrying on as we are must hit something within the horizon. "As we are" is the
	// speed we are doing (a little more, since we may be accelerating; at least a quarter of what we want, so that a
	// unit setting off sees what is right in front of it), not the speed we would like.
	//
	const Coord3D *ourVel = obj->getPhysics() ? obj->getPhysics()->getVelocity() : nullptr;
	Real wanted = speed;
	if (ourVel != nullptr)
	{
		Real doing = WWMath::Sqrtf(ourVel->x*ourVel->x + ourVel->y*ourVel->y) * 1.2f + 0.1f;
		if (doing < 0.25f*wanted)
			doing = 0.25f*wanted;
		if (doing < speed)
			speed = doing;
	}
	Real straightCost = 0.0f;
	Int i;
	Int dbgWorst = -1;
	for (i = 0; i < count; ++i)
	{
		Real cost = collisionCost(timeToCollision(neighbours[i], dirX*speed, dirY*speed));
		if (cost > straightCost)
		{
			straightCost = cost;
			dbgWorst = i;
		}
	}
	if (DEBUG_UNIT != 0 && (UnsignedInt)obj->getID() == DEBUG_UNIT && dbgWorst >= 0)
	{
		const Neighbour& n = neighbours[dbgWorst];
		Real fwd = n.x*dirX + n.y*dirY, lat = -n.x*dirY + n.y*dirX;
		Real vf = n.vx*dirX + n.vy*dirY, vl = -n.vx*dirY + n.vy*dirX;
		printf("DBGSTEER f=%u cost=%.2f n=%d fwd=%.1f lat=%.1f vf=%.2f vl=%.2f r=%.1f stand=%d speed=%.2f steering=%d\n",
			now, straightCost, count, fwd, lat, vf, vl, n.radius, n.standing ? 1 : 0, speed, alreadySteering ? 1 : 0);
	}
	if (straightCost <= 0.0f)
		return FALSE;
	// A far-off, mild threat doesn't start a manoeuvre (at that range the best answer flips with every small change
	// in the picture). One under way ends at a lower mark than it started at, so it does not stop and start.
	if (straightCost < (alreadySteering ? START_COST*RELEASE_SHARE : START_COST))
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
			Real cost = turn.cost * TURN_COST_SCALE;
			if (s == 1) cost += COST_HALF_SPEED;
			if (s == 2) cost += COST_STOP;
			if (committed && turn.side != 0)
				cost += (turn.side == state.side) ? -BONUS_SAME_SIDE : COST_CHANGE_SIDE;
			if (haveLast && t == state.lastTurn && s == state.lastSpeedStep)
				cost -= STICK;
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
			bestSpeedStep = s;
		}
	}

	// FALSE: nothing beats carrying on.
	return !(bestTurn == 0 && bestSpeedFactor >= 1.0f);
}

//-------------------------------------------------------------------------------------------------
Bool steer( Object *obj, AIUpdateInterface *ai, Coord3D& goalPos, Real& speed, State& state )
{
	loadTuning();
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
	Real pushX = 0.0f, pushY = 0.0f;
	Int bestSpeedStep = 0;
	Bool chose = choose(obj, ai, dirX, dirY, speed, state, now, bestTurn, bestSpeedFactor, followCap, pushX, pushY, bestSpeedStep);
	// The turn we want is this frame's choice and nothing else: no reason, no turn.
	Int wantTurn = chose ? bestTurn : 0;
	state.lastTurn = wantTurn;
	state.lastSpeedStep = chose ? bestSpeedStep : 0;
	if (chose && bestTurn > 0)
		state.lastChoiceFrame = now;

	if (pushX != 0.0f || pushY != 0.0f)
	{
		Real pushLen = WWMath::Sqrtf(pushX*pushX + pushY*pushY);
		if (pushLen > SEP_MAX)
		{
			pushX *= SEP_MAX / pushLen;
			pushY *= SEP_MAX / pushLen;
		}
		Coord3D to = *pos;
		to.x += pushX;
		to.y += pushY;
		if (TheAI->pathfinder()->validMovementPosition(FALSE, obj->getLayer(), ai->getLocomotorSet(), &to))
		{
			to.z = TheTerrainLogic->getLayerHeight(to.x, to.y, obj->getLayer());
			obj->setPosition(&to);
			state.pushFrame = now;
		}
	}

	// Remember the side for COMMIT_FRAMES, and ease the applied turn toward the wanted one instead of jumping to
	// it, both on the way in and on the way back to the path direction.
	Real wantCos = 1.0f;
	Real wantSin = 0.0f;
	if (wantTurn > 0)
	{
		const Turn& chosen = TURNS[wantTurn];
		wantCos = chosen.cosA;
		wantSin = chosen.sinA;
		if (chose && bestTurn > 0)
		{
			state.side = chosen.side;
			state.sideFrame = now;
		}
	}
	else if ((now - state.sideFrame) > COMMIT_FRAMES)
	{
		state.side = 0;
	}

	// Easing back toward the path direction (the wanted turn is smaller than the applied one, same side or none).
	Bool letGo = WWMath::Fabs(wantSin) < WWMath::Fabs(state.offSin) && wantSin*state.offSin >= 0.0f;
	Real ease = letGo ? EASE_OUT : EASE;
	state.offCos += (wantCos - state.offCos) * ease;
	state.offSin += (wantSin - state.offSin) * ease;
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
	if (state.offSin != 0.0f || (chose && bestSpeedFactor < 1.0f))
		state.activeFrame = now;
	if (DEBUG_UNIT != 0 && (UnsignedInt)obj->getID() == DEBUG_UNIT)
		printf("DBGOUT f=%u chose=%d turn=%d factor=%.1f offSin=%.3f cap=%.2f pos=%.0f,%.0f id=%u\n", now, chose ? 1 : 0, bestTurn, bestSpeedFactor, state.offSin, followCap > 1000.0f ? -1.0f : followCap, pos->x, pos->y, (UnsignedInt)obj->getID());

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
