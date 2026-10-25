// zh-pipeline @feature 2026-10-22 Menu UI system: layout descriptions. See GameClient/UILayout.h.
#include "PreRTS.h"

#include <vector>
#include <string.h>
#include <stdlib.h>
#include "GameClient/UILayout.h"
#include "GameClient/UIStyle.h"
#include "GameClient/UIFonts.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Display.h"
#include <stdio.h>
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/WinInstanceData.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetStaticText.h"
#include "Common/NameKeyGenerator.h"
#include "Common/FileSystem.h"
#include "Common/File.h"

namespace UILayout
{

namespace
{
	struct Rec { const GameWindow *win; Style style; };
	std::vector<Rec> s_styles;

	enum DecoKind { DECO_PANEL, DECO_RULE, DECO_RULER, DECO_BAND, DECO_DIALOG, DECO_SCRIM };
	struct Deco { const GameWindow *root; DecoKind kind; Int ax, ay; Real x, y, w, h; };
	std::vector<Deco> s_decos;

	// `hide`: the window draws nothing but keeps its children, input and show/hide behaviour (the shell toggles its panels)
	void noDraw( GameWindow *, WinInstanceData * ) {}

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
		if( stricmp( s, "caption" ) == 0 ) return STYLE_CAPTION;
		if( stricmp( s, "meta" ) == 0 ) return STYLE_META;
		if( stricmp( s, "section" ) == 0 ) return STYLE_SECTION;
		if( stricmp( s, "title" ) == 0 ) return STYLE_TITLE;
		if( stricmp( s, "panel" ) == 0 ) return STYLE_PANEL;
		if( stricmp( s, "entry" ) == 0 ) return STYLE_ENTRY;
		if( stricmp( s, "dropdown" ) == 0 ) return STYLE_DROPDOWN;
		if( stricmp( s, "checkbox" ) == 0 ) return STYLE_CHECKBOX;
		if( stricmp( s, "list" ) == 0 ) return STYLE_LIST;
		if( stricmp( s, "slider" ) == 0 ) return STYLE_SLIDER;
		if( stricmp( s, "radio" ) == 0 ) return STYLE_RADIO;
		if( stricmp( s, "body" ) == 0 ) return STYLE_BODY;
		if( stricmp( s, "value" ) == 0 ) return STYLE_VALUE;
		if( stricmp( s, "heading" ) == 0 ) return STYLE_HEADING;
		if( stricmp( s, "dialog" ) == 0 ) return STYLE_DIALOG;
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

	GameWindow *findByLabel( GameWindow *w, const char *label )
	{
		const AsciiString want( label );
		for( ; w; w = w->winGetNext() )
		{
			WinInstanceData *d = w->winGetInstanceData();
			if( d && d->m_textLabelString.compareNoCase( want ) == 0 )
				return w;
			GameWindow *hit = findByLabel( w->winGetChild(), label );
			if( hit )
				return hit;
		}
		return nullptr;
	}

	// a window whose screen rectangle is where the legacy 800x600 mapping put the design rect l,t,r,b (unnamed windows;
	// must come before any line that moves one of its ancestors)
	GameWindow *findByRect( GameWindow *w, Int l, Int t, Int r, Int b )
	{
		const Real sx = ( TheDisplay ? TheDisplay->getWidth() : 800 ) / 800.0f;
		const Real sy = ( TheDisplay ? TheDisplay->getHeight() : 600 ) / 600.0f;
		const Int ex = (Int)( l * sx ), ey = (Int)( t * sy ), ew = (Int)( r * sx ) - (Int)( l * sx ), eh = (Int)( b * sy ) - (Int)( t * sy );
		for( ; w; w = w->winGetNext() )
		{
			Int x, y, cw, ch;
			w->winGetScreenPosition( &x, &y );
			w->winGetSize( &cw, &ch );
			if( abs( x - ex ) <= 3 && abs( y - ey ) <= 3 && abs( cw - ew ) <= 3 && abs( ch - eh ) <= 3 )
				return w;
			GameWindow *hit = findByRect( w->winGetChild(), l, t, r, b );
			if( hit )
				return hit;
		}
		return nullptr;
	}

	char s_stem[ 128 ] = "";   // the screen being laid out, for the `$Name` shorthand

	GameWindow *find( GameWindow *root, const char *name )
	{
		if( TheWindowManager == nullptr || TheNameKeyGenerator == nullptr || name == nullptr )
			return nullptr;
		if( name[ 0 ] == '$' )
		{
			AsciiString full;
			full.format( "%s.wnd:%s", s_stem, name + 1 );
			return find( root, full.str() );
		}
		if( strncmp( name, "text:", 5 ) == 0 )
			return findByLabel( root, name + 5 );
		if( strncmp( name, "at:", 3 ) == 0 )
		{
			Int l = 0, t = 0, r = 0, b = 0;
			if( sscanf( name + 3, "%d,%d,%d,%d", &l, &t, &r, &b ) == 4 )
				return findByRect( root, l, t, r, b );
			return nullptr;
		}
		if( strncmp( name, "parent:", 7 ) == 0 )
		{
			GameWindow *w = find( root, name + 7 );
			return w ? w->winGetParent() : nullptr;
		}
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

	// the stock draw functions paint fill and border only when the colour is defined, so clearing the colours and
	// switching to the plain (non-image) draw function leaves them drawing just their text, cursor and selection
	void clearChrome( GameWindow *w )
	{
		if( w == nullptr ) return;
		for( Int i = 0; i < 3; ++i )
		{
			w->winSetEnabledColor( i, WIN_COLOR_UNDEFINED );
			w->winSetEnabledBorderColor( i, WIN_COLOR_UNDEFINED );
			w->winSetDisabledColor( i, WIN_COLOR_UNDEFINED );
			w->winSetDisabledBorderColor( i, WIN_COLOR_UNDEFINED );
			w->winSetHiliteColor( i, WIN_COLOR_UNDEFINED );
			w->winSetHiliteBorderColor( i, WIN_COLOR_UNDEFINED );
		}
		const UIStyle::Palette &p = UIStyle::palette();
		w->winSetEnabledTextColors( p.textPrimary, 0 );
		w->winSetDisabledTextColors( p.textDisabled, 0 );
		w->winSetHiliteTextColors( p.textPrimary, 0 );
		w->winSetIMECompositeTextColors( p.textPrimary, 0 );
	}

	void setPlainDraw( GameWindow *w, GameWinDrawFunc f )
	{
		if( w && f )
			w->winSetDrawFunc( f );
	}

	void prepareList( GameWindow *list, UIFonts::Role role )
	{
		if( list == nullptr ) return;
		setPlainDraw( list, TheWindowManager->getListBoxDrawFunc() );
		clearChrome( list );
		list->winSetEnabledColor( 1, UIStyle::withAlpha( UIStyle::palette().signalBlue, 0.2f ) );
		GadgetListBoxSetFont( list, UIFonts::font( role ) );
		GameWindow *sl = GadgetListBoxGetSlider( list );
		if( sl )
		{
			setPlainDraw( sl, TheWindowManager->getVerticalSliderDrawFunc() ); clearChrome( sl );
			GameWindow *thumb = sl->winGetChild();
			if( thumb ) { setPlainDraw( thumb, TheWindowManager->getPushButtonDrawFunc() ); clearChrome( thumb ); }
		}
		GameWindow *up = GadgetListBoxGetUpButton( list ), *down = GadgetListBoxGetDownButton( list );
		if( up ) { setPlainDraw( up, TheWindowManager->getPushButtonDrawFunc() ); clearChrome( up ); }
		if( down ) { setPlainDraw( down, TheWindowManager->getPushButtonDrawFunc() ); clearChrome( down ); }
		// the stock scroll bar is 21 px wide at any resolution (a sliver at 4K); size its windows in design units and let
		// the list lay them out again. The thumb's height is kept proportional by the list (adjustDisplay) and the slider.
		{
			const Int sbw = UIStyle::u( UIStyle::Metric::ScrollBarWidth );
			Int sw = 0, sh = 0;
			if( up ) up->winSetSize( sbw, sbw );
			if( down ) down->winSetSize( sbw, sbw );
			if( sl ) { sl->winGetSize( &sw, &sh ); sl->winSetSize( sbw, sh ); }
			Int lw = 0, lh = 0;
			list->winGetSize( &lw, &lh );
			TheWindowManager->winSendSystemMsg( list, GGM_RESIZED, (WindowMsgData)lw, (WindowMsgData)lh );
		}
	}

	// one-time setup of a gadget a layout put on the new system
	void prepare( GameWindow *w, Style st )
	{
		if( w == nullptr || TheWindowManager == nullptr )
			return;
		switch( st )
		{
			case STYLE_ENTRY:
				setPlainDraw( w, TheWindowManager->getTextEntryDrawFunc() );
				clearChrome( w );
				GadgetTextEntrySetFont( w, UIFonts::font( UIFonts::ROLE_BODY ) );
				break;
			case STYLE_DROPDOWN:
			{
				setPlainDraw( w, TheWindowManager->getComboBoxDrawFunc() );
				clearChrome( w );
				GadgetComboBoxSetFont( w, UIFonts::font( UIFonts::ROLE_BODY ) );
				GameWindow *edit = GadgetComboBoxGetEditBox( w );
				GameWindow *btn = GadgetComboBoxGetDropDownButton( w );
				if( edit ) { setPlainDraw( edit, TheWindowManager->getTextEntryDrawFunc() ); clearChrome( edit ); }
				if( btn ) { setPlainDraw( btn, TheWindowManager->getPushButtonDrawFunc() ); clearChrome( btn ); }
				prepareList( GadgetComboBoxGetListBox( w ), UIFonts::ROLE_BODY );
				// the arrow button keeps the size it was created with (the 800x600 rect scaled), which left the edit box
				// a sliver wide; make it square and let the combo lay its children out again for the new size
				{
					Int cw = 0, ch = 0;
					w->winGetSize( &cw, &ch );
					if( btn ) btn->winSetSize( ch, ch );
					TheWindowManager->winSendSystemMsg( w, GGM_RESIZED, (WindowMsgData)cw, (WindowMsgData)ch );
				}
				break;
			}
			case STYLE_LIST:
				prepareList( w, UIFonts::ROLE_TABLE );
				break;
			case STYLE_CHECKBOX:
				setPlainDraw( w, TheWindowManager->getCheckBoxDrawFunc() );
				clearChrome( w );
				break;
			case STYLE_RADIO:
				setPlainDraw( w, TheWindowManager->getRadioButtonDrawFunc() );
				clearChrome( w );
				break;
			case STYLE_SLIDER:
			{
				setPlainDraw( w, TheWindowManager->getHorizontalSliderDrawFunc() );
				clearChrome( w );
				GameWindow *thumb = w->winGetChild();
				if( thumb ) { setPlainDraw( thumb, TheWindowManager->getPushButtonDrawFunc() ); clearChrome( thumb ); }
				break;
			}
			case STYLE_LABEL: case STYLE_CAPTION: case STYLE_META: case STYLE_SECTION: case STYLE_TITLE:
			case STYLE_VALUE: case STYLE_HEADING:
				setPlainDraw( w, TheWindowManager->getStaticTextDrawFunc() );
				clearChrome( w );
				break;
			case STYLE_PANEL: case STYLE_DIALOG:
				clearChrome( w );
				break;
			case STYLE_BODY:
				// the stock static-text draw wraps long text; it keeps drawing, in the body font and our colours
				setPlainDraw( w, TheWindowManager->getStaticTextDrawFunc() );
				clearChrome( w );
				GadgetStaticTextSetFont( w, UIFonts::font( UIFonts::ROLE_BODY ) );
				break;
			default:
				break;
		}
	}

	void record( GameWindow *w, Style st )
	{
		if( w == nullptr || st == STYLE_NONE )
			return;
		Bool found = FALSE;
		for( size_t i = 0; i < s_styles.size(); ++i )
			if( s_styles[ i ].win == w )
			{
				s_styles[ i ].style = st;
				found = TRUE;
			}
		if( !found )
		{
			Rec r; r.win = w; r.style = st;
			s_styles.push_back( r );
		}
		prepare( w, st );
	}

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

static Bool parseFile( const AsciiString &path, GameWindow *root, Int depth );

Bool apply( const AsciiString &wndFile, GameWindow *root )
{
	if( !UIStyle::isEnabled() || root == nullptr || TheFileSystem == nullptr )
		return FALSE;

	// UI\Layouts\<stem>.layout
	const char *f = wndFile.str();
	const char *base = f;
	for( const char *c = f; *c; ++c )
		if( *c == '\\' || *c == '/' ) base = c + 1;
	strlcpy( s_stem, base, sizeof( s_stem ) );
	char *dot = strrchr( s_stem, '.' );
	if( dot ) *dot = 0;
	AsciiString path;
	path.format( "UI\\Layouts\\%s.layout", s_stem );
	return parseFile( path, root, 0 );
}

static Bool parseFile( const AsciiString &path, GameWindow *root, Int depth )
{
	if( depth > 4 )
		return FALSE;
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
					record( w, STYLE_HIDDEN );
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
			else if( stricmp( tok[ 0 ], "include" ) == 0 && n >= 2 )
			{
				AsciiString inc;
				inc.format( "UI\\Layouts\\%s", tok[ 1 ] );
				parseFile( inc, root, depth + 1 );
			}
			else if( stricmp( tok[ 0 ], "scrim" ) == 0 )
			{
				Deco d;
				d.root = root; d.kind = DECO_SCRIM; d.ax = d.ay = 0; d.x = d.y = d.w = d.h = 0.0f;
				s_decos.push_back( d );
			}
			else if( ( ( stricmp( tok[ 0 ], "panel" ) == 0 || stricmp( tok[ 0 ], "band" ) == 0 || stricmp( tok[ 0 ], "dialog" ) == 0 ) && n >= 7 )
							 || ( stricmp( tok[ 0 ], "rule" ) == 0 && n >= 6 ) || ( stricmp( tok[ 0 ], "ruler" ) == 0 && n >= 6 ) )
			{
				Deco d;
				d.root = root;
				d.kind = ( stricmp( tok[ 0 ], "panel" ) == 0 ) ? DECO_PANEL : ( stricmp( tok[ 0 ], "band" ) == 0 ) ? DECO_BAND
							 : ( stricmp( tok[ 0 ], "dialog" ) == 0 ) ? DECO_DIALOG
							 : ( stricmp( tok[ 0 ], "rule" ) == 0 ) ? DECO_RULE : DECO_RULER;
				d.ax = anchorFrom( tok[ 1 ], FALSE ); d.ay = anchorFrom( tok[ 2 ], TRUE );
				d.x = (Real)atof( tok[ 3 ] ); d.y = (Real)atof( tok[ 4 ] ); d.w = (Real)atof( tok[ 5 ] );
				d.h = ( d.kind == DECO_PANEL || d.kind == DECO_BAND || d.kind == DECO_DIALOG ) ? (Real)atof( tok[ 6 ] )
						: ( ( d.kind == DECO_RULE ) ? 4.0f : 12.0f );
				s_decos.push_back( d );
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

Style effectiveStyle( const GameWindow *window )
{
	const GameWindow *w = window;
	for( Int depth = 0; w && depth < 4; ++depth )
	{
		const Style st = styleOf( w );
		if( st != STYLE_NONE )
			return st;
		w = const_cast<GameWindow *>( w )->winGetParent();
	}
	return STYLE_NONE;
}

Bool isManaged( const GameWindow *window )
{
	return styleOf( window ) != STYLE_NONE;
}

Bool isManagedTree( const GameWindow *window )
{
	for( const GameWindow *w = window; w; w = const_cast<GameWindow *>( w )->winGetParent() )
		if( isManaged( w ) )
			return TRUE;
	return FALSE;
}

void forget( const GameWindow *window )
{
	for( size_t i = 0; i < s_styles.size(); ++i )
		if( s_styles[ i ].win == window )
		{
			s_styles[ i ] = s_styles.back();
			s_styles.pop_back();
			break;
		}
	for( size_t i = 0; i < s_decos.size(); )
	{
		if( s_decos[ i ].root == window )
		{
			s_decos[ i ] = s_decos.back();
			s_decos.pop_back();
		}
		else ++i;
	}
}

void reset()
{
	s_styles.clear();
	s_decos.clear();
}

void drawDecorations( const GameWindow *forRoot )
{
	if( !UIStyle::isEnabled() || s_decos.empty() || forRoot == nullptr || const_cast<GameWindow *>( forRoot )->winIsHidden() )
		return;
	for( size_t i = 0; i < s_decos.size(); ++i )
	{
		const Deco &d = s_decos[ i ];
		if( d.root != forRoot )
			continue;
		if( d.kind == DECO_SCRIM ) { UIStyle::Draw::scrim(); continue; }
		const Int x = screenX( d.ax, d.x, d.w ), y = screenY( d.ay, d.y, d.h );
		const Int w = UIStyle::u( d.w ), h = UIStyle::u( d.h );
		switch( d.kind )
		{
			case DECO_PANEL: UIStyle::Draw::panel( x, y, w, h ); break;
			case DECO_RULE: UIStyle::Draw::twoToneRule( x, y, w ); break;
			case DECO_RULER: UIStyle::Draw::ruler( x, y, w ); break;
			case DECO_BAND: UIStyle::Draw::band( x, y, w, h ); break;
			case DECO_DIALOG: UIStyle::Draw::dialog( x, y, w, h ); break;
			case DECO_SCRIM: break;
		}
	}
}

} // namespace UILayout
