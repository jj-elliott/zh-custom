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
//   dialog <ax> <ay> <x> <y> <w> <h>                   decoration: a panel on the raised fill, for a dialog over a screen
//   scrim                                              decoration: dims the whole display under the dialog
//   <window> may also be  text:<label key>  (a window whose TEXT label is that key, for unnamed windows) or
//   parent:<window>  (the parent of that window).
//   styles: menu, button, primary, danger (push buttons); label (centred), caption (left), meta, section, title (static
//   text, one line), value / heading (centred table cell and column heading), body (static text, word-wrapped by the
//   stock draw); panel / dialog (a USER window drawn as a panel
//   or a raised dialog panel); entry, dropdown, checkbox, list, slider, radio.
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
		STYLE_RADIO,         // radio buttons drawn as segments of a segmented control
		STYLE_BODY,          // static text kept on the stock draw (it word-wraps), body font, chrome cleared: message bodies
		STYLE_DIALOG,        // a USER window drawn as a dialog panel (raised fill); follows the window if code moves it
		STYLE_VALUE,         // static text, label role, centred, primary colour: a table cell
		STYLE_HEADING        // static text, meta role, centred, secondary colour: a table column heading
	};

	/// Looks for UI\Layouts\<basename of wndFile>.layout and applies it under root. TRUE when a layout file was found.
	Bool apply( const AsciiString &wndFile, GameWindow *root );

	Style styleOf( const GameWindow *window );          ///< this window's own style
	Style effectiveStyle( const GameWindow *window );   ///< own style, else the nearest styled ancestor's (gadget children)
	Bool isManaged( const GameWindow *window );         ///< a layout placed or hid this window
	Bool isManagedTree( const GameWindow *window );     ///< this window or an ancestor is managed: the screen has a layout
	void forget( const GameWindow *window );            ///< called when a window is destroyed
	void reset();                                       ///< drop every record (shutdown)

	/// Draws the decorations of the layout whose root is this top-level window, right before the manager draws it, so
	/// they sit under that screen's windows and over everything beneath it (a dialog's scrim over the screen below).
	void drawDecorations( const GameWindow *root );
}
