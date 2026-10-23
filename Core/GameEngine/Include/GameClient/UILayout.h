// zh-pipeline @feature 2026-10-22 Menu UI system: layout descriptions (docs/UI-DESIGN.md §5 and §6.4).
// A redesigned screen keeps its .wnd file (window names, callbacks, behaviour) and gets a UI\Layouts\<Screen>.layout
// text file that places the named windows in the 16:9 content frame in design units, with anchors and stacks, and marks
// which of them the procedural renderers draw. Applied right after the window tree is created, before the screen's init
// callback. Windows the file does not mention keep their legacy position.
//
//   # comment
//   hide   <window>                                 drawn by nothing (SEE_THRU), keeps input off
//   window <window> <ax> <ay> <x> <y> <w> <h> [style]   ax: left|centre|right, ay: top|middle|bottom; x,y offsets from
//                                                       the anchored edges, all in design units
//   stack  <ax> <ay> <x> <y> <w> <gap>              a vertical stack; items follow until `end`
//     item <window> <h> [style]
//     space <units>
//   end
//   styles: menu (main-menu button), button, primary, danger, label (static text in the label role)
#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

class GameWindow;

namespace UILayout
{
	enum Style
	{
		STYLE_NONE = 0,
		STYLE_HIDDEN,        // placed by a layout with `hide`: draws nothing, and the shell's flash transitions skip it
		STYLE_MENU_BUTTON,
		STYLE_BUTTON,
		STYLE_PRIMARY,
		STYLE_DANGER,
		STYLE_LABEL
	};

	/// Looks for UI\Layouts\<basename of wndFile>.layout and applies it under root. TRUE when a layout file was found.
	Bool apply( const AsciiString &wndFile, GameWindow *root );

	Style styleOf( const GameWindow *window );   ///< how the procedural renderers should draw this window, if at all
	Bool isManaged( const GameWindow *window );  ///< a layout placed or hid this window
	void forget( const GameWindow *window );     ///< called when a window is destroyed
	void reset();                                ///< drop every record (shutdown)
}
