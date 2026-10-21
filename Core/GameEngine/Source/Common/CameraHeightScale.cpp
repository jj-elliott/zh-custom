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

#include "Common/CameraHeightScale.h"
#include "Common/GlobalData.h"
#include "GameNetwork/NetworkDefs.h"	// TheNetwork

namespace CameraHeightScale
{
	static Real s_userScale = 0.0f;

	void SetUserScale(Real scale)
	{
		s_userScale = scale;
	}

	Real GetEffectiveScale()
	{
		// In a network game every player must see the same ceiling, so the per-machine value is ignored there.
		const Bool useUserScale = (TheNetwork == nullptr) && (s_userScale > 0.0f);
		return useUserScale ? s_userScale : DefaultScale;
	}

	Real GetMaxCameraHeight()
	{
		return TheGlobalData->m_maxCameraHeight * GetEffectiveScale();
	}
}
