// zh-pipeline @feature 2026-10-22 Menu UI system: gadget renderers. See GameClient/UIGadgets.h.
#include "PreRTS.h"

#include "GameClient/UIGadgets.h"
#include "GameClient/UIStyle.h"
#include "GameClient/UIFonts.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/WinInstanceData.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetStaticText.h"

using namespace UIStyle;

namespace UIGadgets
{

namespace
{
	ControlState stateOf( GameWindow *window, WinInstanceData *instData )
	{
		if( !BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED ) )
			return STATE_DISABLED;
		if( BitIsSet( instData->getState(), WIN_STATE_SELECTED ) )
			return STATE_PRESSED;
		if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
			return STATE_HOVER;
		return STATE_REST;
	}

	Bool hasFocus( GameWindow *window )
	{
		return TheWindowManager && TheWindowManager->winGetFocus() == window;
	}

	void rectOf( GameWindow *window, ICoord2D &origin, ICoord2D &size )
	{
		window->winGetScreenPosition( &origin.x, &origin.y );
		window->winGetSize( &size.x, &size.y );
	}
}

void drawPushButton( GameWindow *window, WinInstanceData *instData, UILayout::Style style )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	const ControlState state = stateOf( window, instData );
	const Palette &p = palette();

	// children of styled gadgets: the combo box arrow, the slider knob, the list box scroll buttons
	GameWindow *parent = window->winGetParent();
	const UnsignedInt parentStyle = parent ? parent->winGetStyle() : 0;
	if( BitIsSet( parentStyle, GWS_VERT_SLIDER ) )
	{
		// a list scroll thumb (also inside a combo box's popup list): a bar, clearly lighter than its track
		const Int bw = u( Metric::ScrollBarStroke );
		const Color bar = ( state == STATE_DISABLED ) ? p.lineHair
									 : ( state == STATE_PRESSED ) ? p.signalBlue
									 : ( state == STATE_HOVER ) ? p.textPrimary : p.textSecondary;
		TheDisplay->drawRoundedRect( origin.x + ( size.x - bw ) / 2, origin.y, bw, size.y, bw * 0.5f, bar );
		return;
	}
	if( BitIsSet( parentStyle, GWS_SCROLL_LISTBOX ) )
	{
		// scroll buttons: a chevron, up above the list's centre, down below
		ICoord2D po, ps; rectOf( parent, po, ps );
		const Bool up = origin.y + size.y / 2 < po.y + ps.y / 2;
		const Int half = u( 5.0f );
		const Int cx = origin.x + size.x / 2, cy = origin.y + size.y / 2;
		Coord2D pts[ 3 ];
		pts[ 0 ].x = (Real)( cx - half ); pts[ 0 ].y = (Real)( up ? cy + half / 2 : cy - half / 2 );
		pts[ 1 ].x = (Real)cx;            pts[ 1 ].y = (Real)( up ? cy - half / 2 : cy + half / 2 );
		pts[ 2 ].x = (Real)( cx + half ); pts[ 2 ].y = pts[ 0 ].y;
		TheDisplay->drawPolyline( pts, 3, uf( 2.0f ), ( state == STATE_DISABLED ) ? p.textDisabled
														: ( state == STATE_REST ) ? p.textSecondary : p.textPrimary );
		return;
	}
	if( style == UILayout::STYLE_DROPDOWN )
		return;   // the combo's chrome draws the chevron at the box's right edge, whatever this button's rectangle is
	if( style == UILayout::STYLE_SLIDER )
	{
		// the knob: centred on the thumb window, fader proportions regardless of the thumb's own size
		const Int kw = u( Metric::SliderKnobW ), kh = u( Metric::SliderKnobH );
		const Int kx = origin.x + ( size.x - kw ) / 2, ky = origin.y + ( size.y - kh ) / 2;
		const Color knob = ( state == STATE_DISABLED ) ? p.textDisabled : p.textPrimary;
		TheDisplay->drawRoundedRect( kx, ky, kw, kh, uf( 2.0f ), knob );
		TheDisplay->drawRoundedOutline( kx, ky, kw, kh, uf( 2.0f ), uf( Metric::Hairline ), GameMakeColor( 0, 0, 0, 90 ) );
		TheDisplay->drawLine( kx + kw / 2, ky + u( 5.0f ), kx + kw / 2, ky + kh - u( 5.0f ), uf( Metric::Hairline ), GameMakeColor( 0, 0, 0, 90 ) );
		return;
	}

	ButtonKind kind = BUTTON_NORMAL;
	if( style == UILayout::STYLE_PRIMARY ) kind = BUTTON_PRIMARY;
	else if( style == UILayout::STYLE_DANGER ) kind = BUTTON_DANGER;

	if( style == UILayout::STYLE_MENU_BUTTON )
		Draw::menuButton( origin.x, origin.y, size.x, size.y, state );
	else
		Draw::button( origin.x, origin.y, size.x, size.y, state, kind );

	const UnicodeString text = instData->getText();
	if( text.isEmpty() )
		return;
	Int role = UIFonts::ROLE_LABEL;
	if( style == UILayout::STYLE_MENU_BUTTON ) role = UIFonts::ROLE_MENU;
	else if( size.y < u( 36.0f ) ) role = UIFonts::ROLE_META;
	Text::draw( origin.x + size.x / 2, origin.y, text, role, Draw::textColor( state, kind ), 1, size.y );
}

void drawStaticText( GameWindow *window, WinInstanceData *instData, UILayout::Style style )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	UnicodeString text = GadgetStaticTextGetText( window );   // static text gadgets keep their string in their own data
	if( text.isEmpty() )
		text = instData->getText();
	if( text.isEmpty() )
		return;
	const Bool enabled = BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED );
	const Palette &p = palette();
	Int role = UIFonts::ROLE_LABEL;
	Int align = 0;
	Color color = enabled ? p.textPrimary : p.textDisabled;
	switch( style )
	{
		case UILayout::STYLE_LABEL:   role = UIFonts::ROLE_LABEL; align = 1; color = enabled ? p.textSecondary : p.textDisabled; break;
		case UILayout::STYLE_CAPTION: role = UIFonts::ROLE_LABEL; break;
		case UILayout::STYLE_META:    role = UIFonts::ROLE_META; color = enabled ? p.textSecondary : p.textDisabled; break;
		case UILayout::STYLE_SECTION: role = UIFonts::ROLE_SECTION; break;
		case UILayout::STYLE_TITLE:   role = UIFonts::ROLE_TITLE; break;
		default: break;
	}
	Text::draw( ( align == 1 ) ? origin.x + size.x / 2 : origin.x, origin.y, text, role, color, align, size.y );
}

void drawCheckBox( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	const ControlState state = stateOf( window, instData );
	const Bool checked = BitIsSet( instData->getState(), WIN_STATE_SELECTED );
	const Int box = u( Metric::CheckboxSize );
	const Int by = origin.y + ( size.y - box ) / 2;
	// the window's SELECTED state is the checked state, not a press; show hover on the box only
	const ControlState boxState = ( state == STATE_DISABLED ) ? STATE_DISABLED : ( ( state == STATE_HOVER ) ? STATE_HOVER : STATE_REST );
	Draw::checkbox( origin.x, by, checked, boxState, hasFocus( window ) );
	const UnicodeString text = instData->getText();
	if( text.isEmpty() )
		return;
	const Palette &p = palette();
	Text::draw( origin.x + box + u( 12.0f ), origin.y, text, UIFonts::ROLE_LABEL,
							( state == STATE_DISABLED ) ? p.textDisabled : p.textPrimary, 0, size.y );
}

void drawHorizontalSlider( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	const ControlState state = stateOf( window, instData );
	Int minV = 0, maxV = 1;
	GadgetSliderGetMinMax( window, &minV, &maxV );
	const Int pos = GadgetSliderGetPosition( window );
	const Real value01 = ( maxV > minV ) ? (Real)( pos - minV ) / (Real)( maxV - minV ) : 0.0f;
	Int steps = maxV - minV;
	if( steps > 20 ) steps = 10;
	if( steps < 2 ) steps = 2;
	// track and ticks only: the thumb child draws the knob (drawPushButton, STYLE_SLIDER)
	const Palette &p = palette();
	const Int trackH = u( Metric::SliderTrack );
	const Int cy = origin.y + size.y / 2;
	const Int knobHalf = u( Metric::SliderKnobW ) / 2;
	const Int x = origin.x + knobHalf, w = size.x - 2 * knobHalf;
	const Color trackColor = ( state == STATE_DISABLED ) ? p.lineHair : p.lineStrong;
	TheDisplay->drawRoundedRect( x, cy - trackH / 2, w, trackH, trackH * 0.5f, trackColor );
	// the fill ends under the knob: take the thumb window's centre when there is one, so the two never disagree
	Int fillW = (Int)( w * value01 + 0.5f );
	if( GameWindow *thumb = window->winGetChild() )
	{
		ICoord2D to, ts;
		rectOf( thumb, to, ts );
		fillW = to.x + ts.x / 2 - x;
		if( fillW < 0 ) fillW = 0;
		if( fillW > w ) fillW = w;
	}
	if( fillW > 0 && state != STATE_DISABLED )
		TheDisplay->drawRoundedRect( x, cy - trackH / 2, fillW, trackH, trackH * 0.5f, p.signalBlue );
	const Int tickTop = cy + trackH / 2 + u( 6.0f );
	for( Int i = 0; i <= steps; ++i )
	{
		const Int tx = x + (Int)( (Real)w * i / steps + 0.5f );
		TheDisplay->drawLine( tx, tickTop, tx, tickTop + u( 6.0f ), uf( Metric::Hairline ), trackColor );
	}
}

void drawVerticalSlider( GameWindow *window, WinInstanceData *instData )
{
	// list box scrollbar track: a dim channel; the thumb child draws the bar
	ICoord2D origin, size;
	rectOf( window, origin, size );
	const Int bw = u( Metric::ScrollBarStroke );
	TheDisplay->drawRoundedRect( origin.x + ( size.x - bw ) / 2, origin.y, bw, size.y, bw * 0.5f, withAlpha( palette().lineHair, 0.6f ) );
}

void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	Draw::panel( origin.x, origin.y, size.x, size.y );
}

void drawDialog( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	Draw::dialog( origin.x, origin.y, size.x, size.y );
}

void drawEntryChrome( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	// an entry inside a combo box is part of the combo's box: no chrome of its own
	GameWindow *parent = window->winGetParent();
	if( parent && UILayout::styleOf( parent ) == UILayout::STYLE_DROPDOWN )
		return;
	Draw::control( origin.x, origin.y, size.x, size.y, stateOf( window, instData ), hasFocus( window ) );
}

void drawComboChrome( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	const ControlState state = stateOf( window, instData );
	Draw::control( origin.x, origin.y, size.x, size.y, state, FALSE );
	Draw::chevron( origin.x + size.x - u( 16.0f ), origin.y + size.y / 2,
								 ( state == STATE_DISABLED ) ? palette().textDisabled : palette().textSecondary );
}

void drawRadioButton( GameWindow *window, WinInstanceData *instData )
{
	// one segment of a segmented control: the selected option reads as pressed
	ICoord2D origin, size;
	rectOf( window, origin, size );
	ControlState state = stateOf( window, instData );
	const Bool selected = BitIsSet( instData->getState(), WIN_STATE_SELECTED );
	if( state != STATE_DISABLED )
		state = selected ? STATE_PRESSED : ( ( state == STATE_HOVER ) ? STATE_HOVER : STATE_REST );
	Draw::button( origin.x, origin.y, size.x, size.y, state, BUTTON_NORMAL, FALSE );
	const UnicodeString text = instData->getText();
	if( text.isEmpty() )
		return;
	const Palette &p = palette();
	const Color c = ( state == STATE_DISABLED ) ? p.textDisabled : ( selected ? p.textPrimary : p.textSecondary );
	Text::draw( origin.x + size.x / 2, origin.y, text, UIFonts::ROLE_LABEL, c, 1, size.y );
}

void drawListChrome( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D origin, size;
	rectOf( window, origin, size );
	GameWindow *parent = window->winGetParent();
	if( parent && UILayout::styleOf( parent ) == UILayout::STYLE_DROPDOWN )
		Draw::listPopup( origin.x, origin.y, size.x, size.y );      // the open dropdown
	else
		Draw::control( origin.x, origin.y, size.x, size.y, STATE_REST, FALSE );
}

} // namespace UIGadgets
