// zh-pipeline @feature 2026-10-22 Menu UI system: gadget renderers. See GameClient/UIGadgets.h.
#include "PreRTS.h"

#include "GameClient/UIGadgets.h"
#include "GameClient/UIStyle.h"
#include "GameClient/UIFonts.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowGlobal.h"
#include "GameClient/WinInstanceData.h"

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
}

void drawPushButton( GameWindow *window, WinInstanceData *instData, UILayout::Style style )
{
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );
	const ControlState state = stateOf( window, instData );

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
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );
	const UnicodeString text = instData->getText();
	if( text.isEmpty() )
		return;
	const Bool enabled = BitIsSet( window->winGetStatus(), WIN_STATUS_ENABLED );
	Text::draw( origin.x + size.x / 2, origin.y, text, UIFonts::ROLE_LABEL,
							enabled ? palette().textSecondary : palette().textDisabled, 1, size.y );
}

} // namespace UIGadgets
