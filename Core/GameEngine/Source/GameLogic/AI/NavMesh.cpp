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
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include <stdio.h>

namespace
{
	NavBuild::Mesh s_mesh;
	std::vector<unsigned char> s_labels;	// what the mesh was built from
	std::vector<unsigned char> s_scratch;

	// A bridge as the pathfinder has it: the cells of its layer over a rectangle of the map. kind: 0 = not on the
	// bridge (a unit on this layer is on the ground there, Pathfinder::getCell falls back to the ground cell),
	// 1 = deck, 2 = on the layer and impassable (the bridge's sides; every cell of a destroyed bridge).
	struct LayerCells
	{
		Int layer;
		Int x0, y0, w, h;
		std::vector<unsigned char> kind;
		bool operator==( const LayerCells& o ) const
		{
			return layer == o.layer && x0 == o.x0 && y0 == o.y0 && w == o.w && h == o.h && kind == o.kind;
		}
	};
	std::vector<LayerCells> s_layers, s_layerScratch;	// what the meshes were built from, like s_labels

	Int s_width = 0;
	Int s_height = 0;
	Bool s_dirty = false;
	Bool s_tuningLoaded = false;

	// Tuning.
	Int CACHE = 1;				// reuse a route just found for a neighbour going to the same place (see CachedRoute)
	Int BUILD = 0;				// build and maintain the mesh. Off until something reads it (M3).
	Int DUMP = 0;					// print the label grid and the mesh statistics at map load (scripts/navtest.ps1 -Scenario draws it).
	Int LOG = 0;					// print a line for every rebuild after map load.
	Int CHECK = 0;				// every second, compare the grid with what the mesh was built from: proves no change is missed.
	Int SIMPLIFY_NUM = 1;	// boundary simplification tolerance in cells, as a fraction.
	Int SIMPLIFY_DEN = 1;

	void loadTuning()
	{
		MoveTune::apply("navmesh.build", BUILD);
		MoveTune::apply("navmesh.cache", CACHE);
		MoveTune::apply("navmesh.dump", DUMP);
		MoveTune::apply("navmesh.log", LOG);
		MoveTune::apply("navmesh.check", CHECK);
		MoveTune::apply("navmesh.simplifyNum", SIMPLIFY_NUM);
		MoveTune::apply("navmesh.simplifyDen", SIMPLIFY_DEN);
		s_tuningLoaded = true;
	}

	inline Bool wantPlainMesh() { return BUILD || DUMP || LOG || CHECK; }
	inline Bool enabled() { return wantPlainMesh() || MoveRules::navmesh(); }

	// ---- The meshes units search: one set per (labels the unit may stand on, its radius in half-cells), built from
	// s_labels and s_layers on first use and again when they have changed since (s_gridSerial). A set is the ground's
	// mesh and one mesh per usable bridge ("sheets", GameLogic/NavMeshQuery.h), joined into one atlas.
	struct SizedMesh
	{
		unsigned mask;
		Int radius;
		UnsignedInt serial;
		std::vector<NavBuild::Mesh> meshes;	///< 0 = the ground, then the bridges
		std::vector<Int> layerOf;						///< the pathfinder layer of each (LAYER_GROUND for 0)
		NavQuery::Atlas atlas;
		std::vector<NavQuery::TriGrid> grids;	///< buckets over each sheet's mesh, for the every-frame questions
	};
	std::vector<SizedMesh *> s_sized;
	UnsignedInt s_gridSerial = 1;
	Int s_statMesh = 0, s_statNoStart = 0, s_statNoGoal = 0, s_statNoPath = 0, s_statBuilds = 0, s_statNodes = 0;
	Int s_statBridge = 0, s_statWall = 0;

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

		// The ground as this size of unit sees it, then each bridge: a window of the map round the bridge, as a unit
		// on its layer sees it (the deck and the bridge's sides where the layer has cells, the ground elsewhere), with
		// enough ground round the bridge that everything the bridge changes for this unit is inside.
		std::vector<NavBuild::SheetCells> cells(1);
		NavBuild::erode(&s_labels[0], s_width, s_height, mask, radius, cells[0].cells);
		cells[0].ox = 0;
		cells[0].oy = 0;
		cells[0].w = s_width * 2;
		cells[0].h = s_height * 2;
		sm->layerOf.assign(1, (Int)LAYER_GROUND);
		const Int need = (radius + 1) / 2;	// cells: walls this near the window still shrink the ground inside it
		const Int margin = need + 1;
		std::vector<unsigned char> view, deck;
		for (size_t i = 0; i < s_layers.size(); ++i)
		{
			const LayerCells& lc = s_layers[i];
			Int x0 = lc.x0 - margin, y0 = lc.y0 - margin, x1 = lc.x0 + lc.w + margin, y1 = lc.y0 + lc.h + margin;
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > s_width) x1 = s_width;
			if (y1 > s_height) y1 = s_height;
			Int px0 = x0 - need, py0 = y0 - need, px1 = x1 + need, py1 = y1 + need;
			if (px0 < 0) px0 = 0;
			if (py0 < 0) py0 = 0;
			if (px1 > s_width) px1 = s_width;
			if (py1 > s_height) py1 = s_height;
			Int pw = px1 - px0, ph = py1 - py0;
			if (x1 <= x0 || y1 <= y0)
				continue;
			view.assign(pw * ph, 0);
			deck.assign(pw * ph, 0);
			for (Int y = 0; y < ph; ++y)
			{
				for (Int x = 0; x < pw; ++x)
				{
					Int cx = x + px0, cy = y + py0;
					Int lx = cx - lc.x0, ly = cy - lc.y0;
					unsigned char kind = (lx >= 0 && ly >= 0 && lx < lc.w && ly < lc.h) ? lc.kind[ly*lc.w + lx] : (unsigned char)0;
					view[y*pw + x] = (unsigned char)(kind == 1 ? NavMesh::NAV_CLEAR : (kind == 2 ? NavMesh::NAV_BLOCKED : s_labels[cy*s_width + cx]));
					deck[y*pw + x] = (unsigned char)(kind == 1 ? 1 : 0);
				}
			}
			int pad[4] = { x0 - px0, y0 - py0, px1 - x1, py1 - y1 };
			unsigned mapEdges = (px0 == 0 ? 1u : 0u) | (py0 == 0 ? 2u : 0u) | (px1 == s_width ? 4u : 0u) | (py1 == s_height ? 8u : 0u);
			NavBuild::SheetCells sc;
			NavBuild::erodeSheet(&view[0], &deck[0], pw, ph, mask, radius, pad, mapEdges, sc.cells);
			// No deck this unit can be on (it is too wide for the bridge, or may not use the surface): no sheet.
			Bool usable = false;
			for (size_t c = 0; c < sc.cells.size() && !usable; ++c)
				usable = (sc.cells[c] == NavBuild::SHEET_DECK);
			if (!usable)
				continue;
			sc.ox = 2 * x0;
			sc.oy = 2 * y0;
			sc.w = 2 * (x1 - x0);
			sc.h = 2 * (y1 - y0);
			cells.push_back(sc);
			sm->layerOf.push_back(lc.layer);
		}
		unsigned exact = 0;
		if (cells.size() > 1)
		{
			NavBuild::assignSheets(cells);
			exact = NavBuild::SHEET_EXACT;
		}
		// The atlas keeps pointers to the meshes: all of them are in place before the first is added.
		sm->atlas.clear();
		sm->meshes.clear();
		sm->meshes.resize(cells.size());
		for (size_t k = 0; k < cells.size(); ++k)
		{
			NavBuild::buildMesh(&cells[k].cells[0], cells[k].w, cells[k].h, SIMPLIFY_NUM, SIMPLIFY_DEN, sm->meshes[k], exact);
			sm->atlas.addSheet(&sm->meshes[k], cells[k].ox, cells[k].oy);
		}
		sm->atlas.connect();
		sm->grids.resize(sm->meshes.size());
		for (size_t g = 0; g < sm->meshes.size(); ++g)
			sm->grids[g].build(sm->meshes[g], 8);
		if (LOG)
		{
			Int tris = 0;
			for (size_t k = 0; k < sm->meshes.size(); ++k)
				tris += (Int)sm->meshes[k].tris.size();
			printf("NavMeshSet: frame=%u mask=%u radius=%d halfCells sheets=%d (ground + %d bridges of %d) triangles=%d (ground %d) links=%d\n",
				TheGameLogic->getFrame(), mask, radius, (Int)sm->meshes.size(), (Int)sm->meshes.size() - 1, (Int)s_layers.size(), tris,
				(Int)sm->meshes[0].tris.size(), (Int)sm->atlas.links.size());
		}
		return sm;
	}

	/// Which set of meshes a unit searches: the labels it may stand on and its size. False: none (it flies, or has
	/// no surface the meshes know).
	Bool sizeClassOf( const Object *obj, const LocomotorSet& locomotorSet, unsigned& mask, Int& radius )
	{
		// Which labels this unit may stand on.
		LocomotorSurfaceTypeMask surfaces = locomotorSet.getValidSurfaces();
		if (surfaces & LOCOMOTORSURFACE_AIR)
			return false;
		mask = 0;
		if (surfaces & LOCOMOTORSURFACE_GROUND)
		{
			mask |= 1u << NavMesh::NAV_CLEAR;
			if (obj->getCrusherLevel() > 0)
				mask |= 1u << NavMesh::NAV_FENCE;
		}
		if (surfaces & LOCOMOTORSURFACE_WATER) mask |= 1u << NavMesh::NAV_WATER;
		if (surfaces & LOCOMOTORSURFACE_CLIFF) mask |= 1u << NavMesh::NAV_CLIFF;
		if (surfaces & LOCOMOTORSURFACE_RUBBLE) mask |= 1u << NavMesh::NAV_RUBBLE;
		if (mask == 0)
			return false;

		// Its size, in half-cells, rounded down so that rounding never closes a gap the unit fits through. Infantry
		// are points: they pass through each other and through one-cell gaps, as on the grid.
		const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
		radius = 0;
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
		return true;
	}

	/// The sheet of a pathfinder layer in this set of meshes; 0 (the ground) if it has none.
	Int sheetOfLayer( const SizedMesh *sm, Int layer )
	{
		for (size_t k = 1; k < sm->layerOf.size(); ++k)
		{
			if (sm->layerOf[k] == layer)
				return (Int)k;
		}
		return 0;
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

	/// The bridges, in the order the terrain has them (the same on both machines). Wall tops (LAYER_WALL) are not
	/// read: units on them still get their paths from the grid.
	void readLayers( Pathfinder *pathfinder, Int width, Int height, std::vector<LayerCells>& layers )
	{
		layers.clear();
		if (TheTerrainLogic == nullptr || width <= 0 || height <= 0)
			return;
		for (Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge != nullptr; bridge = bridge->getNext())
		{
			PathfindLayerEnum layer = bridge->getLayer();
			if (layer <= LAYER_GROUND || layer >= LAYER_WALL)
				continue;
			// The layer's cells cover the bridge's bounds and one cell more each way (PathfindLayer::allocateCells);
			// a wider scan cannot miss one, and cells off the layer read as "not on the bridge".
			const Region2D *bounds = bridge->getBounds();
			LayerCells lc;
			lc.layer = (Int)layer;
			lc.x0 = REAL_TO_INT_FLOOR(bounds->lo.x / PATHFIND_CELL_SIZE_F) - 3;
			lc.y0 = REAL_TO_INT_FLOOR(bounds->lo.y / PATHFIND_CELL_SIZE_F) - 3;
			Int x1 = REAL_TO_INT_CEIL(bounds->hi.x / PATHFIND_CELL_SIZE_F) + 3;
			Int y1 = REAL_TO_INT_CEIL(bounds->hi.y / PATHFIND_CELL_SIZE_F) + 3;
			if (lc.x0 < 0) lc.x0 = 0;
			if (lc.y0 < 0) lc.y0 = 0;
			if (x1 > width) x1 = width;
			if (y1 > height) y1 = height;
			lc.w = x1 - lc.x0;
			lc.h = y1 - lc.y0;
			if (lc.w <= 0 || lc.h <= 0)
				continue;
			lc.kind.assign(lc.w * lc.h, 0);
			Bool any = false;
			for (Int y = 0; y < lc.h; ++y)
			{
				for (Int x = 0; x < lc.w; ++x)
				{
					const PathfindCell *cell = pathfinder->getCell(layer, x + lc.x0, y + lc.y0);
					if (cell == nullptr || cell->getLayer() != layer)
						continue;
					lc.kind[y*lc.w + x] = (unsigned char)(cell->getType() == PathfindCell::CELL_CLEAR ? 1 : 2);
					any = true;
				}
			}
			if (any)
				layers.push_back(lc);
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
	readLayers(pathfinder, s_width, s_height, s_layers);
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
		readLayers(pathfinder, w, h, s_layerScratch);
		if (s_scratch != s_labels || !(s_layerScratch == s_layers))
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
	// object that leaves no footprint): rebuild only if a label really changed, on the ground or on a bridge (a
	// bridge destroyed or repaired changes its layer's cells and nothing on the ground).
	readLabels(pathfinder, s_scratch, w, h);
	readLayers(pathfinder, w, h, s_layerScratch);
	if (w == s_width && h == s_height && s_scratch == s_labels && s_layerScratch == s_layers)
		return;
	Int differ = (w == s_width && h == s_height) ? countDifferent(s_scratch, s_labels) : -1;
	s_labels.swap(s_scratch);
	s_layers.swap(s_layerScratch);
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

namespace
{
	/// Which mesh a unit searches: the labels it may stand on, and its size in half-cells. FALSE: none (it flies).
	Bool classOf( Object *obj, const LocomotorSet& locomotorSet, unsigned& mask, Int& radius )
	{
		// Which labels this unit may stand on.
		LocomotorSurfaceTypeMask surfaces = locomotorSet.getValidSurfaces();
		if (surfaces & LOCOMOTORSURFACE_AIR)
			return FALSE;
		mask = 0;
		if (surfaces & LOCOMOTORSURFACE_GROUND)
		{
			mask |= 1u << NavMesh::NAV_CLEAR;
			if (obj->getCrusherLevel() > 0)
				mask |= 1u << NavMesh::NAV_FENCE;
		}
		if (surfaces & LOCOMOTORSURFACE_WATER) mask |= 1u << NavMesh::NAV_WATER;
		if (surfaces & LOCOMOTORSURFACE_CLIFF) mask |= 1u << NavMesh::NAV_CLIFF;
		if (surfaces & LOCOMOTORSURFACE_RUBBLE) mask |= 1u << NavMesh::NAV_RUBBLE;
		if (mask == 0)
			return FALSE;

		// Its size, in half-cells, rounded down so that rounding never closes a gap the unit fits through. Infantry
		// are points: they pass through each other and through one-cell gaps, as on the grid.
		const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
		radius = 0;
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
		return TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool NavMesh::onMesh( Object *obj, const LocomotorSet& locomotorSet, Real x, Real y )
{
	unsigned mask;
	Int radius;
	if (s_labels.empty() || obj == nullptr || !classOf(obj, locomotorSet, mask, radius))
		return FALSE;
	SizedMesh *sm = sizedMesh(mask, radius);
	const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
	NavQuery::Pt p, out;
	p.x = x / HALF;
	p.y = y / HALF;
	int tri = -1;
	// "On" allows a quarter of a half-cell: a line that runs along the mesh's edge is on it.
	// Ground is ground whichever sheet it belongs to: the ground at a bridge's ends is on the bridge's sheet, and
	// asking the main sheet alone called it a wall (an infantryman stood facing it for a whole run). Decks are not
	// asked about here: this is for units on the ground layer.
	for (size_t k = 0; k < sm->meshes.size(); ++k)
	{
		NavQuery::Pt local;
		local.x = p.x - (Real)sm->atlas.sheets[k].ox;
		local.y = p.y - (Real)sm->atlas.sheets[k].oy;
		if (NavQuery::nearestPassable(sm->meshes[k], sm->grids[k], local, 1u << NavBuild::SHEET_GROUND, 0.25f, out, tri))
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool NavMesh::segmentOnMesh( Object *obj, const LocomotorSet& locomotorSet, Real x0, Real y0, Real x1, Real y1 )
{
	unsigned mask;
	Int radius;
	if (s_labels.empty() || obj == nullptr || !classOf(obj, locomotorSet, mask, radius))
		return FALSE;
	SizedMesh *sm = sizedMesh(mask, radius);
	const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
	NavQuery::Pt a, b;
	a.x = x0 / HALF; a.y = y0 / HALF;
	b.x = x1 / HALF; b.y = y1 / HALF;
	// Clear within one sheet's ground. (A line that passes from one sheet's ground to another's is answered "no":
	// that only makes a look-ahead or a spread corner more cautious beside a bridge.)
	for (size_t k = 0; k < sm->meshes.size(); ++k)
	{
		NavQuery::Pt la, lb;
		la.x = a.x - (Real)sm->atlas.sheets[k].ox; la.y = a.y - (Real)sm->atlas.sheets[k].oy;
		lb.x = b.x - (Real)sm->atlas.sheets[k].ox; lb.y = b.y - (Real)sm->atlas.sheets[k].oy;
		if (NavQuery::segmentClear(sm->meshes[k], sm->grids[k], la, lb, 1u << NavBuild::SHEET_GROUND, 2.0f))
			return TRUE;
	}
	return FALSE;
}

namespace
{
	/// A Path from the points of a pulled string and the layer of each: every node is a corner, so each one's "next
	/// optimized" is simply the next.
	Path *pathFrom( const std::vector<Coord3D>& pts, const std::vector<PathfindLayerEnum>& layers )
	{
		Path *path = newInstance(Path);
		Coord3D last = pts[0];
		path->appendNode(&pts[0], layers[0]);
		for (size_t k = 1; k < pts.size(); ++k)
		{
			Real dx = pts[k].x - last.x, dy = pts[k].y - last.y;
			Bool isEnd = (k + 1 == pts.size());
			if (dx*dx + dy*dy < 1.0f)
			{
				// A point a hair from the node before it is that node; if it is where the string gets on a bridge, the
				// node is.
				if (layers[k] != LAYER_GROUND && path->getLastNode()->getLayer() == LAYER_GROUND)
					path->getLastNode()->setLayer(layers[k]);
				if (!isEnd)
					continue;
				if (path->getFirstNode() != path->getLastNode())
				{
					path->updateLastNode(&pts[k]);	// the destination itself is the last node, not a point a hair from it.
					continue;
				}
			}
			path->appendNode(&pts[k], layers[k]);
			last = pts[k];
		}
		for (PathNode *node = path->getFirstNode(); node != nullptr; node = node->getNext())
			node->setNextOptimized(node->getNext());
		path->markOptimized();
		return path;
	}

	// ---- Routes lately found, for the brief's "cache common corridors when large control groups receive identical
	// move commands". A group order is one search and then, for every other member, two straight-line questions:
	// can I go straight to the first corner of a route just found to (nearly) the same place for my size, and
	// from its last corner straight to my own place? If so that route is mine too. Ground routes only.
	struct CachedRoute
	{
		unsigned mask;
		Int radius;
		UnsignedInt serial, frame;
		Coord3D start, goal;
		std::vector<Coord3D> corners;	///< between the start and the goal, neither included
	};
	const size_t MAX_CACHED_ROUTES = 16;
	const UnsignedInt CACHED_ROUTE_FRAMES = LOGICFRAMES_PER_SECOND;	// one order's members are pathed within a few frames
	const Real CACHED_ROUTE_NEAR = 20.0f * PATHFIND_CELL_SIZE_F;
	// How far off the mesh an end may be and still be joined to it for the two straight-line questions. Small: a
	// unit pressed against a thin wall, joined generously, is joined to the ground on the wall's other side, and
	// the route it is then handed starts through the wall (an infantryman walked into one for a whole run).
	const Real CACHED_ROUTE_SNAP = 0.5f;
	std::vector<CachedRoute> s_routes;
	Int s_statCached = 0;
}

//-------------------------------------------------------------------------------------------------
Path *NavMesh::findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to, Int *cost )
{
	if (cost)
		*cost = 0;
	if (s_labels.empty() || obj == nullptr)
		return nullptr;

	const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
	unsigned mask = 0;
	Int radius = 0;
	if (!sizeClassOf(obj, locomotorSet, mask, radius))
		return nullptr;

	// Wall tops are not meshed: a unit on one, or sent to one, is pathed by the grid.
	PathfindLayerEnum objLayer = obj->getLayer();
	PathfindLayerEnum toLayer = TheTerrainLogic->getLayerForDestination(to);
	if (objLayer == LAYER_WALL || toLayer == LAYER_WALL)
	{
		++s_statWall;
		return nullptr;
	}

	SizedMesh *sm = sizedMesh(mask, radius);
	if (cost)
		*cost = 200;

	UnsignedInt now = TheGameLogic->getFrame();
	// Only for a unit setting off from rest, which is what the members of a fresh order are. A unit that asks
	// again while under way (attack-move does, often) gets its own search: handed a neighbour's corners each
	// time, it was sent to and fro between them (desert-attackmove: wobble 0.9 -> 26, one unit never arrived).
	PhysicsBehavior *objPhysics = obj->getPhysics();
	Bool fromRest = objPhysics != nullptr && objPhysics->getVelocityMagnitude() < 0.1f;
	if (CACHE && fromRest && objLayer == LAYER_GROUND && toLayer == LAYER_GROUND && !sm->meshes.empty())
	{
		const unsigned GROUND = 1u << NavBuild::SHEET_GROUND;
		for (size_t r = s_routes.size(); r-- > 0; )	// newest first
		{
			const CachedRoute& route = s_routes[r];
			if (route.mask != mask || route.radius != radius || route.serial != s_gridSerial || now - route.frame > CACHED_ROUTE_FRAMES)
				continue;
			if (sqr(route.goal.x - to->x) + sqr(route.goal.y - to->y) > sqr(CACHED_ROUTE_NEAR))
				continue;
			// ... from (nearly) the same place too: a route from elsewhere to my destination is somebody else's,
			// and its first corner may lie behind me.
			if (sqr(route.start.x - from->x) + sqr(route.start.y - from->y) > sqr(CACHED_ROUTE_NEAR))
				continue;
			if (!route.corners.empty()
					&& (route.corners.front().x - from->x) * (to->x - from->x) + (route.corners.front().y - from->y) * (to->y - from->y) <= 0.0f)
				continue;
			NavQuery::Pt a, b;
			a.x = from->x / HALF; a.y = from->y / HALF;
			const Coord3D& firstStop = route.corners.empty() ? *to : route.corners.front();
			b.x = firstStop.x / HALF; b.y = firstStop.y / HALF;
			if (!NavQuery::segmentClear(sm->meshes[0], sm->grids[0], a, b, GROUND, CACHED_ROUTE_SNAP))
				continue;
			if (!route.corners.empty())
			{
				a.x = route.corners.back().x / HALF; a.y = route.corners.back().y / HALF;
				b.x = to->x / HALF; b.y = to->y / HALF;
				if (!NavQuery::segmentClear(sm->meshes[0], sm->grids[0], a, b, GROUND, CACHED_ROUTE_SNAP))
					continue;
			}
			std::vector<Coord3D> pts;
			std::vector<PathfindLayerEnum> layers;
			pts.push_back(*from);
			pts.insert(pts.end(), route.corners.begin(), route.corners.end());
			pts.push_back(*to);
			layers.assign(pts.size(), LAYER_GROUND);
			++s_statMesh;
			++s_statCached;
			if (cost)
				*cost = 40;
			return pathFrom(pts, layers);
		}
	}

	// Where the unit is: on a bridge's deck, on the ground, or at a bridge's end, where it can be either (the ground
	// cells there are the bridge's "connect" cells, and the unit's layer changes as it drives over them).
	Int startDeck = sheetOfLayer(sm, objLayer);
	Bool startGround = (startDeck == 0);
	if (startDeck == 0)
	{
		for (size_t k = 1; k < sm->layerOf.size(); ++k)
		{
			if (TheTerrainLogic->objectInteractsWithBridgeLayer(obj, sm->layerOf[k]))
			{
				startDeck = (Int)k;
				break;
			}
		}
	}
	Int goalDeck = sheetOfLayer(sm, toLayer);
	Bool goalGround = (goalDeck == 0);

	// The ends, on the mesh (which is in half-cells). A unit may stand nearer a wall than its mesh allows (it was
	// built there, it was pushed, the grid let it), and so may a destination: each is joined to the nearest point of
	// the mesh, the destination only to ground connected to where the unit is.
	NavQuery::Pt s, g;
	s.x = from->x / HALF; s.y = from->y / HALF;
	g.x = to->x / HALF;   g.y = to->y / HALF;
	Real reach = (Real)(radius + 4);
	NavQuery::AtlasPath found;
	int result = NavQuery::findPath(sm->atlas, s, startDeck, startGround, g, goalDeck, goalGround, reach, found);
	if (result == NavQuery::PATH_NO_START)
	{
		++s_statNoStart;
		return nullptr;
	}
	if (result == NavQuery::PATH_NO_GOAL)
	{
		++s_statNoGoal;
		return nullptr;
	}
	if (result != NavQuery::PATH_FOUND || found.pts.size() < 2)
	{
		++s_statNoPath;
		return nullptr;
	}
	++s_statMesh;
	s_statNodes += found.expanded;
	if (cost)
		*cost += found.expanded * 4;

	// As a Path: every node is a corner, so each one's "next optimized" is simply the next. A node is on a bridge's
	// layer if the path is on that bridge's deck on either side of it: the nodes where the string gets on and off
	// the deck and its corners between. That is what the follower reads to put the unit on the bridge's layer
	// (Path::computePointOnPath, Pathfinder::updateLayer), as it does with the grid's paths.
	std::vector<Coord3D> pts;
	std::vector<PathfindLayerEnum> layers;
	Coord3D c;
	c = *from;
	pts.push_back(c);
	layers.push_back(objLayer);
	Bool overBridge = false;
	for (size_t k = 0; k < found.pts.size(); ++k)
	{
		if (found.inserted[k] == 1)
			continue;	// where the string passes from one sheet's ground to another's: nothing the follower needs
		PathfindLayerEnum layer = LAYER_GROUND;
		if (found.deck[k])
			layer = (PathfindLayerEnum)sm->layerOf[found.sheet[k]];
		else if (k > 0 && found.deck[k-1])
			layer = (PathfindLayerEnum)sm->layerOf[found.sheet[k-1]];
		if (layer != LAYER_GROUND)
			overBridge = true;
		c.x = found.pts[k].x * HALF;
		c.y = found.pts[k].y * HALF;
		c.z = TheTerrainLogic->getLayerHeight(c.x, c.y, layer);
		pts.push_back(c);
		layers.push_back(layer);
	}
	c = *to;
	pts.push_back(c);
	layers.push_back(toLayer);
	if (overBridge)
		++s_statBridge;
	else if (CACHE && objLayer == LAYER_GROUND && toLayer == LAYER_GROUND && pts.size() >= 4)
	{
		if (s_routes.size() >= MAX_CACHED_ROUTES)
			s_routes.erase(s_routes.begin());
		s_routes.push_back(CachedRoute());
		CachedRoute& route = s_routes.back();
		route.mask = mask;
		route.radius = radius;
		route.serial = s_gridSerial;
		route.frame = now;
		route.start = *from;
		route.goal = *to;
		// The corners only: pts is [where the unit is, the string from its start to its goal, where it was sent],
		// and the string's own two ends are this unit's start and place, not corners of the route. (Left in, every
		// neighbour was first sent to this unit's start and finally through its place.)
		if (pts.size() > 4)
			route.corners.assign(pts.begin() + 2, pts.end() - 2);
	}
	return pathFrom(pts, layers);
}

//-------------------------------------------------------------------------------------------------
Int NavMesh::debugOverlay()
{
	// Read once: "-movetune" is parsed before any map loads, and a renderer asks this every frame.
	static Int s_overlay = -1;
	if (s_overlay < 0)
	{
		s_overlay = 0;
		MoveTune::apply("navmesh.overlay", s_overlay);
		if (s_overlay < 0)
			s_overlay = 0;
	}
	return s_overlay;
}

//-------------------------------------------------------------------------------------------------
Bool NavMesh::debugSheets( const Object *obj, const LocomotorSet *locomotorSet, std::vector<DebugSheet>& out, Int *radiusHalfCells,
													 Bool *upToDate )
{
	out.clear();
	const SizedMesh *sm = nullptr;
	if (obj != nullptr && locomotorSet != nullptr)
	{
		unsigned mask = 0;
		Int radius = 0;
		if (!sizeClassOf(obj, *locomotorSet, mask, radius))
			return false;
		for (size_t i = 0; i < s_sized.size(); ++i)
		{
			if (s_sized[i]->mask == mask && s_sized[i]->radius == radius)
				sm = s_sized[i];
		}
	}
	else if (!s_sized.empty())
	{
		sm = s_sized[0];
	}
	// Only what the sim has already built: a set appears with its size's first path request (and is rebuilt by the
	// next one after the grid changed), never because somebody looked.
	if (sm == nullptr || sm->meshes.empty())
		return false;
	for (size_t k = 0; k < sm->meshes.size(); ++k)
	{
		DebugSheet ds;
		ds.mesh = &sm->meshes[k];
		ds.ox = sm->atlas.sheets[k].ox;
		ds.oy = sm->atlas.sheets[k].oy;
		ds.layer = sm->layerOf[k];
		out.push_back(ds);
	}
	if (radiusHalfCells)
		*radiusHalfCells = sm->radius;
	if (upToDate)
		*upToDate = (sm->serial == s_gridSerial);
	return true;
}

//-------------------------------------------------------------------------------------------------
void NavMesh::printStats()
{
	if (!MoveRules::navmesh())
		return;
	// mesh: paths the mesh gave (overBridge of them cross a bridge); the others went to the grid: no ground of the
	// unit's size near it (noStart), the destination not connected to it (noGoal), a search that failed though it
	// is (noPath, should be 0), a unit on or sent to a wall top (wall).
	Int sheets = 0;
	for (size_t i = 0; i < s_sized.size(); ++i)
	{
		if ((Int)s_sized[i]->meshes.size() > sheets)
			sheets = (Int)s_sized[i]->meshes.size();
	}
	printf("NavMeshCache: reused=%d of %d\n", s_statCached, s_statMesh);
	printf("NavMeshPaths: mesh=%d overBridge=%d noStart=%d noGoal=%d noPath=%d wall=%d sizes=%d bridgeSheets=%d builds=%d meanNodes=%.1f\n",
		s_statMesh, s_statBridge, s_statNoStart, s_statNoGoal, s_statNoPath, s_statWall, (Int)s_sized.size(), sheets > 0 ? sheets - 1 : 0,
		s_statBuilds, s_statMesh ? (Real)s_statNodes / (Real)s_statMesh : 0.0f);
}
