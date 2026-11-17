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

// zh-pipeline @feature 2026-11-16 Movement benchmark, see MoveBench.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/MoveBench.h"

#include "Common/GameState.h"
#include "Common/Geometry.h"
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "Common/KindOf.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/MoveRules.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"	// NO_MAX_SHOTS_LIMIT

#include <stdio.h>
#include <vector>

namespace
{
	struct Order
	{
		UnsignedInt offset;	// frames after the first order.
		Bool attackMove;
		Coord3D dest;
		std::vector<ObjectID> ids;
		Bool issued;
	};

	struct Tracked
	{
		ObjectID id;
		Bool ordered;						// has received its (first) order.
		UnsignedInt orderFrame;
		UnsignedInt lastMovingFrame;
		Coord3D startPos, lastPos;
		Real travelled;
		Real lastHeading;
		Int lastTurnSign;
		Int reversals;
		UnsignedInt stalledFrames;
		Bool vehicle;
		Real radius;
		Coord3D dest;						// where its (last) order sent the group.
	};

	UnsignedInt s_firstRecordFrame = 0;
	Bool s_haveFirstRecord = false;

	const UnsignedInt REST_FRAMES = LOGICFRAMES_PER_SECOND;				// stopped this long = arrived.
	const UnsignedInt TIME_LIMIT = 180 * LOGICFRAMES_PER_SECOND;	// give up on a course after 3 minutes of game time.
	const Real STANDING_SPEED = 0.05f;
	const Real TURN_EPSILON = 0.01f;	// radians a frame; smaller heading changes are not "turning".
	const Real OVERLAP_SHARE = 0.6f;	// centres closer than this share of the combined radius = overlapping.

	Real unitRadius( const Object *obj )
	{
		const GeometryInfo& geom = obj->getGeometryInfo();
		if (geom.getGeomType() == GEOMETRY_BOX)
			return 0.5f * (geom.getMajorRadius() + geom.getMinorRadius());
		return geom.getMajorRadius();
	}

	// The recorder: called from the logic's order dispatch (through MoveRules::moveOrderHook) for the local player's
	// move and attack-move orders.
	void recordOrder( AIGroup *group, const Coord3D *dest, Bool attackMove )
	{
		if (group == nullptr || dest == nullptr || MoveRules::benchRecordFile()[0] == 0)
			return;
		const VecObjectID& ids = group->getAllIDs();
		if (ids.empty())
			return;
		UnsignedInt now = TheGameLogic->getFrame();
		if (!s_haveFirstRecord)
		{
			s_haveFirstRecord = true;
			s_firstRecordFrame = now;
		}
		FILE *f = fopen(MoveRules::benchRecordFile(), "a");
		if (f == nullptr)
			return;
		fprintf(f, "order %u %d %.3f %.3f %.3f %u", now - s_firstRecordFrame, attackMove ? 1 : 0, dest->x, dest->y, dest->z, (unsigned)ids.size());
		for (size_t i = 0; i < ids.size(); ++i)
			fprintf(f, " %u", (unsigned)ids[i]);
		fprintf(f, "\n");
		fclose(f);
	}

	Bool loadCourse( std::vector<Order>& orders )
	{
		FILE *f = fopen(MoveRules::benchRunFile(), "r");
		if (f == nullptr)
			return FALSE;
		char word[32];
		while (fscanf(f, "%31s", word) == 1)
		{
			if (strcmp(word, "order") != 0)
				break;
			Order o;
			unsigned offset = 0, count = 0;
			int attack = 0;
			float x = 0, y = 0, z = 0;
			if (fscanf(f, "%u %d %f %f %f %u", &offset, &attack, &x, &y, &z, &count) != 6)
				break;
			o.offset = offset;
			o.attackMove = attack != 0;
			o.dest.x = x; o.dest.y = y; o.dest.z = z;
			o.issued = false;
			for (unsigned i = 0; i < count; ++i)
			{
				unsigned id = 0;
				if (fscanf(f, "%u", &id) != 1)
					break;
				o.ids.push_back((ObjectID)id);
			}
			orders.push_back(o);
		}
		fclose(f);
		return !orders.empty();
	}
}

namespace MoveBench
{

void install()
{
	if (MoveRules::benchRecordFile()[0] != 0)
		MoveRules::moveOrderHook = recordOrder;
}

Bool wantsRun()
{
	return MoveRules::benchRunFile()[0] != 0;
}

int run()
{
	std::vector<Order> orders;
	Bool autoCourse = strcmp(MoveRules::benchRunFile(), "auto") == 0;
	if (!autoCourse && !loadCourse(orders))
	{
		printf("MoveBench: cannot read course \"%s\"\n", MoveRules::benchRunFile());
		return 2;
	}
	if (TheGlobalData->m_loadSaveGame.isEmpty())
	{
		printf("MoveBench: no save given (-loadsave <file>)\n");
		return 2;
	}
	TheGameState->loadQueuedSaveGame();
	if (!TheGameLogic->isInGame())
	{
		printf("MoveBench: the save did not load\n");
		return 2;
	}
	if (TheGameLogic->isGamePaused())
		TheGameLogic->setGamePaused(FALSE, FALSE, FALSE);

	// A game loaded from a save draws its random numbers from wherever the generators happen to stand, which is not
	// the same from one process to the next. Fix them, so that the same build, save, course and tuning always print
	// the same line: without that, a difference between two tunings could be luck.
	// "-movetune bench.seed=N" gives variant N of the course: another random seed, and every destination moved by
	// up to three cells. One run of a chaotic system says little; a tuning is judged on the average over variants.
	Int seed = 1;
	MoveTune::apply("bench.seed", seed);
	InitRandom((UnsignedInt)seed);
	Real shiftX = (Real)(((seed * 37) % 7) - 3) * PATHFIND_CELL_SIZE_F;
	Real shiftY = (Real)(((seed * 53) % 7) - 3) * PATHFIND_CELL_SIZE_F;
	if (seed == 1)
	{
		shiftX = 0.0f;	// variant 1 is the course exactly as recorded.
		shiftY = 0.0f;
	}

	// Let the loaded game settle for a second, and make sure the clock runs.
	UnsignedInt before = TheGameLogic->getFrame();
	for (Int i = 0; i < LOGICFRAMES_PER_SECOND; ++i)
		TheGameLogic->UPDATE();
	if (TheGameLogic->getFrame() == before)
	{
		printf("MoveBench: the loaded game does not advance\n");
		return 2;
	}

	// "-movebench auto": no recorded course; send every combat ground unit the local player has to the middle of the
	// map in one order. Crude, but it needs nothing from a person and is the same every time.
	if (autoCourse)
	{
		Order all;
		all.offset = 0;
		all.attackMove = false;
		all.issued = false;
		Region3D extent;
		TheTerrainLogic->getExtent(&extent);
		all.dest.x = 0.5f * (extent.lo.x + extent.hi.x);
		all.dest.y = 0.5f * (extent.lo.y + extent.hi.y);
		all.dest.z = TheTerrainLogic->getGroundHeight(all.dest.x, all.dest.y);
		const Player *local = ThePlayerList->getLocalPlayer();
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		{
			AIUpdateInterface *ai = obj->getAI();
			if (ai == nullptr || obj->getControllingPlayer() != local || obj->isEffectivelyDead())
				continue;
			if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_IMMOBILE) || obj->isKindOf(KINDOF_AIRCRAFT)
					|| obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_PROJECTILE))
				continue;
			if (!obj->isKindOf(KINDOF_VEHICLE) && !obj->isKindOf(KINDOF_INFANTRY))
				continue;
			if (!ai->isDoingGroundMovement() || obj->getContainedBy() != nullptr)
				continue;
			all.ids.push_back(obj->getID());
		}
		orders.push_back(all);
	}

	std::vector<Tracked> units;
	size_t o;
	for (o = 0; o < orders.size(); ++o)
	{
		orders[o].dest.x += shiftX;
		orders[o].dest.y += shiftY;
	}
	for (o = 0; o < orders.size(); ++o)
	{
		for (size_t i = 0; i < orders[o].ids.size(); ++i)
		{
			ObjectID id = orders[o].ids[i];
			Bool known = false;
			for (size_t u = 0; u < units.size(); ++u)
				if (units[u].id == id) { known = true; break; }
			Object *obj = TheGameLogic->findObjectByID(id);
			if (known || obj == nullptr || obj->getAI() == nullptr)
				continue;
			Tracked t;
			t.id = id;
			t.ordered = false;
			t.orderFrame = 0;
			t.lastMovingFrame = 0;
			t.startPos = *obj->getPosition();
			t.lastPos = t.startPos;
			t.travelled = 0.0f;
			t.lastHeading = obj->getOrientation();
			t.lastTurnSign = 0;
			t.reversals = 0;
			t.stalledFrames = 0;
			t.vehicle = !obj->isKindOf(KINDOF_INFANTRY);
			t.radius = unitRadius(obj);
			t.dest = t.startPos;
			units.push_back(t);
		}
	}
	if (units.empty())
	{
		printf("MoveBench: none of the course's units exist in this save\n");
		return 2;
	}

	UnsignedInt startFrame = TheGameLogic->getFrame();
	UnsignedInt lastOffset = orders[orders.size()-1].offset;
	UnsignedInt overlapPairFrames = 0;
	Bool timedOut = false;
	for (;;)
	{
		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt elapsed = now - startFrame;

		// Give the orders that are due.
		for (o = 0; o < orders.size(); ++o)
		{
			Order& order = orders[o];
			if (order.issued || order.offset > elapsed)
				continue;
			order.issued = true;
			AIGroupPtr group = TheAI->createGroup();
			for (size_t i = 0; i < order.ids.size(); ++i)
			{
				Object *obj = TheGameLogic->findObjectByID(order.ids[i]);
				if (obj == nullptr || obj->isEffectivelyDead())
					continue;
				group->add(obj);
				for (size_t u = 0; u < units.size(); ++u)
				{
					if (units[u].id == order.ids[i])
					{
						units[u].dest = order.dest;
						if (!units[u].ordered)
						{
							units[u].ordered = true;
							units[u].orderFrame = now;
							units[u].lastMovingFrame = now;
						}
					}
				}
			}
			if (order.attackMove)
				group->groupAttackMoveToPosition(&order.dest, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER);
			else
				group->groupMoveToPosition(&order.dest, false, CMD_FROM_PLAYER);
		}

		TheGameLogic->UPDATE();
		now = TheGameLogic->getFrame();
		elapsed = now - startFrame;

		// Measure.
		Bool anyBusy = false;
		size_t u;
		for (u = 0; u < units.size(); ++u)
		{
			Tracked& t = units[u];
			Object *obj = TheGameLogic->findObjectByID(t.id);
			if (obj == nullptr || obj->isEffectivelyDead() || !t.ordered)
				continue;
			AIUpdateInterface *ai = obj->getAI();
			const Coord3D *pos = obj->getPosition();
			Real dx = pos->x - t.lastPos.x;
			Real dy = pos->y - t.lastPos.y;
			Real moved = WWMath::Sqrtf(dx*dx + dy*dy);
			t.travelled += moved;
			t.lastPos = *pos;

			Bool moving = ai != nullptr && ai->isMoving();
			if (moving)
			{
				t.lastMovingFrame = now;
				if (moved < STANDING_SPEED)
					++t.stalledFrames;
			}
			if (now - t.lastMovingFrame < REST_FRAMES)
				anyBusy = true;

			Real heading = obj->getOrientation();
			Real turn = heading - t.lastHeading;
			if (turn > PI) turn -= 2.0f*PI;
			if (turn < -PI) turn += 2.0f*PI;
			t.lastHeading = heading;
			if (moving && WWMath::Fabs(turn) > TURN_EPSILON)
			{
				Int sign = (turn > 0.0f) ? 1 : -1;
				if (t.lastTurnSign != 0 && sign != t.lastTurnSign)
					++t.reversals;
				t.lastTurnSign = sign;
			}
		}
		for (u = 0; u < units.size(); ++u)
		{
			if (!units[u].vehicle)
				continue;
			Object *a = TheGameLogic->findObjectByID(units[u].id);
			if (a == nullptr || a->isEffectivelyDead())
				continue;
			for (size_t v = u + 1; v < units.size(); ++v)
			{
				if (!units[v].vehicle)
					continue;
				Object *b = TheGameLogic->findObjectByID(units[v].id);
				if (b == nullptr || b->isEffectivelyDead())
					continue;
				Real dx = a->getPosition()->x - b->getPosition()->x;
				Real dy = a->getPosition()->y - b->getPosition()->y;
				Real reach = OVERLAP_SHARE * (units[u].radius + units[v].radius);
				if (dx*dx + dy*dy < reach*reach)
					++overlapPairFrames;
			}
		}

		if (elapsed > lastOffset && !anyBusy)
			break;
		if (elapsed > lastOffset + TIME_LIMIT)
		{
			timedOut = true;
			break;
		}
	}

	// Report.
	Real sumArrive = 0.0f, maxArrive = 0.0f, sumDetour = 0.0f, sumShort = 0.0f;
	Int reversals = 0, counted = 0, detourCounted = 0;
	UnsignedInt stalled = 0;
	for (size_t u = 0; u < units.size(); ++u)
	{
		const Tracked& t = units[u];
		if (!t.ordered)
			continue;
		++counted;
		Real arrive = (Real)(t.lastMovingFrame - t.orderFrame) / (Real)LOGICFRAMES_PER_SECOND;
		sumArrive += arrive;
		if (arrive > maxArrive)
			maxArrive = arrive;
		reversals += t.reversals;
		stalled += t.stalledFrames;
		Real gx = t.lastPos.x - t.dest.x;
		Real gy = t.lastPos.y - t.dest.y;
		sumShort += WWMath::Sqrtf(gx*gx + gy*gy);
		Real sx = t.lastPos.x - t.startPos.x;
		Real sy = t.lastPos.y - t.startPos.y;
		Real straight = WWMath::Sqrtf(sx*sx + sy*sy);
		if (straight > 1.0f)
		{
			sumDetour += t.travelled / straight;
			++detourCounted;
		}
	}
	if (counted == 0)
		counted = 1;
	printf("MoveBench: units=%d meanArrive=%.2f maxArrive=%.2f goalDist=%.1f overlap=%.2f reversals=%.2f stalled=%.2f detour=%.3f timeout=%d frames=%u level=%d\n",
		counted, sumArrive / counted, maxArrive, sumShort / counted,
		(Real)overlapPairFrames / (Real)LOGICFRAMES_PER_SECOND,
		(Real)reversals / counted,
		(Real)stalled / (Real)LOGICFRAMES_PER_SECOND,
		detourCounted ? sumDetour / detourCounted : 1.0f,
		timedOut ? 1 : 0, TheGameLogic->getFrame() - startFrame, MoveRules::getLevel());
	fflush(stdout);
	return timedOut ? 1 : 0;
}

}
