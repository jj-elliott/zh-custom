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

// zh-pipeline @feature 2026-11-17 Navigation mesh construction (docs/PLAN.md Phase 7, "The StarCraft 2 model", M1).
//
// Turns a grid of labelled cells (the pathfinder's cell types) into a triangle mesh:
//   1. the boundaries between cells of different label are traced into chains of lattice points;
//   2. each chain is simplified (Douglas-Peucker, tolerance in cells) but never so far that the centre of a cell
//      ends up on the other side of the boundary: the mesh and the grid always agree about every cell, so about
//      what is connected to what. Any chain whose simplified form would touch another goes back to its exact form,
//      so the set of boundaries always stays a planar graph;
//   3. a constrained Delaunay triangulation of the map rectangle is built with every boundary segment as a
//      constrained edge;
//   4. every triangle gets the label of the region it lies in.
//
// One mesh serves every kind of unit: a search treats a triangle as walkable or not by its label (ground, water,
// cliff, rubble, fence, blocked), as the grid pathfinder does with LocomotorSurfaceTypeMask.
//
// DETERMINISM. All coordinates are integers (cell corners) and every geometric decision is an exact 64-bit integer
// predicate: no floating point, no dependence on compiler flags, same mesh on every machine from the same grid.
// The order of every loop is fixed by the grid, never by a hash or a pointer.
//
// This file and its .cpp use only the C++ standard library, so the builder can be compiled and fuzzed on its own
// (tests/navmesh/navtest.cpp, scripts/navtest.ps1) without the engine.

#pragma once

#include <vector>

namespace NavBuild
{
	typedef long long I64;

	struct Vert
	{
		int x, y;	///< cell-corner lattice: world position = (x, y) * cell size
	};

	/// Counter-clockwise triangle. Edge i runs from v[i] to v[(i+1)%3]; n[i] is the triangle across it (-1: map edge);
	/// c[i] is set when that edge is a boundary between regions (or the map edge).
	struct Tri
	{
		int v[3];
		int n[3];
		unsigned char c[3];
		unsigned char label;
	};

	enum { LABEL_NONE = 255 };

	struct Stats
	{
		int chains;					///< boundary chains traced
		int exactPoints;		///< lattice points on all chains before simplification
		int keptPoints;			///< after
		int revertedChains;	///< chains put back to exact form because their simplified form touched another
		int labelConflicts;	///< triangles reached with two different labels (0 in a correct build)
		int unlabelled;			///< triangles no region reached (0 in a correct build)
		Stats() : chains(0), exactPoints(0), keptPoints(0), revertedChains(0), labelConflicts(0), unlabelled(0) {}
	};

	struct Mesh
	{
		int width, height;	///< in cells
		std::vector<Vert> verts;
		std::vector<Tri> tris;
		Stats stats;
	};

	/// Exact predicates. orient > 0: c is to the left of a->b. inCircle > 0: d is strictly inside the circle through
	/// the counter-clockwise triangle a, b, c. Valid for coordinates up to 2^13 in magnitude.
	I64 orient( const Vert& a, const Vert& b, const Vert& c );
	I64 inCircle( const Vert& a, const Vert& b, const Vert& c, const Vert& d );

	/// Constrained Delaunay triangulation of the rectangle [0,w] x [0,h] on the integer lattice.
	class CDT
	{
	public:
		void init( int w, int h );
		int addVertex( int x, int y );						///< returns the vertex index (an existing one if the point is already there)
		void addConstraint( int a, int b );				///< must not properly cross an earlier constraint; may pass through vertices
		int findEdge( int u, int v ) const;				///< triangle*3 + edge for the directed edge u->v, or -1

		std::vector<Vert> verts;
		std::vector<Tri> tris;

	private:
		int locate( int x, int y, int& onEdge );
		void legalize( std::vector<int>& stack );
		void flip( int t, int i );
		void setTri( int t );
		int backIndex( int nb, int t ) const;
		void markConstrained( int u, int v );
		void rotateTo( int t, int i );

		std::vector<int> m_vtri;	///< one triangle touching each vertex
		int m_last;
	};

	/// labels: width*height bytes, row-major (index = y*width + x). simplifyNum/simplifyDen: the tolerance in cells
	/// (0 = keep the grid's boundaries exactly, collinear points removed).
	/// exactLabels: bit n set = a boundary between label 0 and label n is never simplified, so that it stays on the
	/// grid's lines (where two meshes are joined along such a boundary, NavMeshQuery.h, both must have it there).
	void buildMesh( const unsigned char *labels, int width, int height, int simplifyNum, int simplifyDen, Mesh& out,
									unsigned exactLabels = 0 );

	/// The ground as a unit of a given size sees it: where its centre may be. The result has cells of half the size
	/// (2*width x 2*height, row-major), 0 where the centre of a unit of radius 'radiusHalfCells' half-cells may be and
	/// 1 elsewhere: a half-cell is 0 only if its centre is at least that far from every cell the unit may not stand
	/// on and from the map edge. passMask: bit n set = the unit may stand on label n. A mesh built from the result
	/// has one passable label (0), and on it the unit is a point (see NavMeshQuery.h for why width is handled here).
	/// Integer arithmetic only.
	void erode( const unsigned char *labels, int width, int height, unsigned passMask, int radiusHalfCells,
							std::vector<unsigned char>& out );

	/// Labels of a mesh built from eroded ground: what erode() and erodeSheet() write, and SHEET_OTHER, which
	/// assignSheets() adds: ground that is another sheet's (a wall as far as this sheet's mesh goes).
	enum { SHEET_GROUND = 0, SHEET_WALL = 1, SHEET_DECK = 2, SHEET_OTHER = 3 };
	/// The boundaries a sheet's mesh must keep on the grid's lines: buildMesh's exactLabels for a sheet.
	enum { SHEET_EXACT = (1 << SHEET_DECK) | (1 << SHEET_OTHER) };

	/// The same for a bridge's sheet (NavMeshQuery.h, Atlas): a window of the map as a unit on that bridge sees it,
	/// the deck where there is one and the ground everywhere else. labels and deck are width x height cells: the
	/// window with pad[0..3] more cells on its left, bottom, right and top, so that walls just outside the window
	/// shrink the ground inside it (give at least the radius, less only where the map ends); deck[i] != 0 marks the
	/// cells that are the bridge. mapEdges says which sides of that padded grid are the edge of the map (bit 0 left,
	/// 1 bottom, 2 right, 3 top). The result is the window alone in half-cells, (width - pad[0] - pad[2])*2 wide:
	/// SHEET_GROUND, SHEET_WALL, or SHEET_DECK where the unit's centre may be and it is then on the bridge.
	void erodeSheet( const unsigned char *labels, const unsigned char *deck, int width, int height, unsigned passMask,
									 int radiusHalfCells, const int pad[4], unsigned mapEdges, std::vector<unsigned char>& out );

	/// One sheet's half-cells: 'cells' is w x h, row-major, and its (0,0) is half-cell (ox, oy) of the ground sheet.
	struct SheetCells
	{
		int ox, oy, w, h;
		std::vector<unsigned char> cells;
	};

	/// Shares the ground out between the sheets, so that every spot of ground is in exactly one mesh.
	/// In: sheets[0] = the ground (erode), sheets[1..] = one window per bridge (erodeSheet).
	/// A half-cell that a bridge's sheet has as ground goes to that bridge (it is what the bridge's deck opens onto,
	/// and near the bridge's ends only that sheet has it right: the ground sheet sees a wall where the low deck
	/// begins); where two bridges' windows overlap, to the bridge whose deck is nearer. All other ground stays the
	/// ground sheet's. A unit's centre may be wherever any sheet says it may, and that whole area is covered once.
	/// Out: each sheet's cells for buildMesh (pass SHEET_EXACT): SHEET_GROUND only where the ground is this sheet's,
	/// SHEET_OTHER where it is another's, SHEET_DECK and SHEET_WALL as before. A bridge's sheet grows by a ring of
	/// one half-cell (ox, oy, w, h change), so that its ground has a boundary where it meets the ground outside its
	/// window. Integer arithmetic only.
	void assignSheets( std::vector<SheetCells>& sheets );

	/// Checks everything a correct mesh must satisfy; returns the number of violations and describes the first few in
	/// 'report'. Used by the fuzz test and, cheaply, after a build in a debug run.
	int validate( const Mesh& mesh, std::vector<char>& report );
}
