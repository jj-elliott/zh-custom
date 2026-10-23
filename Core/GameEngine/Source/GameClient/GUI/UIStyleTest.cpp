// zh-pipeline @feature 2026-10-22 Menu UI system: the component sheet as a test screen. See GameClient/UIStyleTest.h.
#include "PreRTS.h"

#include <vector>
#include "GameClient/UIStyleTest.h"
#include "GameClient/UIStyle.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"

using namespace UIStyle;

namespace
{

// A small pool of display strings reused in draw order every frame; the sheet is static so slot N is always the same label.
std::vector<DisplayString *> s_strings;
Int s_next = 0;

enum TextRole
{
	TEXT_TITLE,
	TEXT_SECTION,
	TEXT_LABEL,
	TEXT_BODY,
	TEXT_META,
	TEXT_MENU
};

GameFont *fontFor( TextRole role )
{
	// Step 3 brings the real typeface and small caps; until then the roles map onto the stock faces at the right size.
	switch( role )
	{
		case TEXT_TITLE:   return TheFontLibrary->getFont( "Arial", u( 34.0f ), TRUE );
		case TEXT_SECTION: return TheFontLibrary->getFont( "Arial", u( 19.0f ), TRUE );
		case TEXT_MENU:    return TheFontLibrary->getFont( "Arial", u( 22.0f ), TRUE );
		case TEXT_LABEL:   return TheFontLibrary->getFont( "Arial", u( 15.0f ), FALSE );
		case TEXT_META:    return TheFontLibrary->getFont( "Arial", u( 12.0f ), FALSE );
		case TEXT_BODY:
		default:           return TheFontLibrary->getFont( "Arial", u( 15.0f ), FALSE );
	}
}

// draws text; align: 0 left, 1 centre, 2 right. Returns the text width.
Int text( Int x, Int y, const char *ascii, TextRole role, Color color, Int align = 0, Int centreInHeight = 0 )
{
	if( TheDisplayStringManager == nullptr || TheFontLibrary == nullptr )
		return 0;
	if( s_next >= (Int)s_strings.size() )
		s_strings.push_back( TheDisplayStringManager->newDisplayString() );
	DisplayString *ds = s_strings[ s_next++ ];
	if( ds == nullptr )
		return 0;
	UnicodeString us;
	us.translate( AsciiString( ascii ) );
	ds->setFont( fontFor( role ) );
	ds->setText( us );
	Int w = 0, h = 0;
	ds->getSize( &w, &h );
	Int dx = x;
	if( align == 1 ) dx = x - w / 2;
	else if( align == 2 ) dx = x - w;
	Int dy = y;
	if( centreInHeight > 0 ) dy = y + ( centreInHeight - h ) / 2;
	ds->draw( dx, dy, color, 0 );
	return w;
}

struct SwatchDef { const char *name; const char *value; Color color; };

} // namespace

void UIStyleTest::shutdown()
{
	if( TheDisplayStringManager )
		for( size_t i = 0; i < s_strings.size(); ++i )
			TheDisplayStringManager->freeDisplayString( s_strings[ i ] );
	s_strings.clear();
	s_next = 0;
}

void UIStyleTest::draw()
{
	if( TheDisplay == nullptr )
		return;
	s_next = 0;
	const Palette &p = palette();
	const Int W = TheDisplay->getWidth();
	const Int H = TheDisplay->getHeight();

	TheDisplay->beginBatch();
	TheDisplay->drawFillRect( 0, 0, W, H, p.ground );

	const Int margin = u( 64.0f );
	Int x = margin;
	Int y = u( 40.0f );

	// ---- title -------------------------------------------------------------------------------------------------------
	text( x, y, "UI style test", TEXT_TITLE, p.textPrimary );
	Draw::ruler( x, y + u( 50.0f ), u( 560.0f ) );
	{
		char buf[ 128 ];
		snprintf( buf, sizeof( buf ), "%d x %d, unit %.2f px", W, H, unit() );
		text( W - margin, y + u( 12.0f ), buf, TEXT_META, p.textSecondary, 2 );
	}
	y += u( 84.0f );

	// ---- colour ------------------------------------------------------------------------------------------------------
	text( x, y, "Color", TEXT_SECTION, p.textPrimary );
	y += u( 36.0f );
	{
		const SwatchDef sw[] = {
			{ "ink.ground", "#070B14", p.ground },
			{ "panel.fill", "#0B1424 76%", p.panelFill },
			{ "panel.fill.raised", "#122036 88%", p.panelFillRaised },
			{ "line.hair", "#7FA6D9 18%", p.lineHair },
			{ "line.strong", "#7FA6D9 40%", p.lineStrong },
			{ "signal.blue", "#4FA3FF", p.signalBlue },
			{ "signal.cyan", "#00E3FF", p.signalCyan },
			{ "signal.blue.deep", "#1E66C9", p.signalBlueDeep },
			{ "attention.amber", "#F2B544", p.amber },
			{ "text.primary", "#E8EEF6", p.textPrimary },
			{ "text.secondary", "#9FB0C8", p.textSecondary },
			{ "text.disabled", "#5E6C80", p.textDisabled },
		};
		const Int n = sizeof( sw ) / sizeof( sw[ 0 ] );
		const Int cw = u( 132.0f ), ch = u( 48.0f ), gap = u( 14.0f );
		for( Int i = 0; i < n; ++i )
		{
			const Int sx = x + i * ( cw + gap );
			TheDisplay->drawRoundedRect( sx, y, cw, ch, uf( Metric::RadiusControl ), sw[ i ].color );
			TheDisplay->drawRoundedOutline( sx, y, cw, ch, uf( Metric::RadiusControl ), uf( Metric::Hairline ), GameMakeColor( 255, 255, 255, 31 ) );
			text( sx, y + ch + u( 8.0f ), sw[ i ].name, TEXT_META, p.textPrimary );
			text( sx, y + ch + u( 24.0f ), sw[ i ].value, TEXT_META, p.textSecondary );
		}
	}
	y += u( 110.0f );

	// ---- buttons -----------------------------------------------------------------------------------------------------
	text( x, y, "Buttons", TEXT_SECTION, p.textPrimary );
	y += u( 36.0f );
	{
		const Int bw = u( 150.0f ), bh = u( Metric::ControlHeight ), gap = u( 16.0f );
		struct B { const char *label; ControlState state; ButtonKind kind; Bool focused; const char *cap; };
		const B defs[] = {
			{ "Select Map", STATE_REST, BUTTON_NORMAL, FALSE, "rest" },
			{ "Select Map", STATE_HOVER, BUTTON_NORMAL, FALSE, "hover" },
			{ "Select Map", STATE_PRESSED, BUTTON_NORMAL, FALSE, "pressed" },
			{ "Select Map", STATE_REST, BUTTON_NORMAL, TRUE, "focus" },
			{ "Select Map", STATE_DISABLED, BUTTON_NORMAL, FALSE, "disabled" },
			{ "Start", STATE_REST, BUTTON_PRIMARY, FALSE, "primary" },
			{ "Start", STATE_HOVER, BUTTON_PRIMARY, FALSE, "primary hover" },
			{ "Disconnect", STATE_REST, BUTTON_DANGER, FALSE, "destructive" },
		};
		const Int n = sizeof( defs ) / sizeof( defs[ 0 ] );
		for( Int i = 0; i < n; ++i )
		{
			const Int bx = x + i * ( bw + gap );
			Draw::button( bx, y, bw, bh, defs[ i ].state, defs[ i ].kind, defs[ i ].focused );
			text( bx + bw / 2, y, defs[ i ].label, TEXT_LABEL, Draw::textColor( defs[ i ].state, defs[ i ].kind ), 1, bh );
			text( bx, y + bh + u( 8.0f ), defs[ i ].cap, TEXT_META, p.textSecondary );
		}
		// menu buttons
		const Int mx = x + n * ( bw + gap ) + u( 24.0f );
		const Int mw = u( Metric::MenuButtonWidth ), mh = u( Metric::MenuButtonHeight );
		if( mx + mw <= W - margin )
		{
			Draw::menuButton( mx, y - u( 4.0f ), mw, mh, STATE_REST );
			text( mx + mw / 2, y - u( 4.0f ), "SOLO PLAY", TEXT_MENU, p.textPrimary, 1, mh );
			Draw::menuButton( mx, y + mh + u( 10.0f ), mw, mh, STATE_HOVER );
			text( mx + mw / 2, y + mh + u( 10.0f ), "MULTIPLAYER", TEXT_MENU, p.textPrimary, 1, mh );
			text( mx, y + 2 * mh + u( 20.0f ), "menu button: rest, hover", TEXT_META, p.textSecondary );
		}
	}
	y += u( 150.0f );

	// ---- dropdown, entry, checkbox, slider ----------------------------------------------------------------------------
	text( x, y, "Dropdown, entry, checkbox, slider", TEXT_SECTION, p.textPrimary );
	y += u( 62.0f );   // room for the control labels above the row
	{
		const Int cw = u( 260.0f ), ch = u( Metric::ControlHeight );
		// dropdown with open list
		Draw::control( x, y, cw, ch, STATE_REST );
		text( x + u( 16.0f ), y, "USA Air Force General", TEXT_LABEL, p.textPrimary, 0, ch );
		Draw::chevron( x + cw - u( 18.0f ), y + ch / 2, p.textSecondary );
		{
			const Int rows = 5, rh = u( Metric::RowHeight ), pad = u( 6.0f );
			const Int ly = y + ch + u( 6.0f );
			Draw::listPopup( x, ly, cw, rows * rh + 2 * pad );
			const char *items[ rows ] = { "USA", "USA Air Force General", "USA Laser General", "USA Superweapon General", "China" };
			for( Int i = 0; i < rows; ++i )
			{
				const Int ry = ly + pad + i * rh;
				Draw::listRow( x + u( 1.0f ), ry, cw - u( 2.0f ), rh, i == 1, i == 2 );
				text( x + u( 16.0f ), ry, items[ i ], TEXT_BODY, p.textPrimary, 0, rh );
			}
			text( x, ly + rows * rh + 2 * pad + u( 8.0f ), "dropdown, open list, selected and hovered rows", TEXT_META, p.textSecondary );
		}
		// entry
		const Int ex = x + cw + u( 40.0f );
		text( ex, y - u( 22.0f ), "Player", TEXT_META, p.textSecondary );
		Draw::control( ex, y, cw, ch, STATE_REST, TRUE );
		{
			const Int tw = text( ex + u( 16.0f ), y, "Jack", TEXT_LABEL, p.textPrimary, 0, ch );
			Draw::caret( ex + u( 16.0f ) + tw + u( 2.0f ), y + u( 11.0f ), ch - u( 22.0f ) );
		}
		text( ex, y + ch + u( 8.0f ), "text entry, focused", TEXT_META, p.textSecondary );
		// checkboxes
		const Int cx = ex;
		const Int cy = y + ch + u( 40.0f );
		Draw::checkbox( cx, cy, TRUE, STATE_REST );
		text( cx + u( 32.0f ), cy, "Limit Superweapons", TEXT_LABEL, p.textPrimary, 0, u( Metric::CheckboxSize ) );
		Draw::checkbox( cx, cy + u( 34.0f ), FALSE, STATE_REST );
		text( cx + u( 32.0f ), cy + u( 34.0f ), "Limit Superweapons", TEXT_LABEL, p.textPrimary, 0, u( Metric::CheckboxSize ) );
		Draw::checkbox( cx, cy + u( 68.0f ), TRUE, STATE_DISABLED );
		text( cx + u( 32.0f ), cy + u( 68.0f ), "Limit Superweapons", TEXT_LABEL, p.textDisabled, 0, u( Metric::CheckboxSize ) );
		// slider
		const Int sx = ex + cw + u( 40.0f ), sw = u( 240.0f );
		text( sx, y - u( 22.0f ), "Game Speed", TEXT_META, p.textSecondary );
		Draw::slider( sx, y + u( 10.0f ), sw, 0.6f, 10, STATE_REST );
		{
			const Int vx = sx + sw + u( 14.0f ), vw = u( 56.0f ), vh = u( 32.0f );
			Draw::control( vx, y + u( 6.0f ), vw, vh, STATE_REST );
			text( vx + vw / 2, y + u( 6.0f ), "30", TEXT_LABEL, p.textPrimary, 1, vh );
		}
		text( sx, y + ch + u( 8.0f ), "slider: fader knob, ticks per step, read-out", TEXT_META, p.textSecondary );
		Draw::slider( sx, y + ch + u( 40.0f ), sw, 0.25f, 10, STATE_DISABLED );
	}
	y += u( 310.0f );

	// ---- panel with motifs -------------------------------------------------------------------------------------------
	if( y + u( 230.0f ) <= H )
	{
		text( x, y, "Panel and motifs", TEXT_SECTION, p.textPrimary );
		y += u( 36.0f );
		const Int pw = u( 600.0f ), ph = u( 180.0f );
		Draw::panel( x, y, pw, ph );
		text( x + u( 24.0f ), y + u( 18.0f ), "Players", TEXT_SECTION, p.textPrimary );
		Draw::twoToneRule( x + u( 24.0f ), y + u( 52.0f ), pw - u( 48.0f ) );
		Draw::ruler( x + u( 24.0f ), y + u( 76.0f ), pw - u( 48.0f ) );
		text( x + u( 24.0f ), y + u( 110.0f ), "drafting grid, corner brackets, two-tone rule, ruler", TEXT_META, p.textSecondary );
		// swatches as used in the player roster
		const Color colors[] = { GameMakeColor( 0x2F, 0x6B, 0xFF, 255 ), GameMakeColor( 0xD8, 0x3A, 0x3A, 255 ), GameMakeColor( 0x3D, 0xA3, 0x5D, 255 ), GameMakeColor( 0xE8, 0xA3, 0x3D, 255 ) };
		for( Int i = 0; i < 4; ++i )
			Draw::swatch( x + u( 24.0f ) + i * u( 30.0f ), y + u( 136.0f ), u( 20.0f ), colors[ i ] );
	}

	TheDisplay->endBatch();
}
