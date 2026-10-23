// zh-pipeline @feature 2026-10-22 Menu UI system: layout descriptions. See GameClient/UILayout.h.
#include "PreRTS.h"

#include <vector>
#include <string.h>
#include <stdlib.h>
#include "GameClient/UILayout.h"
#include "GameClient/UIStyle.h"
#include "GameClient/GameWindow.h"
#include "GameClient/WinInstanceData.h"
#include "GameClient/GameWindowManager.h"
#include "Common/NameKeyGenerator.h"
#include "Common/FileSystem.h"
#include "Common/File.h"

namespace UILayout
{

namespace
{
	struct Rec { const GameWindow *win; Style style; };
	std::vector<Rec> s_styles;

	void record( GameWindow *w, Style st )
	{
		if( w == nullptr || st == STYLE_NONE )
			return;
		for( size_t i = 0; i < s_styles.size(); ++i )
			if( s_styles[ i ].win == w )
			{
				s_styles[ i ].style = st;
				return;
			}
		Rec r; r.win = w; r.style = st;
		s_styles.push_back( r );
	}

	Int anchorFrom( const char *s, Bool vertical )
	{
		if( s == nullptr ) return 0;
		if( vertical )
		{
			if( stricmp( s, "middle" ) == 0 || stricmp( s, "centre" ) == 0 || stricmp( s, "center" ) == 0 ) return 1;
			if( stricmp( s, "bottom" ) == 0 ) return 2;
			return 0;
		}
		if( stricmp( s, "centre" ) == 0 || stricmp( s, "center" ) == 0 ) return 1;
		if( stricmp( s, "right" ) == 0 ) return 2;
		return 0;
	}

	Style styleFrom( const char *s )
	{
		if( s == nullptr ) return STYLE_NONE;
		if( stricmp( s, "menu" ) == 0 ) return STYLE_MENU_BUTTON;
		if( stricmp( s, "button" ) == 0 ) return STYLE_BUTTON;
		if( stricmp( s, "primary" ) == 0 ) return STYLE_PRIMARY;
		if( stricmp( s, "danger" ) == 0 ) return STYLE_DANGER;
		if( stricmp( s, "label" ) == 0 ) return STYLE_LABEL;
		return STYLE_NONE;
	}

	// screen x of a box of width w (units) at offset x (units) from the anchored edge of the frame
	Int screenX( Int ax, Real x, Real w )
	{
		Int fx, fy, fw, fh;
		UIStyle::frame( fx, fy, fw, fh );
		switch( ax )
		{
			case 1: return fx + ( fw - UIStyle::u( w ) ) / 2 + UIStyle::u( x );
			case 2: return fx + fw - UIStyle::u( x ) - UIStyle::u( w );
			default: return fx + UIStyle::u( x );
		}
	}

	Int screenY( Int ay, Real y, Real h )
	{
		Int fx, fy, fw, fh;
		UIStyle::frame( fx, fy, fw, fh );
		switch( ay )
		{
			case 1: return fy + ( fh - UIStyle::u( h ) ) / 2 + UIStyle::u( y );
			case 2: return fy + fh - UIStyle::u( y ) - UIStyle::u( h );
			default: return fy + UIStyle::u( y );
		}
	}

	GameWindow *find( GameWindow *root, const char *name )
	{
		if( TheWindowManager == nullptr || TheNameKeyGenerator == nullptr || name == nullptr )
			return nullptr;
		const NameKeyType key = TheNameKeyGenerator->nameToKey( name );
		GameWindow *w = TheWindowManager->winGetWindowFromId( root, key );
		if( w == nullptr )
			w = TheWindowManager->winGetWindowFromId( nullptr, key );
		return w;
	}

	void place( GameWindow *w, Int sx, Int sy, Int sw, Int sh )
	{
		Int px = 0, py = 0;
		GameWindow *parent = w->winGetParent();
		if( parent )
			parent->winGetScreenPosition( &px, &py );
		w->winSetPosition( sx - px, sy - py );
		w->winSetSize( sw, sh );
	}

	// `hide`: the window draws nothing but keeps its children, input and show/hide behaviour (the shell toggles its panels)
	void noDraw( GameWindow *, WinInstanceData * ) {}

	// whitespace tokeniser for one line; '#' starts a comment
	Int tokenise( char *line, char *tok[], Int maxTok )
	{
		Int n = 0;
		char *p = line;
		while( *p && n < maxTok )
		{
			while( *p == ' ' || *p == '\t' || *p == '\r' ) ++p;
			if( *p == 0 || *p == '#' ) break;
			tok[ n++ ] = p;
			while( *p && *p != ' ' && *p != '\t' && *p != '\r' ) ++p;
			if( *p ) *p++ = 0;
		}
		return n;
	}
}

Bool apply( const AsciiString &wndFile, GameWindow *root )
{
	if( !UIStyle::isEnabled() || root == nullptr || TheFileSystem == nullptr )
		return FALSE;

	// UI\Layouts\<stem>.layout
	const char *f = wndFile.str();
	const char *base = f;
	for( const char *c = f; *c; ++c )
		if( *c == '\\' || *c == '/' ) base = c + 1;
	char stem[ 128 ];
	strlcpy( stem, base, sizeof( stem ) );
	char *dot = strrchr( stem, '.' );
	if( dot ) *dot = 0;
	AsciiString path;
	path.format( "UI\\Layouts\\%s.layout", stem );

	File *file = TheFileSystem->openFile( path.str(), File::READ | File::TEXT );
	if( file == nullptr )
		return FALSE;
	char *data = file->readEntireAndClose();
	if( data == nullptr )
		return FALSE;

	Bool inStack = FALSE;
	Int sAx = 0, sAy = 0;
	Real sX = 0, sY = 0, sW = 0, sGap = 0, cursor = 0;
	Int placed = 0, missing = 0;

	char *line = data;
	while( line && *line )
	{
		char *next = strchr( line, '\n' );
		if( next ) *next++ = 0;
		char *tok[ 12 ];
		const Int n = tokenise( line, tok, 12 );
		if( n > 0 )
		{
			if( stricmp( tok[ 0 ], "hide" ) == 0 && n >= 2 )
			{
				GameWindow *w = find( root, tok[ 1 ] );
				if( w )
				{
					w->winSetDrawFunc( noDraw );
					++placed;
				}
				else ++missing;
			}
			else if( stricmp( tok[ 0 ], "window" ) == 0 && n >= 8 )
			{
				GameWindow *w = find( root, tok[ 1 ] );
				if( w )
				{
					const Int ax = anchorFrom( tok[ 2 ], FALSE ), ay = anchorFrom( tok[ 3 ], TRUE );
					const Real x = (Real)atof( tok[ 4 ] ), y = (Real)atof( tok[ 5 ] ), ww = (Real)atof( tok[ 6 ] ), hh = (Real)atof( tok[ 7 ] );
					place( w, screenX( ax, x, ww ), screenY( ay, y, hh ), UIStyle::u( ww ), UIStyle::u( hh ) );
					if( n >= 9 ) record( w, styleFrom( tok[ 8 ] ) );
					++placed;
				}
				else ++missing;
			}
			else if( stricmp( tok[ 0 ], "stack" ) == 0 && n >= 7 )
			{
				inStack = TRUE;
				sAx = anchorFrom( tok[ 1 ], FALSE ); sAy = anchorFrom( tok[ 2 ], TRUE );
				sX = (Real)atof( tok[ 3 ] ); sY = (Real)atof( tok[ 4 ] ); sW = (Real)atof( tok[ 5 ] ); sGap = (Real)atof( tok[ 6 ] );
				cursor = sY;
			}
			else if( stricmp( tok[ 0 ], "item" ) == 0 && n >= 3 && inStack )
			{
				const Real hh = (Real)atof( tok[ 2 ] );
				GameWindow *w = find( root, tok[ 1 ] );
				if( w )
				{
					place( w, screenX( sAx, sX, sW ), screenY( sAy, cursor, hh ), UIStyle::u( sW ), UIStyle::u( hh ) );
					if( n >= 4 ) record( w, styleFrom( tok[ 3 ] ) );
					++placed;
				}
				else ++missing;
				cursor += hh + sGap;
			}
			else if( stricmp( tok[ 0 ], "space" ) == 0 && n >= 2 && inStack )
			{
				cursor += (Real)atof( tok[ 1 ] );
			}
			else if( stricmp( tok[ 0 ], "end" ) == 0 )
			{
				inStack = FALSE;
			}
		}
		line = next;
	}
	delete [] data;
	DEBUG_LOG(( "UILayout: %s placed %d windows, %d names not found", path.str(), placed, missing ));
	return TRUE;
}

Style styleOf( const GameWindow *window )
{
	for( size_t i = 0; i < s_styles.size(); ++i )
		if( s_styles[ i ].win == window )
			return s_styles[ i ].style;
	return STYLE_NONE;
}

void forget( const GameWindow *window )
{
	for( size_t i = 0; i < s_styles.size(); ++i )
		if( s_styles[ i ].win == window )
		{
			s_styles[ i ] = s_styles.back();
			s_styles.pop_back();
			return;
		}
}

void reset()
{
	s_styles.clear();
}

} // namespace UILayout
