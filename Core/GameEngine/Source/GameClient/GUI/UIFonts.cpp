// zh-pipeline @feature 2026-10-22 Menu UI system: typefaces. See GameClient/UIFonts.h.
#include "PreRTS.h"

#include <windows.h>
#include <vector>
#include "GameClient/UIFonts.h"
#include "GameClient/UIStyle.h"
#include "GameClient/GameFont.h"

namespace UIFonts
{

namespace
{
	Bool s_initDone = FALSE;
	Bool s_available = FALSE;
	std::vector<AsciiString> s_registered;   // file paths handed to AddFontResourceEx

	// GDI family names as the shipped files declare them (name table ID 1): the static cuts register as their own families
	const char *const FACE_BARLOW_MEDIUM   = "Barlow Medium";
	const char *const FACE_BARLOW_SEMIBOLD = "Barlow SemiBold";
	const char *const FACE_SEMICOND_MEDIUM = "Barlow Semi Condensed Medium";
	const char *const FACE_SEMICOND_SEMIBOLD = "Barlow Semi Condensed SemiBold";
	const char *const FACE_STENCIL         = "Saira Stencil One";
	const char *const FACE_FALLBACK        = "Arial";
}

void ensureInit()
{
	if( s_initDone )
		return;
	s_initDone = TRUE;

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA( "Fonts\\*.ttf", &fd );
	if( h == INVALID_HANDLE_VALUE )
		return;
	Int count = 0;
	do
	{
		AsciiString path;
		path.format( "Fonts\\%s", fd.cFileName );
		if( AddFontResourceExA( path.str(), FR_PRIVATE, 0 ) > 0 )
		{
			s_registered.push_back( path );
			++count;
		}
	}
	while( FindNextFileA( h, &fd ) );
	FindClose( h );
	// the Barlow cuts are the minimum; the stencil face is optional (title falls back to Semi Condensed)
	s_available = count >= 2;
}

void shutdown()
{
	for( size_t i = 0; i < s_registered.size(); ++i )
		RemoveFontResourceExA( s_registered[ i ].str(), FR_PRIVATE, 0 );
	s_registered.clear();
	s_available = FALSE;
	s_initDone = FALSE;
}

Bool available()
{
	ensureInit();
	return s_available;
}

Bool substitute( const AsciiString &name, Bool bold, AsciiString &outName, Bool &outBold )
{
	if( !UIStyle::isEnabled() || !available() )
		return FALSE;
	const char *n = name.str();
	if( n == nullptr || *n == 0 )
		return FALSE;
	// body faces of the 2003 shell
	if( stricmp( n, "Arial" ) == 0 || stricmp( n, "Times New Roman" ) == 0 || stricmp( n, "Tahoma" ) == 0 ||
			stricmp( n, "Verdana" ) == 0 || stricmp( n, "MS Sans Serif" ) == 0 )
	{
		outName = bold ? FACE_BARLOW_SEMIBOLD : FACE_BARLOW_MEDIUM;
		outBold = FALSE;   // the weight is in the family; FW_BOLD on top would be synthesised
		return TRUE;
	}
	// display faces: the game's own "Generals" and the Placard headings
	if( stricmp( n, "Generals" ) == 0 || stricmp( n, "Placard MT Condensed" ) == 0 )
	{
		outName = FACE_SEMICOND_SEMIBOLD;
		outBold = FALSE;
		return TRUE;
	}
	// Courier (mission briefings, copyright line) and the Unicode fallback stay as they are
	return FALSE;
}

const char *face( Role role )
{
	if( !available() )
		return FACE_FALLBACK;
	switch( role )
	{
		case ROLE_TITLE:   return FACE_STENCIL;
		case ROLE_MENU:    return FACE_SEMICOND_SEMIBOLD;
		case ROLE_SECTION: return FACE_SEMICOND_SEMIBOLD;
		case ROLE_LABEL:
		case ROLE_BODY:
		case ROLE_TABLE:
		case ROLE_META:
		default:           return FACE_BARLOW_MEDIUM;
	}
}

Real sizeUnits( Role role )
{
	switch( role )
	{
		case ROLE_TITLE:   return 40.0f;
		case ROLE_MENU:    return 26.0f;
		case ROLE_SECTION: return 22.0f;
		case ROLE_LABEL:   return 17.0f;
		case ROLE_BODY:    return 17.0f;
		case ROLE_TABLE:   return 15.0f;
		case ROLE_META:    return 13.0f;
		default:           return 17.0f;
	}
}

Bool smallCaps( Role role )
{
	return role == ROLE_MENU || role == ROLE_SECTION || role == ROLE_LABEL || role == ROLE_META;
}

Bool uppercase( Role role )
{
	return role == ROLE_TITLE;
}

GameFont *font( Role role, Real scale )
{
	if( TheFontLibrary == nullptr )
		return nullptr;
	// without the shipped faces, Arial bold stands in for the heavy roles
	const Bool bold = !available() && ( role == ROLE_TITLE || role == ROLE_MENU || role == ROLE_SECTION );
	// Create_GDI_Font maps the "point size" at 96 dpi, so one point is 4/3 px; the role sizes are px-style units
	Int points = UIStyle::u( sizeUnits( role ) * scale * 0.75f );
	if( points < 6 ) points = 6;
	return TheFontLibrary->getFont( AsciiString( face( role ) ), points, bold );
}

} // namespace UIFonts
