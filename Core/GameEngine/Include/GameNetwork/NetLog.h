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

// zh-pipeline @feature 2026-10-21 Network session log ("-netlog <path>"), PLAN.md Phase 4 step 1: instrument, don't guess.
//
// A pure observer: every function here only reads what the network layer hands it and writes text; nothing feeds back
// into the simulation, so the logic CRC is unaffected. All calls are no-ops unless -netlog was given. The file is opened
// lazily on the first write (after the working directory is the game folder), so sessions without a network game
// leave no file behind.
//
// Format: one line per record, "<kind> t=<ms since open> key=value ...". Kinds:
//   start      game start: local slot, packet router, players, initial run-ahead and frame rate
//   sec        once per wall-clock second: frame, run-ahead, logic fps, stall ms + count, min cushion, max latency,
//              local ack-latency samples (n/mean/max/jitter), retries, transport packets/bytes per second
//   stall      a stall episode >= 100 ms (start frame, duration)
//   runahead   the packet router computed a new run-ahead (inputs included) / a run-ahead command was applied
//   crc        CRC mismatch detected (frame)
//   event      disconnects and other notable events

#pragma once

#include "Lib/BaseType.h"

namespace NetLog
{
	void SetPath(const char* path);            ///< From the command line. Enables logging.
	Bool IsEnabled();

	void GameStart(Int localSlot, Int packetRouterSlot, Int players, Int runAhead, Int frameRate);
	/// Once per Network::update (i.e. every client frame while in a network game).
	void Update(UnsignedInt logicFrame, Int runAhead, Int frameRate, Bool frameDataReady, Bool isStalling, Real minCushion, Real maxLatencySeconds);
	void LatencySample(Real latencyMs);        ///< One command->ack round trip measured locally.
	void Retry(UnsignedInt retryTimerMs = 0);  ///< A command was re-sent because its ack did not arrive within retryTimerMs.
	void RunAheadComputed(Int oldRunAhead, Int newRunAhead, Real maxLatencySeconds, Int minFps, Int oldFrameRate, UnsignedInt executionFrame);
	void RunAheadApplied(Int runAhead, Int frameRate, UnsignedInt logicFrame);
	void TransportSecond(UnsignedInt inPackets, UnsignedInt inBytes, UnsignedInt outPackets, UnsignedInt outBytes, UnsignedInt unknownPackets);
	void CRCMismatch(UnsignedInt logicFrame);
	void Event(const char* fmt, ...);
}
