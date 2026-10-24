// zh-pipeline @feature 2026-10-22 Menu UI system: gadget renderers for windows a layout put on the new system
// (docs/UI-DESIGN.md §4 and §6.3). The W3D gadget draw functions call these first. Two kinds: full replacements
// (push button, static text, check box, sliders, panel) and "chrome" that is drawn first while the stock function
// then draws its own text, caret and selection with its colours cleared (entry, combo box, list box).
#pragma once

#include "Lib/BaseType.h"
#include "GameClient/UILayout.h"

class GameWindow;
class WinInstanceData;

namespace UIGadgets
{
	// full replacements: the caller returns after these
	void drawPushButton( GameWindow *window, WinInstanceData *instData, UILayout::Style style );
	void drawStaticText( GameWindow *window, WinInstanceData *instData, UILayout::Style style );
	void drawCheckBox( GameWindow *window, WinInstanceData *instData );
	void drawRadioButton( GameWindow *window, WinInstanceData *instData );
	void drawHorizontalSlider( GameWindow *window, WinInstanceData *instData );
	void drawVerticalSlider( GameWindow *window, WinInstanceData *instData );
	void drawPanel( GameWindow *window, WinInstanceData *instData );
	void drawDialog( GameWindow *window, WinInstanceData *instData );

	// chrome only: the caller continues with its own text drawing
	void drawEntryChrome( GameWindow *window, WinInstanceData *instData );
	void drawComboChrome( GameWindow *window, WinInstanceData *instData );
	void drawListChrome( GameWindow *window, WinInstanceData *instData );
}
