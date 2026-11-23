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
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/MoveRules.h"
#include "GameLogic/AIDrive.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/NavMesh.h"
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

	std::vector<ObjectID> s_onWall;	// the units of "onwall" groups: checked every frame to be on the wall top still

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
		Int quickReversals;			// reversals within half a second of the previous one: wobble, as against an S-bend.
		UnsignedInt lastReversalFrame;
		Int revSteer, revStart, revEnd;	// of those: while steering / in the first 5 s / within 200 of the goal.
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

	// ---- Scenarios: a course built from nothing but a text file. ----
	//   map <path inside the game's archives, e.g. Maps\Tournament Desert\Tournament Desert.map>
	//   group                         starts a group; everything below belongs to it until the next "group"
	//   from <waypoint name> | from <x> <y>     where the group is placed (units fill free ground around it)
	//   to <waypoint name> | to <x> <y>         where it is ordered to go
	//   spawn <object template> <count>
	//   at <frames>                   when the group gets its order (default 0)
	//   attackmove                    attack-move instead of move
	//   onwall                        the group stands on a wall top (fortress walls) and is sent along it: "from" and
	//                                 "to" must be over the wall. Nothing walks up onto a wall in this game: maps place
	//                                 units there.
	// Lines starting with # are comments. The map is loaded as a single-player game with nobody on it, so there is
	// no base, no enemy and no script getting in the way: only the terrain and the units the file asks for.
	struct SpawnSpec
	{
		AsciiString templateName;
		Int count;
	};
	struct GroupSpec
	{
		Coord3D from, to;
		AsciiString fromName, toName;
		UnsignedInt at;
		Bool attackMove;
		Bool onWall;
		std::vector<SpawnSpec> spawns;
	};
	struct Scenario
	{
		AsciiString map;
		std::vector<GroupSpec> groups;
	};

	Bool isScenarioFile( const char *path )
	{
		size_t n = strlen(path);
		return n > 9 && _stricmp(path + n - 9, ".scenario") == 0;
	}

	void parsePlace( const char *rest, Coord3D& pos, AsciiString& name )
	{
		float x = 0, y = 0;
		if (sscanf(rest, "%f %f", &x, &y) == 2)
		{
			pos.x = x; pos.y = y; pos.z = 0;
			name.clear();
		}
		else
		{
			name = rest;
			name.trim();
		}
	}

	Bool loadScenario( Scenario& sc )
	{
		FILE *f = fopen(MoveRules::benchRunFile(), "r");
		if (f == nullptr)
			return FALSE;
		char line[512];
		while (fgets(line, sizeof(line), f))
		{
			size_t n = strlen(line);
			while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ' || line[n-1] == '\t'))
				line[--n] = 0;
			const char *p = line;
			while (*p == ' ' || *p == '\t') ++p;
			if (*p == 0 || *p == '#')
				continue;
			if (strncmp(p, "map ", 4) == 0)
			{
				sc.map = p + 4;
				sc.map.trim();
			}
			else if (strcmp(p, "group") == 0)
			{
				GroupSpec g;
				g.from.zero();
				g.to.zero();
				g.at = 0;
				g.attackMove = false;
				g.onWall = false;
				sc.groups.push_back(g);
			}
			else if (sc.groups.empty())
			{
				continue;
			}
			else if (strncmp(p, "from ", 5) == 0)
			{
				parsePlace(p + 5, sc.groups.back().from, sc.groups.back().fromName);
			}
			else if (strncmp(p, "to ", 3) == 0)
			{
				parsePlace(p + 3, sc.groups.back().to, sc.groups.back().toName);
			}
			else if (strncmp(p, "at ", 3) == 0)
			{
				sc.groups.back().at = (UnsignedInt)atoi(p + 3);
			}
			else if (strcmp(p, "attackmove") == 0)
			{
				sc.groups.back().attackMove = true;
			}
			else if (strcmp(p, "onwall") == 0)
			{
				sc.groups.back().onWall = true;
			}
			else if (strncmp(p, "spawn ", 6) == 0)
			{
				char name[128];
				int count = 1;
				if (sscanf(p + 6, "%127s %d", name, &count) >= 1)
				{
					SpawnSpec s;
					s.templateName = name;
					s.count = count;
					sc.groups.back().spawns.push_back(s);
				}
			}
		}
		fclose(f);
		return sc.map.isNotEmpty() && !sc.groups.empty();
	}

	Bool resolvePlace( Coord3D& pos, const AsciiString& name )
	{
		if (name.isEmpty())
		{
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			return TRUE;
		}
		Waypoint *way = TheTerrainLogic->getWaypointByName(name);
		if (way == nullptr)
		{
			printf("MoveBench: no waypoint \"%s\" on this map\n", name.str());
			return FALSE;
		}
		pos = *way->getLocation();
		return TRUE;
	}

	// Put the scenario's units on the map and turn its groups into orders.
	Bool populateScenario( Scenario& sc, std::vector<Order>& orders )
	{
		Player *owner = ThePlayerList->getLocalPlayer();
		if (owner == nullptr)
			owner = ThePlayerList->getNeutralPlayer();
		Team *team = owner->getDefaultTeam();

		// Places around a point, nearest first, 2.5 cells apart; each unit takes the first one it can stand on.
		const Int RINGS = 9;
		const Real SPACING = 2.5f * PATHFIND_CELL_SIZE_F;
		std::vector<ICoord2D> slots;
		for (Int r = 0; r <= RINGS; ++r)
			for (Int j = -r; j <= r; ++j)
				for (Int i = -r; i <= r; ++i)
					if (i == -r || i == r || j == -r || j == r)
					{
						ICoord2D c;
						c.x = i;
						c.y = j;
						slots.push_back(c);
					}

		for (size_t g = 0; g < sc.groups.size(); ++g)
		{
			GroupSpec& group = sc.groups[g];
			if (!resolvePlace(group.from, group.fromName) || !resolvePlace(group.to, group.toName))
				return FALSE;
			Real dx = group.to.x - group.from.x;
			Real dy = group.to.y - group.from.y;
			Real facing = (dx != 0.0f || dy != 0.0f) ? WWMath::Atan2(dy, dx) : 0.0f;

			Order order;
			order.offset = group.at;
			order.attackMove = group.attackMove;
			order.dest = group.to;
			order.issued = false;
			if (group.onWall)
			{
				// A destination is on the wall if it is at the wall's height (TerrainLogic::getLayerForDestination).
				if (!TheAI->pathfinder()->isPointOnWall(&group.from) || !TheAI->pathfinder()->isPointOnWall(&group.to))
				{
					printf("MoveBench: group %u is \"onwall\" but its from or to is not on a wall top\n", (unsigned)(g + 1));
					return FALSE;
				}
				order.dest.z = TheAI->pathfinder()->getWallHeight();
			}
			size_t nextSlot = 0;
			for (size_t s = 0; s < group.spawns.size(); ++s)
			{
				const ThingTemplate *tmpl = TheThingFactory->findTemplate(group.spawns[s].templateName);
				if (tmpl == nullptr)
				{
					printf("MoveBench: no object template \"%s\"\n", group.spawns[s].templateName.str());
					return FALSE;
				}
				for (Int n = 0; n < group.spawns[s].count; ++n)
				{
					Object *obj = TheThingFactory->newObject(tmpl, team);
					if (obj == nullptr || obj->getAI() == nullptr)
					{
						printf("MoveBench: \"%s\" is not a unit that can be ordered about\n", group.spawns[s].templateName.str());
						return FALSE;
					}
					if (n == 0)
					{
						// One line per kind of unit: the numbers its movement is made of (distances per logic frame,
						// radians per frame). The drive model (PLAN.md, M4) is designed from these.
						Int dumpLoco = 0;
						MoveTune::apply("bench.loco", dumpLoco);
						AIUpdateInterface *spawnedAi = obj->getAIUpdateInterface();
						Locomotor *loco = spawnedAi ? spawnedAi->getCurLocomotor() : nullptr;
						if (dumpLoco && loco != nullptr)
						{
							const GeometryInfo& geom = obj->getGeometryInfo();
							printf("MoveBenchLoco: unit=%s loco=%s appearance=%d speed=%.3f turn=%.4f accel=%.4f brake=%.4f minSpeed=%.3f minTurnSpeed=%.3f "
										 "backward=%d pivot=%.1f closeEnough=%.1f ultraAccurate=%d major=%.1f minor=%.1f box=%d infantry=%d\n",
								group.spawns[s].templateName.str(), loco->getTemplateName().str(), (Int)loco->getAppearance(),
								loco->getMaxSpeedForCondition(BODY_PRISTINE), loco->getMaxTurnRate(BODY_PRISTINE),
								loco->getMaxAcceleration(BODY_PRISTINE), loco->getBraking(), loco->getMinSpeed(), loco->getMinTurnSpeed(),
								loco->canMoveBackwards() ? 1 : 0, loco->getTurnPivotOffset(), loco->getCloseEnoughDist(),
								loco->isUltraAccurate() ? 1 : 0, geom.getMajorRadius(), geom.getMinorRadius(),
								geom.getGeomType() == GEOMETRY_BOX ? 1 : 0, obj->isKindOf(KINDOF_INFANTRY) ? 1 : 0);
						}
					}
					Bool placed = false;
					for (; nextSlot < slots.size() && !placed; ++nextSlot)
					{
						Coord3D pos = group.from;
						// (On a wall top places are a cell apart: it is a few cells wide.)
						pos.x += slots[nextSlot].x * (group.onWall ? PATHFIND_CELL_SIZE_F : SPACING);
						pos.y += slots[nextSlot].y * (group.onWall ? PATHFIND_CELL_SIZE_F : SPACING);
						if (group.onWall)
						{
							if (!TheAI->pathfinder()->isPointOnWall(&pos))
								continue;
							pos.z = TheAI->pathfinder()->getWallHeight();
							obj->setPosition(&pos);
							obj->setLayer(LAYER_WALL);
							obj->setOrientation(facing);
							s_onWall.push_back(obj->getID());
							placed = true;
							continue;
						}
						if (!TheAI->pathfinder()->validMovementPosition(FALSE, LAYER_GROUND, obj->getAI()->getLocomotorSet(), &pos))
							continue;
						pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
						obj->setPosition(&pos);
						obj->setOrientation(facing);
						placed = true;
					}
					if (!placed)
					{
						printf("MoveBench: no room around the start for all of group %u\n", (unsigned)(g + 1));
						return FALSE;
					}
					order.ids.push_back(obj->getID());
				}
			}
			orders.push_back(order);
		}
		// Orders must be in time order for the run loop's "last offset".
		for (size_t a = 1; a < orders.size(); ++a)
			for (size_t b = a; b > 0 && orders[b].offset < orders[b-1].offset; --b)
			{
				Order tmp = orders[b];
				orders[b] = orders[b-1];
				orders[b-1] = tmp;
			}
		return TRUE;
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
	Bool scenarioCourse = isScenarioFile(MoveRules::benchRunFile());
	Scenario scenario;
	if (scenarioCourse)
	{
		if (!loadScenario(scenario))
		{
			printf("MoveBench: cannot read scenario \"%s\" (needs a map line and at least one group)\n", MoveRules::benchRunFile());
			return 2;
		}
		// Start the map as an empty single-player game, the way "-file <map>" does.
		TheWritableGlobalData->m_pendingFile = scenario.map;
		TheGameLogic->prepareNewGame(GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0);
		TheGameLogic->startNewGame(FALSE);
		if (!TheGameLogic->isInGame())
		{
			printf("MoveBench: map \"%s\" did not load\n", scenario.map.str());
			return 2;
		}
	}
	else if (!autoCourse && !loadCourse(orders))
	{
		printf("MoveBench: cannot read course \"%s\"\n", MoveRules::benchRunFile());
		return 2;
	}
	if (!scenarioCourse)
	{
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
	UnsignedInt debugUnit = 0;	// "-movetune debug.unit=<object id>": trace that unit (scripts/movetrace.ps1).
	MoveTune::apply("debug.unit", debugUnit);
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

	if (scenarioCourse)
	{
		if (!populateScenario(scenario, orders))
			return 2;
		// One more second for the new units to settle into their idle states and claim their cells.
		for (Int i = 0; i < LOGICFRAMES_PER_SECOND; ++i)
			TheGameLogic->UPDATE();
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
			t.revSteer = t.revStart = t.revEnd = 0;
			t.quickReversals = 0;
			t.lastReversalFrame = 0;
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
	UnsignedInt movingFrames = 0, steerFrames = 0, pushFrames = 0;	// unit-frames under way, and of those with avoidance bending the aim or cutting speed.
	UnsignedInt ovMM = 0, ovMS = 0, ovSS = 0, ovCross = 0;	// breakdown: both moving / one / neither; pairs with different orders.
	Bool timedOut = false;
	// Test hooks, in frames after the first order (-1 = never):
	//   bench.bridgesDown / bench.bridgesUp   every bridge becomes impassable / passable again for the pathfinder, as
	//                                         when one is destroyed / repaired (Pathfinder::changeBridgeState)
	//   bench.reorder                         every order given so far is given again (units get new paths from
	//                                         wherever they are: on a bridge, for one)
	//   bench.wallDown                        the walk-on wall piece nearest the last order's destination is destroyed
	Int bridgesDown = -1, bridgesUp = -1, reorder = -1, wallDown = -1;
	UnsignedInt offWallFrames = 0, wallDeaths = 0;
	MoveTune::apply("bench.wallDown", wallDown);
	MoveTune::apply("bench.bridgesDown", bridgesDown);
	MoveTune::apply("bench.bridgesUp", bridgesUp);
	MoveTune::apply("bench.reorder", reorder);
	for (;;)
	{
		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt elapsed = now - startFrame;

		if ((bridgesDown >= 0 && elapsed == (UnsignedInt)bridgesDown) || (bridgesUp >= 0 && elapsed == (UnsignedInt)bridgesUp))
		{
			Bool up = (bridgesUp >= 0 && elapsed == (UnsignedInt)bridgesUp);
			Int count = 0;
			for (Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge != nullptr; bridge = bridge->getNext())
			{
				if (bridge->getLayer() > LAYER_GROUND)
				{
					TheAI->pathfinder()->changeBridgeState(bridge->getLayer(), up);
					++count;
				}
			}
			printf("MoveBenchBridges: frame=%u %d bridges %s\n", elapsed, count, up ? "up" : "down");
		}
		if (reorder >= 0 && elapsed == (UnsignedInt)reorder)
		{
			Int onBridge = 0;
			for (o = 0; o < orders.size(); ++o)
			{
				orders[o].issued = false;
				for (size_t i = 0; i < orders[o].ids.size(); ++i)
				{
					Object *obj = TheGameLogic->findObjectByID(orders[o].ids[i]);
					if (obj != nullptr && obj->getLayer() > LAYER_GROUND)
						++onBridge;
				}
			}
			printf("MoveBenchReorder: frame=%u unitsOnABridge=%d\n", elapsed, onBridge);
		}

		if (wallDown >= 0 && elapsed == (UnsignedInt)wallDown)
		{
			const Coord3D& around = orders[orders.size()-1].dest;
			Object *piece = nullptr;
			Real best = 0.0f;
			for (Object *w = TheGameLogic->getFirstObject(); w; w = w->getNextObject())
			{
				if (!w->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL) || w->isEffectivelyDead())
					continue;
				Real d = sqr(w->getPosition()->x - around.x) + sqr(w->getPosition()->y - around.y);
				if (piece == nullptr || d < best)
				{
					piece = w;
					best = d;
				}
			}
			if (piece != nullptr)
			{
				printf("MoveBenchWall: frame=%u destroying %s at (%.0f, %.0f)\n", elapsed, piece->getTemplate()->getName().str(),
					piece->getPosition()->x, piece->getPosition()->y);
				// (These pieces have no health to lose: taken off the map the way a script would.)
				TheGameLogic->destroyObject(piece);
			}
		}

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
		// Units put on a wall top must still be on it: on the wall's layer, over the top, at its height.
		for (size_t wi = 0; wi < s_onWall.size(); ++wi)
		{
			Object *onWall = TheGameLogic->findObjectByID(s_onWall[wi]);
			if (onWall == nullptr || onWall->isEffectivelyDead())
			{
				++wallDeaths;
				continue;
			}
			if (onWall->getLayer() != LAYER_WALL || !TheAI->pathfinder()->isPointOnWall(onWall->getPosition())
					|| fabs(onWall->getPosition()->z - TheAI->pathfinder()->getWallHeight()) > 1.0f)
			{
				if (offWallFrames < 6)
					printf("MoveBenchWall: frame=%u unit %u off the wall top: layer=%d at (%.1f, %.1f, %.1f), over the top=%d, wall height %.1f\n",
						elapsed, (unsigned)onWall->getID(), (Int)onWall->getLayer(), onWall->getPosition()->x, onWall->getPosition()->y,
						onWall->getPosition()->z, TheAI->pathfinder()->isPointOnWall(onWall->getPosition()) ? 1 : 0,
						TheAI->pathfinder()->getWallHeight());
				++offWallFrames;
			}
		}

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
				++movingFrames;
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
			if (debugUnit != 0 && (UnsignedInt)t.id == debugUnit)
				printf("DBGPOS f=%u x=%.1f y=%.1f heading=%.3f turn=%.4f moved=%.2f moving=%d\n", now, pos->x, pos->y, heading, turn, moved, moving ? 1 : 0);
			if (moving && WWMath::Fabs(turn) > TURN_EPSILON)
			{
				Int sign = (turn > 0.0f) ? 1 : -1;
				if (t.lastTurnSign != 0 && sign != t.lastTurnSign)
				{
					++t.reversals;
					if (t.lastReversalFrame != 0 && now - t.lastReversalFrame <= LOGICFRAMES_PER_SECOND/2)
						++t.quickReversals;
					t.lastReversalFrame = now;
					Real gx = pos->x - t.dest.x;
					Real gy = pos->y - t.dest.y;
					if (now - t.orderFrame < 5*LOGICFRAMES_PER_SECOND)
						++t.revStart;
					else if (gx*gx + gy*gy < 200.0f*200.0f)
						++t.revEnd;
				}
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
				{
					++overlapPairFrames;
					Int movers = ((a->getAI() && a->getAI()->isMoving()) ? 1 : 0) + ((b->getAI() && b->getAI()->isMoving()) ? 1 : 0);
					if (movers == 2) ++ovMM; else if (movers == 1) ++ovMS; else ++ovSS;
					{
						// "-movetune bench.ovlog=1": which pairs, when (one line a pair each second).
						static Int ovLog = -1;
						if (ovLog < 0) { ovLog = 0; MoveTune::apply("bench.ovlog", ovLog); }
						if (ovLog && (TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND) == 0)
							printf("MoveBenchOverlap: f=%u a=%u b=%u movers=%d dist=%.1f reach=%.1f\n", TheGameLogic->getFrame(),
								(unsigned)units[u].id, (unsigned)units[v].id, movers, WWMath::Sqrtf(dx*dx + dy*dy), reach);
					}
					if (units[u].dest.x != units[v].dest.x || units[u].dest.y != units[v].dest.y)
						++ovCross;
				}
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
	Int reversals = 0, counted = 0, detourCounted = 0, revSteer = 0, revStart = 0, revEnd = 0, wobble = 0;
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
		wobble += t.quickReversals;
		printf("MoveBenchUnit: id=%u vehicle=%d radius=%.1f reversals=%d revSteer=%d stalled=%u lastMoving=%u\n", (unsigned)t.id, t.vehicle ? 1 : 0,
			t.radius, t.reversals, t.revSteer, t.stalledFrames, t.lastMovingFrame);
		revSteer += t.revSteer;
		revStart += t.revStart;
		revEnd += t.revEnd;
		stalled += t.stalledFrames;
		Real gx = t.lastPos.x - t.dest.x;
		Real gy = t.lastPos.y - t.dest.y;
		sumShort += WWMath::Sqrtf(gx*gx + gy*gy);
		// Standing still is not arriving. A unit that has stopped this far from where its group was sent (a group's
		// members stand round the spot, not on it) has given up, and the run counts as timed out.
		if (gx*gx + gy*gy > 400.0f * 400.0f)
			timedOut = true;
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
	NavMesh::printStats();
	if (!s_onWall.empty())
		printf("MoveBenchWall: units=%d offWallUnitFrames=%u deadUnitFrames=%u\n", (Int)s_onWall.size(), offWallFrames, wallDeaths);
	AIDrive::printStats();
	printf("MoveBench: units=%d meanArrive=%.2f maxArrive=%.2f goalDist=%.1f overlap=%.2f reversals=%.2f stalled=%.2f detour=%.3f timeout=%d frames=%u level=%d ovMM=%.2f ovMS=%.2f ovSS=%.2f ovCross=%.2f revSteer=%.2f revStart=%.2f revEnd=%.2f wobble=%.2f steerPct=%.1f pushPct=%.1f\n",
		counted, sumArrive / counted, maxArrive, sumShort / counted,
		(Real)overlapPairFrames / (Real)LOGICFRAMES_PER_SECOND,
		(Real)reversals / counted,
		(Real)stalled / (Real)LOGICFRAMES_PER_SECOND,
		detourCounted ? sumDetour / detourCounted : 1.0f,
		timedOut ? 1 : 0, TheGameLogic->getFrame() - startFrame, MoveRules::getLevel(),
		(Real)ovMM / (Real)LOGICFRAMES_PER_SECOND, (Real)ovMS / (Real)LOGICFRAMES_PER_SECOND,
		(Real)ovSS / (Real)LOGICFRAMES_PER_SECOND, (Real)ovCross / (Real)LOGICFRAMES_PER_SECOND,
		(Real)revSteer / counted, (Real)revStart / counted, (Real)revEnd / counted, (Real)wobble / counted, movingFrames ? 100.0f * (Real)steerFrames / (Real)movingFrames : 0.0f,
		movingFrames ? 100.0f * (Real)pushFrames / (Real)movingFrames : 0.0f);
	fflush(stdout);
	return timedOut ? 1 : 0;
}

}
