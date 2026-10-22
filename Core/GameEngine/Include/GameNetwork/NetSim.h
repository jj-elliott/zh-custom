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

// zh-pipeline @feature 2026-10-21 Link impairment simulation settings for the solo netcode rig.
// Formerly GlobalData members that only existed in RTS_DEBUG builds; kept in Core so release builds can use
// -packetloss / -latAvg / -latAmp / -latPeriod / -latNoise without touching either game's GlobalData. Read by
// Transport.cpp on receive: packets are dropped or delayed on the way *in*, so the simulation never alters what is sent
// and cannot affect the other player's view of the game. Not part of the logic CRC (uses the client RNG).

#pragma once

#include "Lib/BaseType.h"

namespace NetSim
{
	extern Int latencyAverage;		///< ms added to every incoming packet
	extern Int latencyAmplitude;	///< ms amplitude of a sinusoidal latency modulation
	extern Int latencyPeriod;		///< period of that modulation (engine units: multiplies the ms clock)
	extern Int latencyNoise;		///< +- ms of uniform jitter
	extern Int packetLoss;			///< percent of incoming packets dropped
}
