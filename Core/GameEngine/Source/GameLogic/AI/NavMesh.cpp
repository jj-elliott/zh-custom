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

#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/MoveRules.h"

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

	inline Bool enabled() { return BUILD || DUMP || LOG || CHECK; }

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
	if (s_labels.empty())
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
	if (s_labels.empty())
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
