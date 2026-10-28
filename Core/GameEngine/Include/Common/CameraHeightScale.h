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

// zh-pipeline @feature 2026-10-21 Higher tactical-camera ceiling, GenTool style.
//
// The scale multiplies GameData.ini MaxCameraHeight for the player-controlled camera only (scripted cameras keep
// CameraHeight). The baked default is what everyone gets in a network game; Options.ini "CameraHeightScale" is a
// per-machine override honoured only outside network games, so both players always see the same height online.
// Client-only: the camera is not part of the logic CRC. Lives in Core so the shared View code works for both games.

#pragma once

#include "Lib/BaseType.h"

namespace CameraHeightScale
{
	/// Baked default, applied to everyone in network games. 2.3x the retail ceiling (Zero Hour: 310 -> 713).
	/// History: 1.5 play-tested 2026-10-21 "feels right" but the human asked for more; 2.0 next; 2026-10-27 +15% -> 2.3.
	constexpr Real DefaultScale = 2.3f;

	/// Call once after Options.ini has been read (per-machine override; <= 0 means "not set").
	void SetUserScale(Real scale);

	/// The scale in effect right now: user override outside network games, DefaultScale otherwise.
	Real GetEffectiveScale();

	/// TheGlobalData->m_maxCameraHeight times the effective scale.
	Real GetMaxCameraHeight();
}
