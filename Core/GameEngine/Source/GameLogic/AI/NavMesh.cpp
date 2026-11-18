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


// zh-pipeline @feature 2026-11-17 The game's navigation mesh, see NavMesh.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/NavMesh.h"

#include "Common/GameCommon.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LocomotorSet.h"
#include "GameLogic/MoveRules.h"
#include "GameLogic/NavMeshQuery.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include <stdio.h>

namespace
{
	NavBuild::Mesh s_mesh;
	std::vector<unsigned char> s_labels;	// what the mesh was built from
	std::vector<unsigned char> s_scratch;
	Int s_width = 0;
	Int s_height = 0;
	Bool s_dirty = false;
	Bool s_tuningLoaded = false;

	// Tuning.
	Int BUILD = 0;				// build and maintain the mesh. Off until something reads it (M3).
	Int DUMP = 0;					// print the label grid and the mesh statistics at map load (scripts/navtest.ps1 -Scenario draws it).
	Int LOG = 0;					// print a line for every rebuild after map load.
	Int CHECK = 0;				// every second, compare the grid with what the mesh was built from: proves no change is missed.
	Int SIMPLIFY_NUM = 1;	// boundary simplification tolerance in cells, as a fraction.
	Int SIMPLIFY_DEN = 1;

	void loadTuning()
	{
		MoveTune::apply("navmesh.build", BUILD);
		MoveTune::apply("navmesh.dump", DUMP);
		MoveTune::apply("navmesh.log", LOG);
		MoveTune::apply("navmesh.check", CHECK);
		MoveTune::apply("navmesh.simplifyNum", SIMPLIFY_NUM);
		MoveTune::apply("navmesh.simplifyDen", SIMPLIFY_DEN);
		s_tuningLoaded = true;
	}

	inline Bool wantPlainMesh() { return BUILD || DUMP || LOG || CHECK; }
	inline Bool enabled() { return wantPlainMesh() || MoveRules::navmesh(); }

	// ---- The meshes units search: one per (labels the unit may stand on, its radius in half-cells), built from
	// s_labels on first use and again when s_labels has changed since (s_gridSerial).
	struct SizedMesh
	{
		unsigned mask;
		Int radius;
		UnsignedInt serial;
		NavBuild::Mesh mesh;
		std::vector<int> component;	///< per triangle: which connected piece of passable ground, -1 for walls
	};
	std::vector<SizedMesh *> s_sized;
	UnsignedInt s_gridSerial = 1;
	Int s_statMesh = 0, s_statNoStart = 0, s_statNoGoal = 0, s_statNoPath = 0, s_statBuilds = 0, s_statNodes = 0;

	const Int MAX_RADIUS_HALF_CELLS = 5;

	SizedMesh *sizedMesh( unsigned mask, Int radius )
	{
		SizedMesh *sm = nullptr;
		for (size_t i = 0; i < s_sized.size(); ++i)
		{
			if (s_sized[i]->mask == mask && s_sized[i]->radius == radius)
				sm = s_sized[i];
		}
		if (sm == nullptr)
		{
			sm = new SizedMesh;
			sm->mask = mask;
			sm->radius = radius;
			sm->serial = 0;
			s_sized.push_back(sm);
		}
		if (sm->serial == s_gridSerial)
			return sm;
		sm->serial = s_gridSerial;
		++s_statBuilds;
		std::vector<unsigned char> shrunk;
		NavBuild::erode(&s_labels[0], s_width, s_height, mask, radius, shrunk);
		NavBuild::buildMesh(&shrunk[0], s_width*2, s_height*2, SIMPLIFY_NUM, SIMPLIFY_DEN, sm->mesh);
		// Connected pieces of the passable triangles (label 0 after erode).
		const std::vector<NavBuild::Tri>& tris = sm->mesh.tris;
		sm->component.assign(tris.size(), -1);
		std::vector<int> stack;
		Int pieces = 0;
		for (size_t t = 0; t < tris.size(); ++t)
		{
			if (tris[t].label != 0 || sm->component[t] >= 0)
				continue;
			sm->component[t] = pieces;
			stack.push_back((int)t);
			while (!stack.empty())
			{
				int c = stack.back();
				stack.pop_back();
				for (int i = 0; i < 3; ++i)
				{
					int nb = tris[c].n[i];
					if (nb >= 0 && tris[nb].label == 0 && sm->component[nb] < 0)
					{
						sm->component[nb] = pieces;
						stack.push_back(nb);
					}
				}
			}
			++pieces;
		}
		return sm;
	}

	unsigned char labelOf( const PathfindCell *cell )
	{
		if (cell == nullptr)
			return NavMesh::NAV_BLOCKED;
		switch (cell->getType())
		{
			case PathfindCell::CELL_CLEAR: return NavMesh::NAV_CLEAR;
			case PathfindCell::CELL_WATER: return NavMesh::NAV_WATER;
			case PathfindCell::CELL_CLIFF: return NavMesh::NAV_CLIFF;
			case PathfindCell::CELL_RUBBLE: return NavMesh::NAV_RUBBLE;
			case PathfindCell::CELL_OBSTACLE: return cell->isObstacleFence() ? NavMesh::NAV_FENCE : NavMesh::NAV_BLOCKED;
			default: return NavMesh::NAV_BLOCKED;
		}
	}

	void readLabels( Pathfinder *pathfinder, std::vector<unsigned char>& labels, Int& width, Int& height )
	{
		const ICoord2D *hi = pathfinder->getExtent();
		width = hi->x + 1;
		height = hi->y + 1;
		if (width <= 0 || height <= 0)
		{
			width = height = 0;
			labels.clear();
			return;
		}
		labels.resize(width * height);
		for (Int y = 0; y < height; ++y)
		{
			for (Int x = 0; x < width; ++x)
				labels[y*width + x] = labelOf(pathfinder->getCell(LAYER_GROUND, x, y));
		}
	}

	Int countDifferent( const std::vector<unsigned char>& a, const std::vector<unsigned char>& b )
	{
		Int differ = 0;
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
		{
			if (a[i] != b[i])
				++differ;
		}
		return differ;
	}
}

//-------------------------------------------------------------------------------------------------
const NavBuild::Mesh& NavMesh::mesh()
{
	return s_mesh;
}

//-------------------------------------------------------------------------------------------------
void NavMesh::markDirty()
{
	s_dirty = true;
}

//-------------------------------------------------------------------------------------------------
void NavMesh::rebuild( Pathfinder *pathfinder )
{
	loadTuning();
	s_dirty = false;
	if (!enabled())
		return;

	readLabels(pathfinder, s_labels, s_width, s_height);
	++s_gridSerial;
	if (s_labels.empty() || !wantPlainMesh())
		return;
	NavBuild::buildMesh(&s_labels[0], s_width, s_height, SIMPLIFY_NUM, SIMPLIFY_DEN, s_mesh);

	if (DUMP)
	{
		std::vector<char> report;
		Int violations = NavBuild::validate(s_mesh, report);
		printf("NavMesh: grid=%dx%d verts=%d tris=%d chains=%d points=%d->%d reverted=%d violations=%d\n", s_width, s_height,
			(Int)s_mesh.verts.size(), (Int)s_mesh.tris.size(), s_mesh.stats.chains, s_mesh.stats.exactPoints,
			s_mesh.stats.keptPoints, s_mesh.stats.revertedChains, violations);
		printf("NavGrid: %d %d\n", s_width, s_height);
		std::vector<char> row(s_width + 1);
		for (Int y = 0; y < s_height; ++y)
		{
			for (Int x = 0; x < s_width; ++x)
				row[x] = (char)('0' + s_labels[y*s_width + x]);
			row[s_width] = 0;
			printf("NavGridRow: %s\n", &row[0]);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void NavMesh::update( Pathfinder *pathfinder )
{
	if (!s_tuningLoaded || !enabled())
		return;

	UnsignedInt now = TheGameLogic->getFrame();
	Int w, h;
	if (CHECK && !s_dirty && (now % LOGICFRAMES_PER_SECOND) == 0)
	{
		readLabels(pathfinder, s_scratch, w, h);
		if (s_scratch != s_labels)
		{
			printf("NavMeshStale: frame=%u cells=%d (the grid changed and nobody called NavMesh::markDirty)\n", now,
				countDifferent(s_scratch, s_labels));
			s_dirty = true;
		}
	}
	if (!s_dirty)
		return;
	s_dirty = false;

	// Most footprint calls change nothing a ground unit can tell apart (a re-stamp of the same structure, a mobile
	// object that leaves no footprint): rebuild only if a label really changed.
	readLabels(pathfinder, s_scratch, w, h);
	if (w == s_width && h == s_height && s_scratch == s_labels)
		return;
	Int differ = (w == s_width && h == s_height) ? countDifferent(s_scratch, s_labels) : -1;
	s_labels.swap(s_scratch);
	s_width = w;
	s_height = h;
	++s_gridSerial;	// the sized meshes rebuild themselves when next searched.
	if (s_labels.empty() || !wantPlainMesh())
		return;
	NavBuild::buildMesh(&s_labels[0], s_width, s_height, SIMPLIFY_NUM, SIMPLIFY_DEN, s_mesh);
	if (LOG)
	{
		std::vector<char> report;
		Int violations = NavBuild::validate(s_mesh, report);
		printf("NavMeshRebuild: frame=%u cells=%d tris=%d reverted=%d violations=%d\n", now, differ, (Int)s_mesh.tris.size(),
			s_mesh.stats.revertedChains, violations);
	}
}

//-------------------------------------------------------------------------------------------------
Path *NavMesh::findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to, Int *cost )
{
	if (cost)
		*cost = 0;
	if (s_labels.empty() || obj == nullptr)
		return nullptr;

	// Which labels this unit may stand on.
	LocomotorSurfaceTypeMask surfaces = locomotorSet.getValidSurfaces();
	if (surfaces & LOCOMOTORSURFACE_AIR)
		return nullptr;
	unsigned mask = 0;
	if (surfaces & LOCOMOTORSURFACE_GROUND)
	{
		mask |= 1u << NAV_CLEAR;
		if (obj->getCrusherLevel() > 0)
			mask |= 1u << NAV_FENCE;
	}
	if (surfaces & LOCOMOTORSURFACE_WATER) mask |= 1u << NAV_WATER;
	if (surfaces & LOCOMOTORSURFACE_CLIFF) mask |= 1u << NAV_CLIFF;
	if (surfaces & LOCOMOTORSURFACE_RUBBLE) mask |= 1u << NAV_RUBBLE;
	if (mask == 0)
		return nullptr;

	// Its size, in half-cells, rounded down so that rounding never closes a gap the unit fits through. Infantry
	// are points: they pass through each other and through one-cell gaps, as on the grid.
	const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
	Int radius = 0;
	if (!obj->isKindOf(KINDOF_INFANTRY))
	{
		const GeometryInfo& geom = obj->getGeometryInfo();
		Real r = geom.getMajorRadius();
		if (geom.getGeomType() == GEOMETRY_BOX)
			r = (geom.getMajorRadius() + geom.getMinorRadius()) * 0.5f;
		radius = REAL_TO_INT_FLOOR(r / HALF);
		if (radius > MAX_RADIUS_HALF_CELLS)
			radius = MAX_RADIUS_HALF_CELLS;
		if (radius < 0)
			radius = 0;
	}
	SizedMesh *sm = sizedMesh(mask, radius);
	if (cost)
		*cost = 200;

	// The ends, on the mesh (which is in half-cells). A unit may stand nearer a wall than its mesh allows (it was
	// built there, it was pushed, the grid let it), and so may a destination: join each to the nearest point of
	// the mesh, the destination only to ground connected to where the unit is.
	NavQuery::Pt s, g, s2, g2;
	s.x = from->x / HALF; s.y = from->y / HALF;
	g.x = to->x / HALF;   g.y = to->y / HALF;
	Real reach = (Real)(radius + 4);
	int startTri = -1, goalTri = -1;
	if (!NavQuery::nearestPassable(sm->mesh, s, 1u, reach, nullptr, -1, s2, startTri))
	{
		++s_statNoStart;
		return nullptr;
	}
	if (!NavQuery::nearestPassable(sm->mesh, g, 1u, reach, &sm->component[0], sm->component[startTri], g2, goalTri))
	{
		++s_statNoGoal;
		return nullptr;
	}
	NavQuery::Path found;
	if (!NavQuery::findPath(sm->mesh, s2, g2, 1u, found) || found.pts.size() < 2)
	{
		++s_statNoPath;
		return nullptr;
	}
	++s_statMesh;
	s_statNodes += found.expanded;
	if (cost)
		*cost += found.expanded * 4;

	// As a Path: every node is a corner, so each one's "next optimized" is simply the next.
	std::vector<Coord3D> pts;
	Coord3D c;
	c = *from;
	pts.push_back(c);
	for (size_t k = 0; k < found.pts.size(); ++k)
	{
		c.x = found.pts[k].x * HALF;
		c.y = found.pts[k].y * HALF;
		c.z = TheTerrainLogic->getLayerHeight(c.x, c.y, LAYER_GROUND);
		pts.push_back(c);
	}
	c = *to;
	pts.push_back(c);
	Path *path = newInstance(Path);
	Coord3D last = pts[0];
	path->appendNode(&pts[0], LAYER_GROUND);
	for (size_t k = 1; k < pts.size(); ++k)
	{
		Real dx = pts[k].x - last.x, dy = pts[k].y - last.y;
		Bool isEnd = (k + 1 == pts.size());
		if (dx*dx + dy*dy < 1.0f)
		{
			if (!isEnd)
				continue;
			if (path->getFirstNode() != path->getLastNode())
			{
				path->updateLastNode(&pts[k]);	// the destination itself is the last node, not a point a hair from it.
				continue;
			}
		}
		path->appendNode(&pts[k], LAYER_GROUND);
		last = pts[k];
	}
	for (PathNode *node = path->getFirstNode(); node != nullptr; node = node->getNext())
		node->setNextOptimized(node->getNext());
	path->markOptimized();
	return path;
}

//-------------------------------------------------------------------------------------------------
void NavMesh::printStats()
{
	if (!MoveRules::navmesh())
		return;
	printf("NavMeshPaths: mesh=%d noStart=%d noGoal=%d noPath=%d sizes=%d builds=%d meanNodes=%.1f\n", s_statMesh, s_statNoStart,
		s_statNoGoal, s_statNoPath, (Int)s_sized.size(), s_statBuilds, s_statMesh ? (Real)s_statNodes / (Real)s_statMesh : 0.0f);
}
