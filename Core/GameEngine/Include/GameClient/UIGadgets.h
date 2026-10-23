// zh-pipeline @feature 2026-10-22 Menu UI system: gadget renderers for windows a layout put on the new system
// (docs/UI-DESIGN.md §4 and §6.3). The W3D gadget draw functions call these first and return when they drew.
#pragma once

#include "Lib/BaseType.h"
#include "GameClient/UILayout.h"

class GameWindow;
class WinInstanceData;

namespace UIGadgets
{
	/// Draws a push button (frame and text) in the given layout style. Called from the W3D push-button draw functions.
	void drawPushButton( GameWindow *window, WinInstanceData *instData, UILayout::Style style );
	/// Draws static text in the label role.
	void drawStaticText( GameWindow *window, WinInstanceData *instData, UILayout::Style style );
}
