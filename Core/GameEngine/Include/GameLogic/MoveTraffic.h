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

// zh-pipeline @feature 2026-11-16 Traffic map: congestion-aware pathfinding (docs/PLAN.md Phase 7), MoveRules::UNIFIED.
//
// The retail pathfinder costs a route by terrain and by who is standing where right now. It knows nothing about
// where other units are about to go, so every unit of a group is handed the same shortest route and they meet at
// every corner of it. This is a second grid over the pathfind grid that remembers planned routes:
//
//   stamp   when a ground unit gets a path, every cell the path passes over (to the width of the unit) is marked
//           with the frame the unit expects to be there and a load count.
//   cost    while searching, a cell that other units have stamped for about the same time costs extra per unit
//           (capped), so the next unit prefers a slightly longer route that is free, or free at that time.
//   keep    the search result is a chain of cells that the retail path smoother then straightens by line of sight;
//           it would cut straight back through the traffic. Nodes running alongside traffic are pinned so the
//           smoothed path keeps its distance.
//
// Nothing is ever erased: a stamp only counts within WINDOW frames of its own time, so it lapses by itself.
//
// SIM STATE, deterministic: written and read only from the logic thread in logic order, integer cells and frames.
// It is not in the CRC stream or the save format (lockstep keeps both machines' copies equal; a loaded save starts
// with an empty map, which only means the first routes after loading are planned as retail would).

#pragma once

#include "Lib/BaseType.h"

class Path;

namespace MoveTraffic
{
	/// (Re)allocate for a new map. Cell bounds as the pathfinder computes them, inclusive.
	void reset( Int loX, Int loY, Int hiX, Int hiY );

	/// Who the next path searches and the next stamp are for. framesPerCell: how long the unit takes to cross one
	/// pathfind cell at top speed; halfWidthCells: its half width in cells; detourWillingness: scales what traffic
	/// costs this unit (1 = normal; a nimble unit gets more, so it is the one that goes round; a slow one less, so
	/// it keeps the direct line). Call clearMover() when done.
	void setMover( UnsignedInt ownerID, Real framesPerCell, Int halfWidthCells, Real detourWillingness );
	void clearMover();
	Bool hasMover();

	/// A search for the current mover starts at this cell (arrival times are estimated from the distance to it).
	void beginSearch( Int startX, Int startY );

	/// Extra path cost for the current mover entering ground cell (x, y). 0 when there is no mover.
	Int cost( Int x, Int y );

	/// Is there other units' traffic for about the current mover's arrival time in or right next to (x, y)?
	Bool nearTraffic( Int x, Int y );

	/// Record the current mover's path (its optimized nodes) starting now.
	void stamp( Path *path );
}
