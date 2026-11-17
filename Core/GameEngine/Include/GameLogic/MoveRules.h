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

// zh-pipeline @feature 2026-11-16 Unit movement rules level (docs/PLAN.md Phase 7).
//
// Phase 7 changes how units behave, so each step is a deliberate simulation change. Every step is gated on one
// integer so that (a) the retail replay baselines keep verifying at level 0, which proves the gates leak nothing,
// and (b) a feel problem can be bisected to a step by playing at a lower level.
//
// THIS IS SIM STATE. Both machines of a network game must run the same level or they desync at the first collision
// that resolves differently. The default is the newest level; "-moverules <n>" overrides it and exists for replay
// verification and bisecting, so the launcher must never pass it to one side only. The level is fixed for the life
// of the process and is not part of a save game or a replay: a replay only reproduces at the level it was recorded
// at (scripts/verify.ps1 reads it from "<replay>.args").
//
// Determinism of the rules themselves: they only read values that are already in the lockstep state (AI state ids,
// command source, INI mass, positions, object ids) and compare them; no new floating point maths, no new RNG draws,
// no container iteration.

#pragma once

#include "Lib/BaseType.h"

namespace MoveRules
{
	enum Level CPP_11(: Int)
	{
		RETAIL = 0,				///< unchanged behaviour: reproduces replays recorded before Phase 7
		YIELD = 1,				///< step 1: an idle ally in the way of a mover steps aside instead of making the mover path around
		PRIORITY = 2,			///< step 2: push priority (player order > auto-move, then heavier Mass) decides who gives way
		PASS_THROUGH = 3,	///< step 3, first cut: allies moving the same way pass through each other; a unit blocked past 2 s drops collision for itself
		SOFT_FLOW = 4,		///< step 3, second cut: same-way allies queue behind each other instead of overlapping, without the stuck/repath churn; stacked idle units spread out
		TIGHT_QUEUE = 5,	///< step 3, third cut: a follower's speed cap falls to zero as it sinks into the unit ahead, and is applied at once
		EASED_FLOW = 6,		///< step 3, fourth cut: light contact is free, the cap eases in with depth and relaxes gradually; a stalled queue or a stopped shooter ahead is pathed around after 1 s
		STEERING = 7,			///< step 4: local avoidance (AISteering): units steer around each other before contact; tracked vehicles keep rolling through wider turns

		LATEST = STEERING
	};

	Int getLevel();
	void setLevel( Int level );	///< clamped to [RETAIL, LATEST]; call before a game starts, never during one

	inline Bool yieldRule() { return getLevel() >= YIELD; }
	inline Bool pushPriority() { return getLevel() >= PRIORITY; }
	inline Bool passThrough() { return getLevel() >= PASS_THROUGH; }	///< step 3 in either form (also gates the stuck fail-safe)
	inline Bool softFlow() { return getLevel() >= SOFT_FLOW; }				///< queue-following replaces the level 3 overlap
	inline Bool tightQueue() { return getLevel() >= TIGHT_QUEUE; }		///< working queue-following (level 5 form: depth-scaled, immediate speed cap)
	inline Bool easedFlow() { return getLevel() >= EASED_FLOW; }			///< eased, smoothed cap; quick path-around when the queue ahead stops
	inline Bool steering() { return getLevel() >= STEERING; }					///< local avoidance before contact (GameLogic/AISteering.h)
}
