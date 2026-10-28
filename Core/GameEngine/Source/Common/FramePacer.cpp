/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
#include "PreRTS.h"

#include "Common/FramePacer.h"

#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"


FramePacer* TheFramePacer = nullptr;

FramePacer::FramePacer()
{
	// Set the time slice size to 1 ms.
	timeBeginPeriod(1);

	m_maxFPS = BaseFps;
	m_logicTimeScaleFPS = LOGICFRAMES_PER_SECOND;
	m_scriptLogicFpsLimit = 0;
	m_updateTime = 1.0f / (Real)BaseFps; // initialized to something to avoid division by zero on first use
	m_logicFramePhase = 1.0f;
	m_enableFpsLimit = FALSE;
	// zh-pipeline @tweak 2026-10-22 Logic time scale on by default (30 logic fps), so an uncapped render rate does not drag
	// the simulation along with it (with it off, getActualLogicTimeScaleFps returns "uncapped" to mimic the original game,
	// where game speed followed the frame rate). Network games are paced by TheNetwork regardless; the fast-forward and
	// hotkey paths toggle this flag themselves.
	m_enableLogicTimeScale = TRUE;
	m_isTimeFrozen = FALSE;
	m_isGameHalted = FALSE;
}

FramePacer::~FramePacer()
{
	// Restore the previous time slice for Windows.
	timeEndPeriod(1);
}

void FramePacer::update()
{
	// Uses a high resolution counter to cap the frame rate more accurately to the desired limit than retail did.
	const UnsignedInt maxFps = getActualFramesPerSecondLimit();
	m_updateTime = m_frameRateLimit.wait(maxFps);

	if (TheGameLogic != nullptr)
	{
		// Set or advance the logic frame phase by the render step that the next update will draw.
		// It is capped at a whole logic frame, because the render steps in between can add up to more than one.
		// Consumers are expected to interpolate towards the next logic frame and not extrapolate past it.
		const Real timeScale = getActualLogicTimeScaleOverFpsRatio();

		if (TheGameLogic->hasUpdated())
		{
			m_logicFramePhase = timeScale;
		}
		else
		{
			m_logicFramePhase = min(1.0f, m_logicFramePhase + timeScale);
		}
	}
}

void FramePacer::reset()
{
	m_frameRateLimit.reset();
	m_updateTime = 1.0f / (Real)getActualFramesPerSecondLimit();
	m_logicFramePhase = 1.0f;
}

void FramePacer::setFramesPerSecondLimit( Int fps )
{
	DEBUG_LOG(("FramePacer::setFramesPerSecondLimit() - setting max fps to %d (TheGlobalData->m_useFpsLimit == %d)", fps, TheGlobalData->m_useFpsLimit));
	m_maxFPS = fps;
}

Int FramePacer::getFramesPerSecondLimit()  const
{
	return m_maxFPS;
}

void FramePacer::enableFramesPerSecondLimit( Bool enable )
{
	m_enableFpsLimit = enable;
}

Bool FramePacer::isFramesPerSecondLimitEnabled() const
{
	return m_enableFpsLimit;
}

Bool FramePacer::isActualFramesPerSecondLimitEnabled() const
{
	Bool allowFpsLimit = true;

	if (TheTacticalView != nullptr)
	{
		allowFpsLimit &= TheTacticalView->getTimeMultiplier()<=1 && !TheScriptEngine->isTimeFast();
	}

	if (TheGameLogic != nullptr)
	{
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		allowFpsLimit &= !(!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode);
#else	//always allow this cheat key if we're in a replay game.
		allowFpsLimit &= !(!TheGameLogic->isGamePaused() && TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame());
#endif
	}

	allowFpsLimit &= TheGlobalData->m_useFpsLimit;
	allowFpsLimit &= isFramesPerSecondLimitEnabled();

	return allowFpsLimit;
}

Int FramePacer::getActualFramesPerSecondLimit() const
{
	return isActualFramesPerSecondLimitEnabled() ? getFramesPerSecondLimit() : RenderFpsPreset::UncappedFpsValue;
}

Real FramePacer::getUpdateTime()  const
{
	return m_updateTime;
}

Real FramePacer::getUpdateFps()  const
{
	return 1.0f / m_updateTime;
}

Real FramePacer::getBaseOverUpdateFpsRatio(Real minUpdateFps)
{
	// Update fps is floored to default 5 fps, 200 ms.
	// Useful to prevent insane ratios on frame spikes/stalls.
	return (Real)BaseFps / std::max(getUpdateFps(), minUpdateFps);
}

void FramePacer::setTimeFrozen(Bool frozen)
{
	m_isTimeFrozen = frozen;
}

void FramePacer::setGameHalted(Bool halted)
{
	m_isGameHalted = halted;
}

Bool FramePacer::isTimeFrozen() const
{
	return m_isTimeFrozen;
}

Bool FramePacer::isGameHalted() const
{
	return m_isGameHalted;
}

void FramePacer::setLogicTimeScaleFps( Int fps )
{
	m_logicTimeScaleFPS = fps;
}

Int FramePacer::getLogicTimeScaleFps() const
{
	return m_logicTimeScaleFPS;
}

void FramePacer::enableLogicTimeScale( Bool enable )
{
	m_enableLogicTimeScale = enable;
}

Bool FramePacer::isLogicTimeScaleEnabled() const
{
	return m_enableLogicTimeScale;
}

Int FramePacer::getActualLogicTimeScaleFps(LogicTimeQueryFlags flags) const
{
	if (m_isTimeFrozen && (flags & IgnoreFrozenTime) == 0)
	{
		return 0;
	}

	if (m_isGameHalted && (flags & IgnoreHaltedGame) == 0)
	{
		return 0;
	}

	if (TheNetwork != nullptr)
	{
		return TheNetwork->getFrameRate();
	}

	// Returns uncapped value to align with the render update as per the original game behavior.
	Int fps = isLogicTimeScaleEnabled() ? getLogicTimeScaleFps() : (Int)RenderFpsPreset::UncappedFpsValue;

	// zh-pipeline @fix 2026-10-27 Campaign cutscenes set a 20 fps limit by script. In retail the logic ran once per render
	// frame, so that slowed the scene's game time to 20 Hz; apply it to the logic rate only, so the timing is kept and
	// the render stays uncapped.
	if (m_scriptLogicFpsLimit > 0)
	{
		fps = min(fps, m_scriptLogicFpsLimit);
	}

	return fps;
}

void FramePacer::setScriptLogicFpsLimit( Int fps )
{
	m_scriptLogicFpsLimit = max(fps, 0);
}

Int FramePacer::getScriptLogicFpsLimit() const
{
	return m_scriptLogicFpsLimit;
}

Real FramePacer::getActualLogicTimeScaleRatio(LogicTimeQueryFlags flags) const
{
	return (Real)getActualLogicTimeScaleFps(flags) / LOGICFRAMES_PER_SECONDS_REAL;
}

Real FramePacer::getActualLogicTimeScaleOverFpsRatio(LogicTimeQueryFlags flags) const
{
	// Clamps ratio to min 1, because the logic frame rate is currently capped by the render frame rate.
	return min(1.0f, (Real)getActualLogicTimeScaleFps(flags) / getUpdateFps());
}

Real FramePacer::getLogicTimeStepSeconds(LogicTimeQueryFlags flags) const
{
	return SECONDS_PER_LOGICFRAME_REAL * getActualLogicTimeScaleOverFpsRatio(flags);
}

Real FramePacer::getLogicTimeStepMilliseconds(LogicTimeQueryFlags flags) const
{
	return MSEC_PER_LOGICFRAME_REAL * getActualLogicTimeScaleOverFpsRatio(flags);
}

Real FramePacer::getLogicFramePhase() const
{
	return m_logicFramePhase;
}
