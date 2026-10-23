// zh-pipeline @feature 2026-10-22 Menu UI system: style tokens, unit scale and the shared control renderers.
// Design: docs/UI-DESIGN.md (§3 tokens, §4 components, §6.2 this module). Everything here is client-side presentation;
// nothing feeds back into the simulation.
#pragma once

#include "Lib/BaseType.h"
#include "GameClient/Color.h"
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"

namespace UIStyle
{
	// ---- switches (CommandLine.cpp: -classicui, -uitest) --------------------------------------------------------------
	Bool isEnabled();            ///< the procedural menu UI is active (default TRUE)
	void setEnabled( Bool on );
	Bool isTestScreen();         ///< draw the component sheet over the shell (-uitest)
	void setTestScreen( Bool on );
	const AsciiString &devScreen();              ///< -uiscreen <Layout.wnd>: opened once the main menu is up (dev tooling)
	void setDevScreen( const AsciiString &name );

	// ---- scale and frame (§5): the content frame is 16:9, centred, filling the height (letterboxed by the scene on wider
	// screens) or the width (bands on 16:10 and squarer). One design unit = frame height / 1080.
	void frame( Int &x, Int &y, Int &w, Int &h );
	Real unit();
	Real uf( Real units );       ///< design units -> screen pixels
	Int  u( Real units );        ///< design units -> whole screen pixels (at least 1 for a positive input)

	// ---- palette (§3.1), ARGB ----------------------------------------------------------------------------------------
	struct Palette
	{
		Color ground;            // behind everything when no scene renders
		Color panelFill;         // translucent panel over the shell map
		Color panelFillRaised;   // dropdown lists, dialogs
		Color lineHair;          // hairlines, panel edges
		Color lineStrong;        // control borders at rest
		Color signalBlue;        // focus, selection, hover
		Color signalCyan;        // the original's cyan, hairlines only
		Color signalBlueDeep;    // pressed fills
		Color amber;             // primary action
		Color amberHover;
		Color amberInk;          // text on amber
		Color textPrimary;
		Color textSecondary;
		Color textDisabled;
		Color danger;
	};
	const Palette &palette();

	// ---- metrics in design units (§3.3) --------------------------------------------------------------------------------
	namespace Metric
	{
		constexpr Real RadiusPanel = 10.0f;
		constexpr Real RadiusControl = 6.0f;
		constexpr Real RadiusMenuButton = 8.0f;
		constexpr Real RadiusSwatch = 4.0f;
		constexpr Real ControlHeight = 44.0f;
		constexpr Real RowHeight = 40.0f;
		constexpr Real MenuButtonWidth = 440.0f;
		constexpr Real MenuButtonHeight = 52.0f;
		constexpr Real Hairline = 1.0f;
		constexpr Real FocusRing = 2.0f;
		constexpr Real Bracket = 14.0f;
		constexpr Real BracketStroke = 2.0f;
		constexpr Real GridStep = 24.0f;
		constexpr Real CheckboxSize = 20.0f;
		constexpr Real SliderTrack = 4.0f;
		constexpr Real SliderKnobW = 10.0f;
		constexpr Real SliderKnobH = 24.0f;
	}

	// ---- colour helpers ------------------------------------------------------------------------------------------------
	Color mix( Color a, Color b, Real t );      ///< interpolate in linear light (alpha linearly)
	Color withAlpha( Color c, Real alpha01 );   ///< same RGB, new alpha

	// ---- shared control renderers (§4). Screen-pixel rectangles; callers draw their own text. ---------------------------
	enum ControlState
	{
		STATE_REST = 0,
		STATE_HOVER,
		STATE_PRESSED,
		STATE_DISABLED
	};
	enum ButtonKind
	{
		BUTTON_NORMAL = 0,
		BUTTON_PRIMARY,
		BUTTON_DANGER
	};

	// ---- text (§3.2): role fonts, small caps, alignment. Draw order is cached per frame: call beginFrame() once per
	// frame before any draw(). Returns the drawn width in pixels. align: 0 left, 1 centre, 2 right; boxHeight > 0 centres
	// the text vertically in a box of that height at y.
	namespace Text
	{
		void beginFrame();
		Int  width( const char *ascii, Int role );
		Int  width( const UnicodeString &text, Int role );
		Int  draw( Int x, Int y, const char *ascii, Int role, Color color, Int align = 0, Int boxHeight = 0 );
		Int  draw( Int x, Int y, const UnicodeString &text, Int role, Color color, Int align = 0, Int boxHeight = 0 );
		Int  lineHeight( Int role );
	}

	namespace Draw
	{
		void panel( Int x, Int y, Int w, Int h, Bool grid = TRUE, Bool brackets = TRUE );
		void brackets( Int x, Int y, Int w, Int h, Real radiusUnits );
		void grid( Int x, Int y, Int w, Int h );
		void twoToneRule( Int x, Int y, Int w );
		void ruler( Int x, Int y, Int w );
		void button( Int x, Int y, Int w, Int h, ControlState state, ButtonKind kind = BUTTON_NORMAL, Bool focused = FALSE );
		void menuButton( Int x, Int y, Int w, Int h, ControlState state, Bool focused = FALSE );
		void control( Int x, Int y, Int w, Int h, ControlState state, Bool focused = FALSE );   ///< dropdown / entry body
		void chevron( Int cx, Int cy, Color color );
		void listPopup( Int x, Int y, Int w, Int h );
		void listRow( Int x, Int y, Int w, Int h, Bool selected, Bool hovered );
		void caret( Int x, Int y, Int h );
		void checkbox( Int x, Int y, Bool checked, ControlState state, Bool focused = FALSE );
		void slider( Int x, Int y, Int w, Real value01, Int steps, ControlState state, Bool focused = FALSE );
		void swatch( Int x, Int y, Int size, Color color );
		void focusRing( Int x, Int y, Int w, Int h, Real radiusUnits );
		Color textColor( ControlState state, ButtonKind kind = BUTTON_NORMAL );
	}
}
