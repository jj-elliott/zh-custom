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

// zh-pipeline @feature 2026-11-16 Traffic map, see MoveTraffic.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/MoveTraffic.h"

#include "Common/GameCommon.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/MoveRules.h"

#include <vector>

namespace
{
	// Tuning. Path costs are in the pathfinder's units: COST_ORTHOGONAL (10) per cell.
	const UnsignedInt WINDOW = 5 * LOGICFRAMES_PER_SECOND;	// two units "meet" in a cell if they are due within this of each other.
	// First play-test of the unified system: at 6 per unit (up to +18 on a 10-cost cell) units over-spread, some
	// setting off sideways or backwards to get round the whole group. The penalty only has to break the tie between
	// "the same line as everyone" and "the free line next to it", so it is small: at most +3, a 30% longer way round.
	const Int COST_PER_UNIT = 1;														// extra cost per other unit due in a cell...
	const Int MAX_COUNTED = 3;															// ...up to this many, so a real choke stays affordable.
	const Int MAX_LOAD = 250;

	struct Cell
	{
		UnsignedInt frame;	// when the most recent stamper expects to be here.
		UnsignedInt owner;	// that stamper.
		UnsignedByte load;	// how many units have stamped for about that time; 0 = never stamped.
	};

	std::vector<Cell> s_cells;
	Int s_loX = 0, s_loY = 0, s_width = 0, s_height = 0;

	// The unit currently being pathed.
	UnsignedInt s_owner = 0;
	Bool s_hasMover = false;
	Real s_framesPerCell = 0.0f;
	Int s_halfWidth = 0;
	Int s_startX = 0, s_startY = 0;
	UnsignedInt s_startFrame = 0;

	inline Cell *cellAt( Int x, Int y )
	{
		x -= s_loX;
		y -= s_loY;
		if (x < 0 || y < 0 || x >= s_width || y >= s_height)
			return nullptr;
		return &s_cells[y*s_width + x];
	}

	inline Bool closeInTime( UnsignedInt a, UnsignedInt b )
	{
		return (a > b ? a - b : b - a) <= WINDOW;
	}

	// When the current mover would reach (x, y): straight-line distance from where its search started. A lower
	// bound on the real route, which is good enough against a window of seconds, and unlike the search's own
	// running cost it does not depend on the order cells happen to be expanded in.
	inline UnsignedInt arrivalFrame( Int x, Int y )
	{
		Real dx = (Real)(x - s_startX);
		Real dy = (Real)(y - s_startY);
		return s_startFrame + (UnsignedInt)(WWMath::Sqrtf(dx*dx + dy*dy) * s_framesPerCell);
	}

	// Other units due in this cell around frame t.
	inline Int othersDue( const Cell *cell, UnsignedInt t )
	{
		if (cell == nullptr || cell->load == 0 || !closeInTime(cell->frame, t))
			return 0;
		Int others = cell->load;
		if (cell->owner == s_owner)
			--others;	// the latest stamp is our own previous route.
		return others;
	}

	void stampCell( Int x, Int y, UnsignedInt t )
	{
		Cell *cell = cellAt(x, y);
		if (cell == nullptr)
			return;
		if (cell->load != 0 && cell->owner == s_owner && closeInTime(cell->frame, t))
		{
			cell->frame = t;	// re-planning the same stretch: don't count ourselves twice.
			return;
		}
		if (cell->load != 0 && closeInTime(cell->frame, t))
		{
			if (cell->load < MAX_LOAD)
				++cell->load;
		}
		else
		{
			cell->load = 1;
		}
		cell->frame = t;
		cell->owner = s_owner;
	}
}

namespace MoveTraffic
{

void reset( Int loX, Int loY, Int hiX, Int hiY )
{
	s_loX = loX;
	s_loY = loY;
	s_width = hiX - loX + 1;
	s_height = hiY - loY + 1;
	if (s_width < 0) s_width = 0;
	if (s_height < 0) s_height = 0;
	Cell empty;
	empty.frame = 0;
	empty.owner = 0;
	empty.load = 0;
	s_cells.assign((size_t)s_width * (size_t)s_height, empty);
	s_hasMover = false;
}

void setMover( UnsignedInt ownerID, Real framesPerCell, Int halfWidthCells )
{
	s_owner = ownerID;
	s_framesPerCell = framesPerCell;
	s_halfWidth = halfWidthCells;
	s_hasMover = MoveRules::unified() && !s_cells.empty();
	s_startFrame = TheGameLogic->getFrame();
}

void clearMover()
{
	s_hasMover = false;
}

Bool hasMover()
{
	return s_hasMover;
}

void beginSearch( Int startX, Int startY )
{
	s_startX = startX;
	s_startY = startY;
	s_startFrame = TheGameLogic->getFrame();
}

Int cost( Int x, Int y )
{
	if (!s_hasMover)
		return 0;
	Int others = othersDue(cellAt(x, y), arrivalFrame(x, y));
	if (others <= 0)
		return 0;
	if (others > MAX_COUNTED)
		others = MAX_COUNTED;
	return others * COST_PER_UNIT;
}

Bool nearTraffic( Int x, Int y )
{
	if (!s_hasMover)
		return FALSE;
	UnsignedInt t = arrivalFrame(x, y);
	for (Int dy = -1; dy <= 1; ++dy)
		for (Int dx = -1; dx <= 1; ++dx)
			if (othersDue(cellAt(x + dx, y + dy), t) > 0)
				return TRUE;
	return FALSE;
}

void stamp( Path *path )
{
	if (!s_hasMover || path == nullptr)
		return;
	PathNode *node = path->getFirstNode();
	if (node == nullptr)
		return;

	// Walk the optimized path in half-cell steps; mark a square of our own width around each step. (Not wider: with a
	// margin the bands of a group's units tiled its whole frontage, leaving no free line beside anyone.)
	const Real STEP = PATHFIND_CELL_SIZE_F * 0.5f;
	const Int reach = s_halfWidth;
	Real travelled = 0.0f;	// world units along the path so far.
	Int lastX = 0x7fffffff, lastY = 0x7fffffff;
	UnsignedInt now = TheGameLogic->getFrame();
	while (node != nullptr)
	{
		PathNode *next = node->getNextOptimized();
		if (node->getLayer() == LAYER_GROUND)
		{
			Coord3D from = *node->getPosition();
			Coord3D to = next ? *next->getPosition() : from;
			Real dx = to.x - from.x;
			Real dy = to.y - from.y;
			Real length = WWMath::Sqrtf(dx*dx + dy*dy);
			Int steps = (Int)(length / STEP) + 1;
			for (Int i = 0; i < steps; ++i)
			{
				Real f = (length > 0.0f) ? ((Real)i * STEP) / length : 0.0f;
				if (f > 1.0f) f = 1.0f;
				Int cx = REAL_TO_INT_FLOOR((from.x + dx*f) / PATHFIND_CELL_SIZE_F);
				Int cy = REAL_TO_INT_FLOOR((from.y + dy*f) / PATHFIND_CELL_SIZE_F);
				if (cx == lastX && cy == lastY)
					continue;
				lastX = cx;
				lastY = cy;
				UnsignedInt t = now + (UnsignedInt)(((travelled + length*f) / PATHFIND_CELL_SIZE_F) * s_framesPerCell);
				for (Int oy = -reach; oy <= reach; ++oy)
					for (Int ox = -reach; ox <= reach; ++ox)
						stampCell(cx + ox, cy + oy, t);
			}
			travelled += length;
		}
		node = next;
	}
}

}
