// zh-pipeline @feature 2026-10-22 Menu UI system: typefaces (docs/UI-DESIGN.md §3.2 and §6.5).
// Fonts ship as .ttf files under <gameDir>\Fonts\ (SIL OFL, licence text beside them) and are registered process-private
// at first use, so nothing is installed on the machine. Historical face names from the .wnd files and INIs are mapped
// onto the new faces by FontLibrary::getFont while the system is enabled (-classicui restores the originals).
#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

class GameFont;

namespace UIFonts
{
	void ensureInit();     ///< registers Fonts\*.ttf once; safe to call from anywhere, any number of times
	void shutdown();       ///< unregisters them
	Bool available();      ///< the Barlow faces registered (otherwise everything falls back to Arial)

	/// Legacy name mapping for FontLibrary::getFont. Returns TRUE when (name, bold) was replaced.
	Bool substitute( const AsciiString &name, Bool bold, AsciiString &outName, Bool &outBold );

	// Type roles (§3.2): face, size in design units, and casing
	enum Role
	{
		ROLE_TITLE = 0,   // screen title: Saira Stencil One, uppercase
		ROLE_MENU,        // main-menu button: Semi Condensed SemiBold, small caps, tracked
		ROLE_SECTION,     // panel heading: Semi Condensed SemiBold, small caps
		ROLE_LABEL,       // control label / button text: Barlow Medium, small caps
		ROLE_BODY,        // names, map names, list text: Barlow Medium, mixed case
		ROLE_TABLE,       // table text: Barlow Medium, tabular
		ROLE_META,        // hints and captions: Barlow Medium, small caps
		ROLE_COUNT
	};
	const char *face( Role role );
	Real sizeUnits( Role role );
	Bool smallCaps( Role role );
	Bool uppercase( Role role );
	GameFont *font( Role role, Real scale = 1.0f );   ///< resolved through TheFontLibrary at the role's size in screen pixels
}
