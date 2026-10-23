// zh-pipeline @feature 2026-10-22 Menu UI system: the component sheet drawn by the engine (docs/UI-DESIGN.md §7 step 2).
// Started with -uitest; drawn over the shell after the window manager so every primitive and control style can be
// judged from a screenshot at any resolution before a real screen uses them.
#pragma once

namespace UIStyleTest
{
	void draw();
	void shutdown();   ///< frees the cached display strings
}
