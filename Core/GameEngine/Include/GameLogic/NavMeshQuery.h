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

// zh-pipeline @feature 2026-11-17 Searches on a navigation mesh (docs/PLAN.md Phase 7, "The StarCraft 2 model", M3).
//
//   locate     which triangle holds a point
//   findPath   A* over triangle edges, then the funnel: the taut string from start to goal. The search does not stop
//              at the first way through: it goes on until no unexplored way could be shorter than the best string
//              found (triangles in open ground are huge, so the first corridor is often not the shortest).
//
// A unit's width is not handled here. Each size of unit searches a mesh built from ground already shrunk by its
// radius (NavBuild::erode), on which it is a point: the string is then exactly as far from every wall as the unit
// needs, and "is my centre on the mesh" is the whole wall test. (A funnel that keeps a radius away from corners on
// the unshrunk mesh was built first and thrown away: when a gap is seen at a slant the two circles at its ends swap
// sides, and the funnel then bends the string round the wrong one.)
//
// Who may cross a triangle is a bit mask over labels: bit n set = label n is passable.
//
// Units: the mesh's lattice, single-precision floats. DETERMINISM: only + - * / and sqrt, in a fixed order, compiled
// once into the one exe both machines run, like the rest of the sim's float code. Ties in the open list are broken
// by insertion order.
//
// Standard library only, like NavMeshBuild: tests/navmesh/navtest.cpp checks it against a grid search.

#pragma once

#include "GameLogic/NavMeshBuild.h"

namespace NavQuery
{
	struct Pt
	{
		float x, y;
	};

	struct Path
	{
		std::vector<Pt> pts;	///< start, corners, goal
		float length;
		int expanded;					///< search nodes expanded (cost of the search)
		int corridors;				///< complete corridors that were funnelled before the search could stop
	};

	/// The triangle holding p, preferring one that is passable for mask when p is on a boundary. -1: off the map.
	int locate( const NavBuild::Mesh& mesh, Pt p, unsigned mask );

	/// The point of passable ground nearest p, no further than maxDist (p itself if it is on passable ground), and
	/// its triangle. With 'component' (one entry per triangle), only triangles whose entry equals wantComponent
	/// count: "the nearest point I can actually get to". False if there is none.
	bool nearestPassable( const NavBuild::Mesh& mesh, Pt p, unsigned mask, float maxDist, const int *component,
												int wantComponent, Pt& out, int& tri );

	/// False if either end is not on passable ground or no way exists.
	bool findPath( const NavBuild::Mesh& mesh, Pt start, Pt goal, unsigned mask, Path& out );
}
