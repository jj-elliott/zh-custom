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

	// ---- Bridges: several meshes searched as one (docs/PLAN.md, "Bridges in the mesh").
	//
	// A bridge is a second level over the same piece of map, so one flat mesh cannot hold it. Each bridge gets a
	// "sheet": a mesh of a window of the map as a unit on that bridge sees it (NavBuild::erodeSheet), the deck where
	// there is one and the ground everywhere else in the window. The ground's own mesh is sheet 0.
	// Ground inside a window is seen by two sheets. NavBuild::assignSheets gives every piece of ground to exactly one
	// of them, so the sheets do not overlap: they meet along grid lines, each mesh keeps those lines exactly, and
	// connect() joins every pair of triangle edges, one from each side, along the stretch they share ("links").
	// A corridor of the search is then a strip of triangles that crosses edges and links, the funnel pulls one string
	// through all of it, and a path over a bridge is taut from the ground on one bank to the ground on the other.
	// Sheet meshes have the labels NavBuild::SHEET_GROUND / SHEET_WALL / SHEET_DECK / SHEET_OTHER.
	struct Sheet
	{
		const NavBuild::Mesh *mesh;
		int ox, oy;		///< where the mesh's (0,0) is on the atlas's lattice (sheet 0's)
	};

	/// Two ground triangles of different sheets on opposite sides of a grid line, and the stretch a-b of it that
	/// their edges share. Triangles are numbered across all sheets (Atlas::triBase).
	struct Link
	{
		int triA, edgeA;
		int triB, edgeB;
		Pt a, b;	///< along A's edge: A is on the left of a->b
	};

	struct Atlas
	{
		std::vector<Sheet> sheets;					///< 0 is the ground: the whole map
		std::vector<Link> links;
		std::vector<int> triBase;						///< each sheet's first triangle when all sheets' are numbered in a row
		std::vector<int> linkStart, linkOf;	///< triangle g's links: linkOf[linkStart[g] .. linkStart[g+1])
		std::vector<int> component;					///< per triangle: which connected piece of passable ground, -1 for walls

		void clear();
		int addSheet( const NavBuild::Mesh *mesh, int ox, int oy );	///< the mesh must outlive the atlas
		void connect();	///< after the last addSheet: the links and the connected pieces
	};

	struct AtlasPath
	{
		std::vector<Pt> pts;									///< start, corners, goal, and the points where something changes (below)
		std::vector<int> sheet;								///< per point: the sheet the path is on from there to the next point
		std::vector<unsigned char> deck;			///< per point: 1 = from there to the next point it is on that sheet's bridge
		std::vector<unsigned char> inserted;	///< per point: 0 = start, corner or goal; else a point on a straight stretch:
																					///< 1 = the path changes sheet there, 2 = it gets on or off the bridge
		float length;
		int expanded;
		int corridors;
		int fallback;													///< 1 = the search closed every way to a connected goal (rare) and a plain
																					///< corridor's string was used: a valid path, not always the shortest
	};

	enum { PATH_FOUND = 0, PATH_NO_START = 1, PATH_NO_GOAL = 2, PATH_NONE = 3 };

	/// The path of a unit over ground and bridges. startDeck: the bridge sheet whose deck the unit may be standing on
	/// (0: none); startGround: it may be standing on the ground (both can be true at a bridge's end; the bridge then
	/// wins a tie). The same for the goal. An end that is beside the mesh is joined to its nearest point within
	/// 'reach', the goal only to ground connected to the start.
	int findPath( const Atlas& atlas, Pt start, int startDeck, bool startGround, Pt goal, int goalDeck, bool goalGround,
								float reach, AtlasPath& out );
}
