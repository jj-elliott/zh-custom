// zh-pipeline @feature 2026-10-22 Menu UI system: style tokens and shared control renderers. See GameClient/UIStyle.h.
#include "PreRTS.h"

#include <math.h>
#include "GameClient/UIStyle.h"
#include "GameClient/Display.h"

namespace UIStyle
{

// ---- switches ---------------------------------------------------------------------------------------------------------

static Bool s_enabled = TRUE;
static Bool s_testScreen = FALSE;

Bool isEnabled() { return s_enabled; }
void setEnabled( Bool on ) { s_enabled = on; }
Bool isTestScreen() { return s_testScreen; }
void setTestScreen( Bool on ) { s_testScreen = on; }

// ---- scale ------------------------------------------------------------------------------------------------------------

Real unit()
{
	if( TheDisplay == nullptr || TheDisplay->getHeight() <= 0 )
		return 1.0f;
	return (Real)TheDisplay->getHeight() / 1080.0f;
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

// ---- renderers --------------------------------------------------------------------------------------------------------

namespace Draw
{

void brackets( Int x, Int y, Int w, Int h, Real radiusUnits )
{
	const Palette &p = palette();
	const Color c = withAlpha( p.signalBlue, 0.7f );
	const Int len = u( Metric::Bracket );
	const Real stroke = uf( Metric::BracketStroke );
	const Int r = u( radiusUnits );
	// each bracket: a short arc-free L that starts past the corner radius, so it reads as a reticle, not a second border
	// top-left
	TheDisplay->drawLine( x + r, y, x + r + len, y, stroke, c );
	TheDisplay->drawLine( x, y + r, x, y + r + len, stroke, c );
	// top-right
	TheDisplay->drawLine( x + w - r - len, y, x + w - r, y, stroke, c );
	TheDisplay->drawLine( x + w, y + r, x + w, y + r + len, stroke, c );
	// bottom-left
	TheDisplay->drawLine( x + r, y + h, x + r + len, y + h, stroke, c );
	TheDisplay->drawLine( x, y + h - r - len, x, y + h - r, stroke, c );
	// bottom-right
	TheDisplay->drawLine( x + w - r - len, y + h, x + w - r, y + h, stroke, c );
	TheDisplay->drawLine( x + w, y + h - r - len, x + w, y + h - r, stroke, c );
}

void grid( Int x, Int y, Int w, Int h )
{
	const Palette &p = palette();
	const Color c = withAlpha( p.lineHair, 0.07f );
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
		const Int len = u( Metric::Bracket );
		const Real stroke = uf( Metric::BracketStroke );
		const Int ri = (Int)r;
		TheDisplay->drawLine( x + ri, y, x + ri + len, y, stroke, c );
		TheDisplay->drawLine( x, y + ri, x, y + ri + len, stroke, c );
		TheDisplay->drawLine( x + w - ri - len, y, x + w - ri, y, stroke, c );
		TheDisplay->drawLine( x + w, y + ri, x + w, y + ri + len, stroke, c );
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
