// zh-pipeline @feature 2026-10-22 Menu UI system: style tokens and shared control renderers. See GameClient/UIStyle.h.
#include "PreRTS.h"

#include <math.h>
#include "GameClient/UIStyle.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/UIFonts.h"
#include <vector>
#include <ctype.h>
#include <wctype.h>

namespace UIStyle
{

// ---- switches ---------------------------------------------------------------------------------------------------------

static Bool s_enabled = TRUE;
static Bool s_testScreen = FALSE;

Bool isEnabled() { return s_enabled; }
void setEnabled( Bool on ) { s_enabled = on; }
Bool isTestScreen() { return s_testScreen; }
void setTestScreen( Bool on ) { s_testScreen = on; }
static AsciiString s_devScreen;
const AsciiString &devScreen() { return s_devScreen; }
void setDevScreen( const AsciiString &name ) { s_devScreen = name; }

// ---- scale ------------------------------------------------------------------------------------------------------------

void frame( Int &x, Int &y, Int &w, Int &h )
{
	const Int W = TheDisplay ? TheDisplay->getWidth() : 1920;
	const Int H = TheDisplay ? TheDisplay->getHeight() : 1080;
	w = W; h = H;
	if( W * 9 > H * 16 ) w = H * 16 / 9;    // wider than 16:9: letterbox left and right
	else h = W * 9 / 16;                     // 16:10 and squarer: bands top and bottom
	x = ( W - w ) / 2;
	y = ( H - h ) / 2;
}

Real unit()
{
	Int x, y, w, h;
	frame( x, y, w, h );
	if( h <= 0 )
		return 1.0f;
	return (Real)h / 1080.0f;
}

Real uf( Real units ) { return units * unit(); }

Int u( Real units )
{
	const Real px = uf( units );
	Int result = (Int)floorf( px + 0.5f );
	if( units > 0.0f && result < 1 )
		result = 1;
	return result;
}

// ---- palette ----------------------------------------------------------------------------------------------------------

static inline Color rgba( UnsignedByte r, UnsignedByte g, UnsignedByte b, Real a01 = 1.0f )
{
	return GameMakeColor( r, g, b, (UnsignedByte)floorf( a01 * 255.0f + 0.5f ) );
}

const Palette &palette()
{
	static Palette p;
	static Bool init = FALSE;
	if( !init )
	{
		p.ground          = rgba( 0x07, 0x0B, 0x14 );
		p.panelFill       = rgba( 0x0B, 0x14, 0x24, 0.76f );
		p.panelFillRaised = rgba( 0x12, 0x20, 0x36, 0.88f );
		p.lineHair        = rgba( 0x7F, 0xA6, 0xD9, 0.18f );
		p.lineStrong      = rgba( 0x7F, 0xA6, 0xD9, 0.40f );
		p.signalBlue      = rgba( 0x4F, 0xA3, 0xFF );
		p.signalCyan      = rgba( 0x00, 0xE3, 0xFF );
		p.signalBlueDeep  = rgba( 0x1E, 0x66, 0xC9 );
		p.amber           = rgba( 0xF2, 0xB5, 0x44 );
		p.amberHover      = rgba( 0xFF, 0xC9, 0x5B );
		p.amberInk        = rgba( 0x1A, 0x13, 0x00 );
		p.textPrimary     = rgba( 0xE8, 0xEE, 0xF6 );
		p.textSecondary   = rgba( 0x9F, 0xB0, 0xC8 );
		p.textDisabled    = rgba( 0x5E, 0x6C, 0x80 );
		p.danger          = rgba( 0xFF, 0x6B, 0x6B );
		init = TRUE;
	}
	return p;
}

// ---- colour helpers ---------------------------------------------------------------------------------------------------

static inline Real srgbToLinear( Real c )
{
	return ( c <= 0.04045f ) ? c / 12.92f : powf( ( c + 0.055f ) / 1.055f, 2.4f );
}

static inline Real linearToSrgb( Real c )
{
	if( c <= 0.0f ) return 0.0f;
	if( c >= 1.0f ) return 1.0f;
	return ( c <= 0.0031308f ) ? c * 12.92f : 1.055f * powf( c, 1.0f / 2.4f ) - 0.055f;
}

Color mix( Color a, Color b, Real t )
{
	if( t <= 0.0f ) return a;
	if( t >= 1.0f ) return b;
	UnsignedByte ar, ag, ab, aa, br, bg, bb, ba;
	GameGetColorComponents( a, &ar, &ag, &ab, &aa );
	GameGetColorComponents( b, &br, &bg, &bb, &ba );
	const Real r = linearToSrgb( srgbToLinear( ar / 255.0f ) * ( 1.0f - t ) + srgbToLinear( br / 255.0f ) * t );
	const Real g = linearToSrgb( srgbToLinear( ag / 255.0f ) * ( 1.0f - t ) + srgbToLinear( bg / 255.0f ) * t );
	const Real bl = linearToSrgb( srgbToLinear( ab / 255.0f ) * ( 1.0f - t ) + srgbToLinear( bb / 255.0f ) * t );
	const Real al = ( aa / 255.0f ) * ( 1.0f - t ) + ( ba / 255.0f ) * t;
	return rgba( (UnsignedByte)floorf( r * 255.0f + 0.5f ), (UnsignedByte)floorf( g * 255.0f + 0.5f ),
							 (UnsignedByte)floorf( bl * 255.0f + 0.5f ), al );
}

Color withAlpha( Color c, Real alpha01 )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( c, &r, &g, &b, &a );
	return rgba( r, g, b, alpha01 );
}

// composite c over the panel fill at the given opacity (for hover tints)
static Color tint( Color base, Color over, Real amount )
{
	return mix( base, withAlpha( over, 1.0f ), amount );
}


// ---- text -------------------------------------------------------------------------------------------------------------
// Small caps are built from two fonts: capitals, digits and punctuation in the role's font, lowercase runs uppercased in
// the same face at 78% of the size, baseline-aligned. The engine's DisplayString carries one font, so a string becomes a
// run of DisplayStrings taken from a pool in draw order; the pool cursor resets each frame.

namespace Text
{

namespace
{
	const Real SMALL_CAPS_SCALE = 0.78f;
	const Real ASCENT_RATIO = 0.80f;   // Barlow's ascent over its em: aligns the small run's baseline to the full run's

	std::vector<DisplayString *> s_pool;
	size_t s_cursor = 0;

	// Pool entries are handed out in draw order and the cursor is reset by beginFrame() each frame (InGameUI::postDraw).
	// A hard cap guards against a missing reset: past it the pool wraps instead of growing, which only risks a wrong
	// string on screen, never the unbounded allocation that once exhausted the renderer's memory.
	const size_t POOL_CAP = 2048;

	DisplayString *take()
	{
		if( TheDisplayStringManager == nullptr )
			return nullptr;
		if( s_cursor >= POOL_CAP )
			s_cursor = 0;
		if( s_cursor >= s_pool.size() )
			s_pool.push_back( TheDisplayStringManager->newDisplayString() );
		return s_pool[ s_cursor++ ];
	}

	struct Run
	{
		UnicodeString text;
		Bool lowerRun;
	};

	Bool hasLower( const UnicodeString &t )
	{
		for( Int i = 0; i < t.getLength(); ++i )
			if( iswlower( t.getCharAt( i ) ) )
				return TRUE;
		return FALSE;
	}

	// the game's own strings are often set in capitals ("SOLO PLAY"); small caps need the mixed-case form, so an all-caps
	// string is title-cased first: first letter of each word kept, the rest lowered
	UnicodeString titleCase( const UnicodeString &t )
	{
		UnicodeString out;
		Bool wordStart = TRUE;
		for( Int i = 0; i < t.getLength(); ++i )
		{
			const WideChar c = t.getCharAt( i );
			if( iswalpha( c ) )
			{
				out.concat( wordStart ? c : (WideChar)towlower( c ) );
				wordStart = FALSE;
			}
			else
			{
				out.concat( c );
				wordStart = TRUE;
			}
		}
		return out;
	}

	// split into runs; for small caps, lowercase letters become uppercase in the small font
	// this renderer sets one line: a string table entry that breaks itself ("Units \n Created", written for 74 px wide
	// legacy windows) is flattened to single spaces first
	UnicodeString oneLine( const UnicodeString &src )
	{
		UnicodeString out;
		Bool pendingSpace = FALSE;
		for( Int i = 0; i < src.getLength(); ++i )
		{
			const WideChar c = src.getCharAt( i );
			if( c == L'\n' || c == L'\r' || c == L' ' || c == L'\t' )
			{
				pendingSpace = out.getLength() > 0;
				continue;
			}
			if( pendingSpace ) { out.concat( L' ' ); pendingSpace = FALSE; }
			out.concat( c );
		}
		return out;
	}

	void splitRuns( const UnicodeString &srcRaw, Bool smallCaps, Bool upper, std::vector<Run> &runs )
	{
		runs.clear();
		const UnicodeString src = oneLine( srcRaw );
		if( !smallCaps )
		{
			Run r; r.lowerRun = FALSE;
			if( upper )
				for( Int i = 0; i < src.getLength(); ++i )
					r.text.concat( (WideChar)towupper( src.getCharAt( i ) ) );
			else
				r.text = src;
			runs.push_back( r );
			return;
		}
		const UnicodeString t = hasLower( src ) ? src : titleCase( src );
		Run cur; cur.lowerRun = FALSE;
		Bool have = FALSE;
		for( Int i = 0; i < t.getLength(); ++i )
		{
			const WideChar c = t.getCharAt( i );
			const Bool lower = iswlower( c ) != 0;
			if( have && lower != cur.lowerRun )
			{
				runs.push_back( cur );
				cur.text.clear();
			}
			cur.lowerRun = lower;
			cur.text.concat( lower ? (WideChar)towupper( c ) : c );
			have = TRUE;
		}
		if( have )
			runs.push_back( cur );
	}

	// one measured run ready to draw
	struct Laid
	{
		DisplayString *ds;
		Int w, h;
		Bool lowerRun;
	};

	// The sentence renderer reports a string's width with the last glyph's overlap padding added (render2dsentence.cpp,
	// Get_Extra_Overlap), so butting two measured runs leaves a gap. Measure that padding per font from the public API:
	// width("H") * 2 - width("HH"). Cached per GameFont.
	struct OverlapEntry { GameFont *font; Int overlap; };
	std::vector<OverlapEntry> s_overlaps;

	Int joinOverlap( GameFont *font )
	{
		if( font == nullptr )
			return 0;
		for( size_t i = 0; i < s_overlaps.size(); ++i )
			if( s_overlaps[ i ].font == font )
				return s_overlaps[ i ].overlap;
		Int overlap = 0;
		if( TheDisplayStringManager )
		{
			DisplayString *a = TheDisplayStringManager->newDisplayString();
			DisplayString *b = TheDisplayStringManager->newDisplayString();
			if( a && b )
			{
				UnicodeString one, two;
				one.translate( AsciiString( "H" ) );
				two.translate( AsciiString( "HH" ) );
				a->setFont( font ); a->setText( one );
				b->setFont( font ); b->setText( two );
				Int w1 = 0, w2 = 0, h = 0;
				a->getSize( &w1, &h );
				b->getSize( &w2, &h );
				overlap = 2 * w1 - w2;
				if( overlap < 0 ) overlap = 0;
			}
			if( a ) TheDisplayStringManager->freeDisplayString( a );
			if( b ) TheDisplayStringManager->freeDisplayString( b );
		}
		OverlapEntry e; e.font = font; e.overlap = overlap;
		s_overlaps.push_back( e );
		return overlap;
	}

	Int layout( const UnicodeString &text, Int roleInt, std::vector<Laid> &out, Int &fullHeight, Int &smallHeight )
	{
		out.clear();
		fullHeight = smallHeight = 0;
		const UIFonts::Role role = (UIFonts::Role)roleInt;
		GameFont *big = UIFonts::font( role );
		GameFont *lowerFont = UIFonts::smallCaps( role ) ? UIFonts::font( role, SMALL_CAPS_SCALE ) : big;
		if( big == nullptr )
			return 0;
		fullHeight = big->height;
		smallHeight = lowerFont ? lowerFont->height : big->height;
		std::vector<Run> runs;
		splitRuns( text, UIFonts::smallCaps( role ), UIFonts::uppercase( role ), runs );
		Int total = 0;
		for( size_t i = 0; i < runs.size(); ++i )
		{
			DisplayString *ds = take();
			if( ds == nullptr )
				break;
			GameFont *f = runs[ i ].lowerRun ? lowerFont : big;
			ds->setFont( f );
			ds->setText( runs[ i ].text );
			Laid l; l.ds = ds; l.lowerRun = runs[ i ].lowerRun; l.w = 0; l.h = 0;
			ds->getSize( &l.w, &l.h );
			// every run but the last advances by its width minus the trailing overlap padding
			if( i + 1 < runs.size() )
				l.w -= joinOverlap( f );
			total += l.w;
			out.push_back( l );
		}
		return total;
	}
}

void beginFrame()
{
	s_cursor = 0;
}

Int lineHeight( Int role )
{
	GameFont *f = UIFonts::font( (UIFonts::Role)role );
	return f ? f->height : 0;
}

Int width( const UnicodeString &text, Int role )
{
	std::vector<Laid> laid;
	Int fh, sh;
	const size_t saved = s_cursor;
	const Int w = layout( text, role, laid, fh, sh );
	s_cursor = saved;   // measuring borrows pool entries without consuming them
	return w;
}

Int width( const char *ascii, Int role )
{
	UnicodeString us;
	us.translate( AsciiString( ascii ? ascii : "" ) );
	return width( us, role );
}

Int draw( Int x, Int y, const UnicodeString &text, Int role, Color color, Int align, Int boxHeight )
{
	std::vector<Laid> laid;
	Int fullH, smallH;
	const Int total = layout( text, role, laid, fullH, smallH );
	if( laid.empty() )
		return 0;
	Int cx = x;
	if( align == 1 ) cx = x - total / 2;
	else if( align == 2 ) cx = x - total;
	Int top = y;
	if( boxHeight > 0 )
		top = y + ( boxHeight - fullH ) / 2;
	const Int smallTop = top + (Int)( ( fullH - smallH ) * ASCENT_RATIO + 0.5f );
	for( size_t i = 0; i < laid.size(); ++i )
	{
		laid[ i ].ds->draw( cx, laid[ i ].lowerRun ? smallTop : top, color, 0 );
		cx += laid[ i ].w;
	}
	return total;
}

Int draw( Int x, Int y, const char *ascii, Int role, Color color, Int align, Int boxHeight )
{
	UnicodeString us;
	us.translate( AsciiString( ascii ? ascii : "" ) );
	return draw( x, y, us, role, color, align, boxHeight );
}

} // namespace Text

// ---- renderers --------------------------------------------------------------------------------------------------------

namespace Draw
{

// one corner bracket: a stub along each edge joined by the corner arc, so it hugs the rounded corner instead of sitting
// beside it. (cx, cy) is the arc centre, a0 the angle of the first stub's edge, the arc runs a quarter turn to a1.
static void cornerBracket( Real cx, Real cy, Real r, Real a0, Real a1, Real len, Real stroke, Color c )
{
	const Int arcSegs = 6;
	Coord2D pts[ arcSegs + 3 ];
	Int n = 0;
	// stub end before the arc: continue the edge beyond the arc start
	const Real sx = cx + r * cosf( a0 ), sy = cy + r * sinf( a0 );
	const Real tx = -sinf( a0 ), ty = cosf( a0 );   // tangent at a0, pointing in the direction of decreasing angle
	pts[ n ].x = sx + tx * len * ( ( a1 > a0 ) ? -1.0f : 1.0f ); pts[ n ].y = sy + ty * len * ( ( a1 > a0 ) ? -1.0f : 1.0f ); ++n;
	for( Int i = 0; i <= arcSegs; ++i )
	{
		const Real a = a0 + ( a1 - a0 ) * (Real)i / (Real)arcSegs;
		pts[ n ].x = cx + r * cosf( a ); pts[ n ].y = cy + r * sinf( a ); ++n;
	}
	const Real ex = cx + r * cosf( a1 ), ey = cy + r * sinf( a1 );
	const Real ux = -sinf( a1 ), uy = cosf( a1 );
	pts[ n ].x = ex + ux * len * ( ( a1 > a0 ) ? 1.0f : -1.0f ); pts[ n ].y = ey + uy * len * ( ( a1 > a0 ) ? 1.0f : -1.0f ); ++n;
	TheDisplay->drawPolyline( pts, n, stroke, c );
}

void brackets( Int x, Int y, Int w, Int h, Real radiusUnits )
{
	const Palette &p = palette();
	const Color c = withAlpha( p.signalBlue, 0.7f );
	const Real len = uf( Metric::Bracket );
	const Real stroke = uf( Metric::BracketStroke );
	const Real r = uf( radiusUnits );
	const Real kPi = 3.14159265f;
	// the stroke sits centred on the outline: offset the arc centres by nothing, the outline is at the rect edge
	cornerBracket( x + r,     y + r,     r, kPi,       1.5f * kPi, len, stroke, c );   // top-left: left edge -> arc -> top edge
	cornerBracket( x + w - r, y + r,     r, 1.5f * kPi, 2.0f * kPi, len, stroke, c );   // top-right
	cornerBracket( x + w - r, y + h - r, r, 0.0f,      0.5f * kPi, len, stroke, c );   // bottom-right
	cornerBracket( x + r,     y + h - r, r, 0.5f * kPi, kPi,       len, stroke, c );   // bottom-left
}

void grid( Int x, Int y, Int w, Int h, Real alpha )
{
	const Palette &p = palette();
	const Color c = withAlpha( p.lineHair, alpha );
	const Int step = u( Metric::GridStep );
	const Real hair = uf( Metric::Hairline );
	const Int inset = u( 2.0f );
	for( Int gx = x + step; gx < x + w - inset; gx += step )
		TheDisplay->drawLine( gx, y + inset, gx, y + h - inset, hair, c );
	for( Int gy = y + step; gy < y + h - inset; gy += step )
		TheDisplay->drawLine( x + inset, gy, x + w - inset, gy, hair, c );
}

void panel( Int x, Int y, Int w, Int h, Bool withGrid, Bool withBrackets )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusPanel );
	TheDisplay->drawRoundedRect( x, y, w, h, r, p.panelFill );
	if( withGrid )
		grid( x, y, w, h );
	TheDisplay->drawRoundedOutline( x, y, w, h, r, uf( Metric::Hairline ), p.lineHair );
	if( withBrackets )
		brackets( x, y, w, h, Metric::RadiusPanel );
}

void dialog( Int x, Int y, Int w, Int h )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusPanel );
	TheDisplay->drawRoundedRect( x, y, w, h, r, p.panelFillRaised );
	grid( x, y, w, h );
	TheDisplay->drawRoundedOutline( x, y, w, h, r, uf( Metric::Hairline ), p.lineStrong );
	brackets( x, y, w, h, Metric::RadiusPanel );
}

void scrim()
{
	TheDisplay->drawFillRect( 0, 0, (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight(), withAlpha( palette().ground, 0.4f ) );
}

void band( Int x, Int y, Int w, Int h )
{
	// the old MainMenuRuler frame as geometry: a translucent band with the blueprint grid showing through
	const Palette &p = palette();
	TheDisplay->drawFillRect( x, y, w, h, p.panelFill );
	grid( x, y, w, h, 0.22f );
	TheDisplay->drawOpenRect( x, y, w, h, uf( Metric::Hairline ), p.lineHair );
}

void twoToneRule( Int x, Int y, Int w )
{
	const Palette &p = palette();
	const Real hair = uf( Metric::Hairline );
	TheDisplay->drawLine( x, y, x + w, y, hair, p.signalBlue );
	TheDisplay->drawLine( x, y + u( 3.0f ), x + w, y + u( 3.0f ), hair, withAlpha( p.signalCyan, 0.6f ) );
}

void ruler( Int x, Int y, Int w )
{
	const Palette &p = palette();
	const Real hair = uf( Metric::Hairline );
	const Int minor = u( 8.0f ), major = u( 40.0f );
	const Int hMinor = u( 6.0f ), hMajor = u( 12.0f );
	const Color cMinor = withAlpha( p.lineStrong, 0.55f * 2.5f > 1.0f ? 1.0f : 0.55f * 2.5f );	// lineStrong is already 40%: lift to ~55% visible
	const Color cMajor = withAlpha( p.lineStrong, 0.8f * 2.5f > 1.0f ? 1.0f : 0.8f * 2.5f );
	Int i = 0;
	for( Int tx = x; tx <= x + w; tx += minor, ++i )
	{
		const Bool big = ( ( tx - x ) % major ) == 0;
		TheDisplay->drawLine( tx, y + hMajor - ( big ? hMajor : hMinor ), tx, y + hMajor, hair, big ? cMajor : cMinor );
	}
}

void focusRing( Int x, Int y, Int w, Int h, Real radiusUnits )
{
	const Int ring = u( Metric::FocusRing );
	TheDisplay->drawRoundedOutline( x - ring, y - ring, w + 2 * ring, h + 2 * ring, uf( radiusUnits ) + ring, (Real)ring, palette().signalBlue );
}

Color textColor( ControlState state, ButtonKind kind )
{
	const Palette &p = palette();
	if( state == STATE_DISABLED ) return p.textDisabled;
	if( kind == BUTTON_PRIMARY ) return p.amberInk;
	if( kind == BUTTON_DANGER ) return p.danger;
	if( state == STATE_HOVER ) return p.signalBlue;
	return p.textPrimary;
}

void button( Int x, Int y, Int w, Int h, ControlState state, ButtonKind kind, Bool focused )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusControl );
	const Real hair = uf( Metric::Hairline );
	const Int inset = u( 1.0f );
	if( kind == BUTTON_PRIMARY && state != STATE_DISABLED )
	{
		const Color top = ( state == STATE_HOVER ) ? p.amberHover : p.amber;
		const Color bottom = mix( top, rgba( 0xE1, 0xA6, 0x31 ), ( state == STATE_HOVER ) ? 0.6f : 1.0f );
		TheDisplay->drawRoundedRectVGradient( x, y, w, h, r, top, ( state == STATE_PRESSED ) ? top : bottom );
		TheDisplay->drawLine( x + (Int)r, y + inset, x + w - (Int)r, y + inset, hair, rgba( 255, 255, 255, 0.35f ) );
	}
	else
	{
		Color fill = p.panelFill;
		Color border = p.lineStrong;
		Color highlight = p.lineHair;
		if( state == STATE_HOVER )
		{
			fill = tint( p.panelFill, p.signalBlue, 0.14f );
			border = p.signalBlue;
			highlight = withAlpha( p.signalCyan, 0.7f );
		}
		else if( state == STATE_PRESSED )
		{
			fill = tint( p.panelFill, p.signalBlueDeep, 0.35f );
			border = p.signalBlue;
		}
		else if( state == STATE_DISABLED )
		{
			border = p.lineHair;
			highlight = 0;
		}
		if( kind == BUTTON_DANGER && state != STATE_DISABLED )
			border = withAlpha( p.danger, 0.5f );
		TheDisplay->drawRoundedRect( x, y, w, h, r, fill );
		TheDisplay->drawRoundedOutline( x, y, w, h, r, hair, border );
		if( highlight != 0 )
			TheDisplay->drawLine( x + (Int)r, y + inset, x + w - (Int)r, y + inset, hair, highlight );
	}
	if( focused )
		focusRing( x, y, w, h, Metric::RadiusControl );
}

void menuButton( Int x, Int y, Int w, Int h, ControlState state, Bool focused )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusMenuButton );
	const Real hair = uf( Metric::Hairline );
	const Int inset = u( 1.0f );
	Color top = rgba( 14, 26, 46, 0.82f ), bottom = rgba( 9, 16, 30, 0.82f );
	Color border = p.lineStrong;
	Color highlight = p.lineHair;
	if( state == STATE_HOVER )
	{
		top = tint( top, p.signalBlue, 0.22f ); bottom = tint( bottom, p.signalBlueDeep, 0.18f );
		border = p.signalBlue; highlight = withAlpha( p.signalCyan, 0.7f );
	}
	else if( state == STATE_PRESSED )
	{
		top = bottom = tint( bottom, p.signalBlueDeep, 0.4f );
		border = p.signalBlue;
	}
	else if( state == STATE_DISABLED )
	{
		border = p.lineHair; highlight = 0;
	}
	TheDisplay->drawRoundedRectVGradient( x, y, w, h, r, top, bottom );
	TheDisplay->drawRoundedOutline( x, y, w, h, r, hair, border );
	if( highlight != 0 )
		TheDisplay->drawLine( x + (Int)r, y + inset, x + w - (Int)r, y + inset, hair, highlight );
	if( state == STATE_HOVER )
	{
		// top corner brackets, as on the mockup
		const Color c = withAlpha( p.signalBlue, 0.9f );
		const Real len = uf( Metric::Bracket );
		const Real stroke = uf( Metric::BracketStroke );
		const Real kPi = 3.14159265f;
		cornerBracket( x + r,     y + r, r, kPi,       1.5f * kPi, len, stroke, c );
		cornerBracket( x + w - r, y + r, r, 1.5f * kPi, 2.0f * kPi, len, stroke, c );
	}
	if( focused )
		focusRing( x, y, w, h, Metric::RadiusMenuButton );
}

void control( Int x, Int y, Int w, Int h, ControlState state, Bool focused )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusControl );
	const Real hair = uf( Metric::Hairline );
	Color border = ( state == STATE_DISABLED ) ? p.lineHair : ( ( state == STATE_HOVER ) ? p.signalBlue : p.lineStrong );
	TheDisplay->drawRoundedRect( x, y, w, h, r, withAlpha( p.panelFill, 0.8f ) );
	TheDisplay->drawRoundedOutline( x, y, w, h, r, hair, border );
	if( state != STATE_DISABLED )
		TheDisplay->drawLine( x + (Int)r, y + u( 1.0f ), x + w - (Int)r, y + u( 1.0f ), hair, withAlpha( p.lineHair, 0.12f * 5.0f ) );
	if( focused )
		focusRing( x, y, w, h, Metric::RadiusControl );
}

void chevron( Int cx, Int cy, Color color )
{
	const Int half = u( 4.0f );
	Coord2D pts[ 3 ];
	pts[ 0 ].x = (Real)( cx - half ); pts[ 0 ].y = (Real)( cy - half / 2 );
	pts[ 1 ].x = (Real)cx;            pts[ 1 ].y = (Real)( cy + half / 2 );
	pts[ 2 ].x = (Real)( cx + half ); pts[ 2 ].y = (Real)( cy - half / 2 );
	TheDisplay->drawPolyline( pts, 3, uf( 1.5f ), color );
}

void listPopup( Int x, Int y, Int w, Int h )
{
	const Palette &p = palette();
	const Real r = uf( Metric::RadiusControl );
	TheDisplay->drawRoundedRect( x, y, w, h, r, withAlpha( p.panelFillRaised, 0.92f ) );
	TheDisplay->drawRoundedOutline( x, y, w, h, r, uf( Metric::Hairline ), p.lineHair );
}

void listRow( Int x, Int y, Int w, Int h, Bool selected, Bool hovered )
{
	const Palette &p = palette();
	if( selected )
	{
		TheDisplay->drawFillRect( x, y, w, h, withAlpha( p.signalBlue, 0.2f ) );
		TheDisplay->drawFillRect( x, y, u( 3.0f ), h, p.signalBlue );
	}
	else if( hovered )
	{
		TheDisplay->drawFillRect( x, y, w, h, rgba( 255, 255, 255, 0.05f ) );
	}
}

void caret( Int x, Int y, Int h )
{
	TheDisplay->drawLine( x, y, x, y + h, uf( 1.0f ), palette().signalBlue );
}

void checkbox( Int x, Int y, Bool checked, ControlState state, Bool focused )
{
	const Palette &p = palette();
	const Int size = u( Metric::CheckboxSize );
	const Real r = uf( Metric::RadiusSwatch );
	if( checked && state != STATE_DISABLED )
	{
		TheDisplay->drawRoundedRect( x, y, size, size, r, ( state == STATE_HOVER ) ? p.amberHover == 0 ? p.signalBlue : p.signalBlue : p.signalBlue );
		Coord2D pts[ 3 ];
		pts[ 0 ].x = x + uf( 5.0f );  pts[ 0 ].y = y + uf( 10.5f );
		pts[ 1 ].x = x + uf( 8.2f );  pts[ 1 ].y = y + uf( 13.7f );
		pts[ 2 ].x = x + uf( 15.0f ); pts[ 2 ].y = y + uf( 7.0f );
		TheDisplay->drawPolyline( pts, 3, uf( 2.2f ), p.ground );
	}
	else
	{
		TheDisplay->drawRoundedRect( x, y, size, size, r, withAlpha( p.panelFill, 0.8f ) );
		TheDisplay->drawRoundedOutline( x, y, size, size, r, uf( Metric::Hairline ),
																		( state == STATE_DISABLED ) ? p.lineHair : ( ( state == STATE_HOVER ) ? p.signalBlue : p.lineStrong ) );
		if( checked )
		{
			Coord2D pts[ 3 ];
			pts[ 0 ].x = x + uf( 5.0f );  pts[ 0 ].y = y + uf( 10.5f );
			pts[ 1 ].x = x + uf( 8.2f );  pts[ 1 ].y = y + uf( 13.7f );
			pts[ 2 ].x = x + uf( 15.0f ); pts[ 2 ].y = y + uf( 7.0f );
			TheDisplay->drawPolyline( pts, 3, uf( 2.2f ), p.textDisabled );
		}
	}
	if( focused )
		focusRing( x, y, size, size, Metric::RadiusSwatch );
}

void slider( Int x, Int y, Int w, Real value01, Int steps, ControlState state, Bool focused )
{
	// an instrument fader: thin track, filled part, tick marks, rectangular knob (doc §4)
	const Palette &p = palette();
	const Int trackH = u( Metric::SliderTrack );
	const Int knobW = u( Metric::SliderKnobW ), knobH = u( Metric::SliderKnobH );
	const Int cy = y + knobH / 2;
	if( value01 < 0.0f ) value01 = 0.0f;
	if( value01 > 1.0f ) value01 = 1.0f;
	const Int fillW = (Int)floorf( w * value01 + 0.5f );
	const Color trackColor = ( state == STATE_DISABLED ) ? p.lineHair : p.lineStrong;
	TheDisplay->drawRoundedRect( x, cy - trackH / 2, w, trackH, trackH * 0.5f, trackColor );
	if( fillW > 0 && state != STATE_DISABLED )
		TheDisplay->drawRoundedRect( x, cy - trackH / 2, fillW, trackH, trackH * 0.5f, p.signalBlue );
	if( steps > 1 )
	{
		const Int tickTop = cy + trackH / 2 + u( 6.0f );
		for( Int i = 0; i <= steps; ++i )
		{
			const Int tx = x + (Int)floorf( (Real)w * i / steps + 0.5f );
			TheDisplay->drawLine( tx, tickTop, tx, tickTop + u( 6.0f ), uf( Metric::Hairline ), trackColor );
		}
	}
	const Int kx = x + fillW - knobW / 2;
	const Int ky = cy - knobH / 2;
	const Color knob = ( state == STATE_DISABLED ) ? p.textDisabled : p.textPrimary;
	TheDisplay->drawRoundedRect( kx, ky, knobW, knobH, uf( 2.0f ), knob );
	TheDisplay->drawRoundedOutline( kx, ky, knobW, knobH, uf( 2.0f ), uf( Metric::Hairline ), rgba( 0, 0, 0, 0.35f ) );
	TheDisplay->drawLine( kx + knobW / 2, ky + u( 5.0f ), kx + knobW / 2, ky + knobH - u( 5.0f ), uf( Metric::Hairline ), rgba( 0, 0, 0, 0.35f ) );
	if( focused )
		focusRing( kx, ky, knobW, knobH, 2.0f );
}

void swatch( Int x, Int y, Int size, Color color )
{
	TheDisplay->drawRoundedRect( x, y, size, size, uf( Metric::RadiusSwatch ), color );
	TheDisplay->drawRoundedOutline( x, y, size, size, uf( Metric::RadiusSwatch ), uf( Metric::Hairline ), rgba( 255, 255, 255, 0.18f ) );
}

} // namespace Draw

} // namespace UIStyle
