// zh-pipeline @feature 2026-10-22 Menu UI system: layout descriptions (docs/UI-DESIGN.md §5 and §6.4).
// A redesigned screen keeps its .wnd file (window names, callbacks, behaviour) and gets a UI\Layouts\<Screen>.layout
// text file that places the named windows in the 16:9 content frame in design units, with anchors and stacks, marks
// which of them the procedural renderers draw, and adds decorations (panels, rules, rulers) drawn under the windows.
// Applied right after the window tree is created, before the screen's init callback. Windows the file does not mention
// keep their legacy position.
//
//   # comment
//   hide   <window>                                   draws nothing (children, input and show/hide behaviour untouched)
//   window <window> <ax> <ay> <x> <y> <w> <h> [style]  ax: left|centre|right, ay: top|middle|bottom; x,y offsets from the
//                                                      anchored frame edges; all in design units
//   stack  <ax> <ay> <x> <y> <w> <gap>                 a vertical stack; items follow until `end`
//     item <window> <h> [style]
//     space <units>
//   end
//   panel  <ax> <ay> <x> <y> <w> <h>                   decoration: a panel (fill, grid, hairline, brackets) under the windows
//   rule   <ax> <ay> <x> <y> <w>                       decoration: the two-tone rule
//   ruler  <ax> <ay> <x> <y> <w>                       decoration: a tick strip
//   band   <ax> <ay> <x> <y> <w> <h>                   decoration: a frame band (translucent, drafting grid, hairline edge)
//   <window> may also be  text:<label key>  (a window whose TEXT label is that key, for unnamed windows) or
//   parent:<window>  (the parent of that window).
//   styles: menu, button, primary, danger (push buttons); label (centred), caption (left), meta, section, title (static
//   text); panel (a USER window drawn as a panel); entry, dropdown, checkbox, list, slider, radio.
//   `$Name` is short for `<Screen>.wnd:Name`; `include <file>` reads another file from UI\Layouts (shared blocks).
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
		STYLE_LABEL,         // static text, label role, centred
		STYLE_CAPTION,       // static text, label role, left
		STYLE_META,          // static text, meta role, left
		STYLE_SECTION,       // static text, section role, left
		STYLE_TITLE,         // static text, title role (stencil, uppercase), left
		STYLE_PANEL,         // a USER window drawn as a panel
		STYLE_ENTRY,
		STYLE_DROPDOWN,
		STYLE_CHECKBOX,
		STYLE_LIST,
		STYLE_SLIDER,
		STYLE_RADIO          // radio buttons drawn as segments of a segmented control
	};

	/// Looks for UI\Layouts\<basename of wndFile>.layout and applies it under root. TRUE when a layout file was found.
	Bool apply( const AsciiString &wndFile, GameWindow *root );

	Style styleOf( const GameWindow *window );          ///< this window's own style
	Style effectiveStyle( const GameWindow *window );   ///< own style, else the nearest styled ancestor's (gadget children)
	Bool isManaged( const GameWindow *window );         ///< a layout placed or hid this window
	void forget( const GameWindow *window );            ///< called when a window is destroyed
	void reset();                                       ///< drop every record (shutdown)

	/// Draws the decorations of every live, visible layout. Called before the window manager repaints.
	void drawDecorations();
}
