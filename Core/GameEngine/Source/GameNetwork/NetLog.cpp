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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/NetLog.h"
#include "Common/version.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace NetLog
{
	static char s_path[512] = "";
	static FILE* s_file = nullptr;
	static Bool s_failed = FALSE;
	static UnsignedInt s_openTime = 0;

	// per-second accumulators
	static UnsignedInt s_secStart = 0;
	static UnsignedInt s_lastUpdate = 0;
	static UnsignedInt s_stallMs = 0;
	static UnsignedInt s_stalls = 0;
	static Bool s_inStall = FALSE;
	static UnsignedInt s_stallStart = 0;
	static UnsignedInt s_stallFrame = 0;
	static UnsignedInt s_retries = 0;
	static UnsignedInt s_latN = 0;
	static Real s_latSum = 0.0f, s_latMax = 0.0f, s_latLast = -1.0f, s_jitterSum = 0.0f;
	static UnsignedInt s_jitterN = 0;
	static UnsignedInt s_inP = 0, s_inB = 0, s_outP = 0, s_outB = 0, s_unkP = 0;
	static Bool s_haveTransport = FALSE;

	static UnsignedInt now() { return (UnsignedInt)timeGetTime(); }

	static Bool ensureOpen()
	{
		if (s_file != nullptr) return TRUE;
		if (s_failed || s_path[0] == '\0') return FALSE;
		s_file = fopen(s_path, "w");
		if (s_file == nullptr) { s_failed = TRUE; return FALSE; }
		s_openTime = now();
		s_secStart = s_openTime;
		time_t t = time(nullptr);
		char stamp[64] = "";
		strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
		const char* version = (TheVersion != nullptr) ? TheVersion->getAsciiVersion().str() : "?";
		fprintf(s_file, "# zh-netlog v1 opened=%s version=%s\n", stamp, version);
		fprintf(s_file, "# sec: once per second. stall_ms = time this second spent waiting for the peer's commands. lat_* = local command->ack round trips in ms; jitter = mean |delta| between successive samples.\n");
		fflush(s_file);
		return TRUE;
	}

	static UnsignedInt rel(UnsignedInt t) { return t - s_openTime; }

	void SetPath(const char* path)
	{
		if (path == nullptr) return;
		strncpy(s_path, path, sizeof(s_path) - 1);
		s_path[sizeof(s_path) - 1] = '\0';
	}

	Bool IsEnabled() { return s_path[0] != '\0' && !s_failed; }

	void GameStart(Int localSlot, Int packetRouterSlot, Int players, Int runAhead, Int frameRate)
	{
		if (!ensureOpen()) return;
		s_lastUpdate = 0; s_inStall = FALSE; s_stallMs = 0; s_stalls = 0; s_retries = 0;
		s_latN = 0; s_latSum = 0.0f; s_latMax = 0.0f; s_latLast = -1.0f; s_jitterSum = 0.0f; s_jitterN = 0;
		s_secStart = now();
		fprintf(s_file, "start t=%u localSlot=%d router=%d players=%d runAhead=%d frameRate=%d\n",
			rel(now()), localSlot, packetRouterSlot, players, runAhead, frameRate);
		fflush(s_file);
	}

	void Update(UnsignedInt logicFrame, Int runAhead, Int frameRate, Bool frameDataReady, Bool isStalling, Real minCushion, Real maxLatencySeconds)
	{
		if (!ensureOpen()) return;
		const UnsignedInt t = now();

		if (s_lastUpdate != 0)
		{
			const UnsignedInt dt = t - s_lastUpdate;
			if (isStalling)
			{
				s_stallMs += dt;
				if (!s_inStall) { s_inStall = TRUE; s_stallStart = t; s_stallFrame = logicFrame; }
			}
		}
		if (s_inStall && (frameDataReady || !isStalling))
		{
			const UnsignedInt dur = t - s_stallStart;
			s_inStall = FALSE;
			++s_stalls;
			if (dur >= 100)
				fprintf(s_file, "stall t=%u frame=%u ms=%u runAhead=%d\n", rel(t), s_stallFrame, dur, runAhead);
		}
		s_lastUpdate = t;

		if (t - s_secStart >= 1000)
		{
			const Real latMean = s_latN ? s_latSum / (Real)s_latN : 0.0f;
			const Real jitter = s_jitterN ? s_jitterSum / (Real)s_jitterN : 0.0f;
			fprintf(s_file, "sec t=%u frame=%u ra=%d fps=%d stall_ms=%u stalls=%u cushion=%.1f lat_max_peer=%.0f lat_n=%u lat_mean=%.0f lat_max=%.0f jitter=%.0f retries=%u in_pps=%u in_bps=%u out_pps=%u out_bps=%u unk_pps=%u\n",
				rel(t), logicFrame, runAhead, frameRate, s_stallMs, s_stalls, minCushion, maxLatencySeconds * 1000.0f,
				s_latN, latMean, s_latMax, jitter, s_retries,
				s_inP, s_inB, s_outP, s_outB, s_unkP);
			fflush(s_file);
			s_secStart = t;
			s_stallMs = 0; s_stalls = 0; s_retries = 0;
			s_latN = 0; s_latSum = 0.0f; s_latMax = 0.0f; s_jitterSum = 0.0f; s_jitterN = 0;
			if (!s_haveTransport) { s_inP = s_inB = s_outP = s_outB = s_unkP = 0; }
			s_haveTransport = FALSE;
		}
	}

	void LatencySample(Real latencyMs)
	{
		if (!IsEnabled()) return;
		++s_latN;
		s_latSum += latencyMs;
		if (latencyMs > s_latMax) s_latMax = latencyMs;
		if (s_latLast >= 0.0f) { s_jitterSum += fabsf(latencyMs - s_latLast); ++s_jitterN; }
		s_latLast = latencyMs;
	}

	void Retry()
	{
		if (!IsEnabled()) return;
		++s_retries;
	}

	void RunAheadComputed(Int oldRunAhead, Int newRunAhead, Real maxLatencySeconds, Int minFps, Int oldFrameRate, UnsignedInt executionFrame)
	{
		if (!ensureOpen()) return;
		fprintf(s_file, "runahead t=%u calc old=%d new=%d maxLat=%.0f minFps=%d oldFps=%d execFrame=%u\n",
			rel(now()), oldRunAhead, newRunAhead, maxLatencySeconds * 1000.0f, minFps, oldFrameRate, executionFrame);
		fflush(s_file);
	}

	void RunAheadApplied(Int runAhead, Int frameRate, UnsignedInt logicFrame)
	{
		if (!ensureOpen()) return;
		fprintf(s_file, "runahead t=%u apply ra=%d fps=%d frame=%u\n", rel(now()), runAhead, frameRate, logicFrame);
		fflush(s_file);
	}

	void TransportSecond(UnsignedInt inPackets, UnsignedInt inBytes, UnsignedInt outPackets, UnsignedInt outBytes, UnsignedInt unknownPackets)
	{
		if (!IsEnabled()) return;
		s_inP = inPackets; s_inB = inBytes; s_outP = outPackets; s_outB = outBytes; s_unkP = unknownPackets;
		s_haveTransport = TRUE;
	}

	void CRCMismatch(UnsignedInt logicFrame)
	{
		if (!ensureOpen()) return;
		fprintf(s_file, "crc t=%u frame=%u MISMATCH\n", rel(now()), logicFrame);
		fflush(s_file);
	}

	void Event(const char* fmt, ...)
	{
		if (!ensureOpen()) return;
		fprintf(s_file, "event t=%u ", rel(now()));
		va_list args;
		va_start(args, fmt);
		vfprintf(s_file, fmt, args);
		va_end(args);
		fputc('\n', s_file);
		fflush(s_file);
	}
}
