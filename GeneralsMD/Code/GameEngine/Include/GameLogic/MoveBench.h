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

// zh-pipeline @feature 2026-11-16 Movement benchmark (docs/PLAN.md Phase 7): numbers instead of "feels sticky".
//
// A course is a save game plus the move orders a person gave in it:
//   record   "-loadsave <x.sav> -movebenchrecord <file>": play normally; every move / attack-move order given to a
//            group is appended to <file> (frame offset from the first order, the units' ids, the destination).
//   run      "-headless -loadsave <x.sav> -movebench <file>": no window. Loads the save, gives the same orders to
//            the same units at the same offsets, simulates as fast as the machine goes until every ordered unit has
//            come to rest (or a time limit), and prints one line of measurements:
//              meanArrive / maxArrive   seconds from a unit's order until it stopped for good
//              goalDist                 how far from the order's destination units ended, averaged (a group settles
//                                       around the point, so not zero; a big number means units gave up short)
//              overlap                  unit-pair-seconds two vehicles spent more than 40% inside each other
//              reversals                times a unit's turning flipped direction, per unit (the "jitter" number)
//              stalled                  unit-seconds spent standing still while still trying to move
//              detour                   distance travelled / straight-line distance, averaged
// "-movebench auto" needs no recorded course: every combat ground unit of the local player to the map centre.
// The random number generators are re-seeded at the start, so a run is repeatable to the last digit.
// Run with different "-movetune name=value,..." settings (GameLogic/MoveRules.h) to compare tunings on one course.
// The run is the real simulation, so it is deterministic: the same build, save, course and tuning print the same line.

#pragma once

namespace MoveBench
{
	void install();	///< hook the recorder in, if "-movebenchrecord" was given. Call once after the engine is up.
	Bool wantsRun();	///< "-movebench" was given
	int run();				///< the headless run; returns the process exit code
}
