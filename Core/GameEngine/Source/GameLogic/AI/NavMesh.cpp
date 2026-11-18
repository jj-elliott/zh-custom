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
#include "GameLogic/MoveRules.h"

#include <stdio.h>

namespace
{
	NavBuild::Mesh s_mesh;

	// Tuning.
	Int BUILD = 0;				// build the mesh at map load. Off until something reads it (M3).
	Int DUMP = 0;					// print the label grid and the mesh statistics to stdout (scripts/navtest.ps1 -Grid draws it).
	Int SIMPLIFY_NUM = 1;	// boundary simplification tolerance in cells, as a fraction.
	Int SIMPLIFY_DEN = 1;

	void loadTuning()
	{
		MoveTune::apply("navmesh.build", BUILD);
		MoveTune::apply("navmesh.dump", DUMP);
		MoveTune::apply("navmesh.simplifyNum", SIMPLIFY_NUM);
		MoveTune::apply("navmesh.simplifyDen", SIMPLIFY_DEN);
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
}

//-------------------------------------------------------------------------------------------------
const NavBuild::Mesh& NavMesh::mesh()
{
	return s_mesh;
}

//-------------------------------------------------------------------------------------------------
void NavMesh::rebuild( Pathfinder *pathfinder )
{
	loadTuning();
	if (!BUILD && !DUMP)
		return;

	const ICoord2D *hi = pathfinder->getExtent();
	Int width = hi->x + 1;
	Int height = hi->y + 1;
	if (width <= 0 || height <= 0)
		return;
	std::vector<unsigned char> labels(width * height);
	Int x, y;
	for (y = 0; y < height; ++y)
	{
		for (x = 0; x < width; ++x)
			labels[y*width + x] = labelOf(pathfinder->getCell(LAYER_GROUND, x, y));
	}
	NavBuild::buildMesh(&labels[0], width, height, SIMPLIFY_NUM, SIMPLIFY_DEN, s_mesh);

	if (DUMP)
	{
		std::vector<char> report;
		Int violations = NavBuild::validate(s_mesh, report);
		printf("NavMesh: grid=%dx%d verts=%d tris=%d chains=%d points=%d->%d reverted=%d violations=%d\n", width, height,
			(Int)s_mesh.verts.size(), (Int)s_mesh.tris.size(), s_mesh.stats.chains, s_mesh.stats.exactPoints,
			s_mesh.stats.keptPoints, s_mesh.stats.revertedChains, violations);
		printf("NavGrid: %d %d\n", width, height);
		std::vector<char> row(width + 1);
		for (y = 0; y < height; ++y)
		{
			for (x = 0; x < width; ++x)
				row[x] = (char)('0' + labels[y*width + x]);
			row[width] = 0;
			printf("NavGridRow: %s\n", &row[0]);
		}
	}
}
