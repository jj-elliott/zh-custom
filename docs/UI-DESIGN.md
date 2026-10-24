# Zero Hour menu UI redesign — design document

Status: **draft for sign-off, 2026-10-22.** Nothing in the engine implements this yet. The three gadget edits and the layout
anchoring draft on `render/textures` are superseded by this document and will be dropped or rewritten against it.
Mockups: https://claude.ai/artifact/7q5GMmP4KYCxSdWcpHbnqQ (main menu, skirmish setup, component sheet).

## 1. What this is

A new menu UI *system* for the shell (front-end) screens, designed for 16:9 and drawn procedurally. Rectangles, frames, lines,
gradients, dropdowns, sliders, check marks and text are geometry and style, never sprites cut from a 512×512 atlas. Real art
assets stay as images: the title logo, faction emblems, general portraits, map previews, loading-screen art.

This is not a reskin of the existing gadgets and not a texture upscale. The 2003 shell is built from 800×600 layouts stretched to
the screen, 1-pixel sprite borders, GDI-rendered Times New Roman and Arial, and an sRGB-ignorant blue (37,52,255) that was chosen
on a CRT. The target is a shell that looks designed for a 4K 16:9 display in 2026 and is at least as fast to use as the old one.

Out of scope for this unit of work: the in-game HUD (control bar, shortcut bars, chat), loading screens, the Options dialog
internals, GameSpy/WOL screens that cannot be reached any more. They stay on the sprite path behind a switch until their own
design pass.

## 2. Principles

1. **Geometry, not textures.** Every chrome element is drawn from style tokens at native resolution. No asset ever needs
   upscaling for the chrome to be sharp.
2. **Designed for 16:9.** Screens are laid out with anchors and stacks for a 16:9 frame (16:10 and 21:9 degrade gracefully by
   letterboxing the content frame, never by stretching). The 800×600 rect mapping is not used by redesigned screens.
3. **One scale unit.** Everything is sized in design units where 1 unit = screen height / 1080. A 44-unit button is 44 px at
   1080p and 88 px at 4K. Type follows the same unit (the "Strict" font method upstream already has).
4. **The shell map is the hero.** Menus sit on the live 3D scene in translucent panels; the UI never covers more of it than it
   needs. No decorative frames or rulers around the viewport.
5. **Quiet by default, one accent for attention.** Blue is the system's identity colour and is used for focus and selection.
   Amber is reserved for the one action the player is here for (Start, Ready, Join) and for warnings.
6. **Legible at arm's length.** Minimum text 13 units; body 15–17; tabular numerals in any column of numbers. Contrast ≥ 4.5:1
   on the darkest panel fill over the brightest shell-map frame we ship.
6b. **Small caps, in homage to the original.** The 2003 shell set every label in capitals. Labels, menu items, headings and
   button text are set in small caps (an initial full capital, the rest at cap height × 0.78, tracked +4%). Body and list
   text (player names, map names, descriptions) stay mixed case. In the engine this is drawn by the font renderer:
   lowercase letters are rendered as capitals at the reduced size, so no small-caps font cut is needed.
6c. **No new copy.** Every string on a redesigned screen is the game's own string from `generals.csf` (`GUI:...`), in the
   game's wording and spelling. The redesign adds no subtitles, hints, summaries or status prose. Where a control needs a
   label the original did not have, the label is the control's existing tooltip text or it is left without one.
7. **Behaviour untouched.** Window names and callbacks that game code binds to (`SkirmishGameOptionsMenu.wnd:ButtonStart`, …)
   keep their names. The redesign changes how controls are drawn and where they sit, not what they do.
8. **Taste is the owner's.** Palette, typeface and the final look are signed off from mockups and then from screenshots of the
   real build at 4K and 1080p, screen by screen.

## 3. Design tokens

### 3.1 Colour (sRGB hex; mixing and gradients are computed in linear light)

| Token | Value | Use |
|---|---|---|
| `ink.ground` | `#070B14` | fallback behind everything when no scene is rendered |
| `panel.fill` | `#0B1424` @ 76% | translucent panel over the shell map |
| `panel.fill.raised` | `#122036` @ 88% | dropdown lists, dialogs |
| `line.hair` | `#7FA6D9` @ 18% | hairlines, panel edges, table rules |
| `line.strong` | `#7FA6D9` @ 40% | control borders at rest |
| `signal.blue` | `#4FA3FF` | focus ring, selected row, hover border, links |
| `signal.blue.deep` | `#1E66C9` | pressed fills |
| `attention.amber` | `#F2B544` | primary action fill, warnings, "ready" |
| `attention.amber.ink` | `#1A1300` | text on amber |
| `text.primary` | `#E8EEF6` | |
| `text.secondary` | `#9FB0C8` | labels, hints |
| `text.disabled` | `#5E6C80` | |
| `danger` | `#FF6B6B` | destructive confirms, disconnects |
| player colours | from game data | drawn as swatches, never as tinted chrome |

Gradients are subtle and vertical only: a panel may run from `panel.fill` to 6% darker; a primary button from amber to 8%
darker. Interpolated in linear light so the midpoint is not muddy.

### 3.2 Type

One family: **Barlow** (SIL OFL), `Barlow Semi Condensed` for titles and the main-menu rail, `Barlow` for everything else.
DIN-descended, tabular numerals, reads as equipment plate rather than sci-fi. Fallback: `Arial`.

| Role | Face | Size (units) | Weight | Case |
|---|---|---|---|---|
| Screen title | Semi Condensed | 40 | 600 | small caps |
| Main-menu button | Semi Condensed | 26 | 600 | small caps, tracked |
| Section heading | Semi Condensed | 22 | 600 | small caps |
| Control label / button text | Barlow | 17 | 500 | small caps |
| Body, names, list text | Barlow | 17 | 500 | mixed |
| Table text | Barlow | 15 | 500, tabular | mixed |
| Meta | Barlow | 13 | 500 | small caps |

The title logo is UI art: `MappedImage GeneralsLogo`, a 370×120 sprite in `SCSmShellUserInterface512_001.tga`, drawn by
`MainMenu.wnd`. It is kept as an image, right-anchored above the menu column. It is the one shell asset worth a
higher-resolution version later (the splash `TitleScreen` art is a candidate source); the chrome never depends on it.

### 3.3 Space, radius, line

- Spacing scale (units): 4, 8, 12, 16, 24, 32, 48, 64. Screen safe margin: 64 horizontally, 48 vertically.
- Radii: panel 10, control 6, swatch 4, pill 999. One radius per hierarchy level, not one radius for everything.
- Line widths: hairline 1, control border 1, focus ring 2 (outside the control, `signal.blue`).
- Control heights: button/dropdown/entry 44; table row 40; slider track 2 with a 16 knob; checkbox 20.
- Shadows: none. Depth comes from fill opacity and hairlines. (The fixed-function path has no cheap blur; a shadow is a
  gradient ring and would read as 2010.)

### 3.4 Motifs: where the personality comes from

Everything that made the 2003 shell recognisable is kept as a motif, redrawn as geometry:

- **The drafting grid.** `MainMenuRuler` was a translucent grey band with a fine blueprint grid. Panels now carry that grid as a
  faint pattern inside them (1-unit lines every 24 units at 7% of `line.hair` over `panel.fill`), so a panel reads as a sheet
  on the drafting table rather than a web card. The frame itself comes back on the main menu (asked for 2026-10-24) as
  `band` decorations: translucent `panel.fill` bands along the four edges of the 16:9 frame (32 units top and bottom, 24 at
  the sides) with the same grid at 22% and a hairline edge, the ruler strip inside the bottom band.
- **The ruler.** Tick strips (1 × 6 every 8 units, 1 × 10 every 40) run under every screen title, along the top of the map
  preview, and as a single full-width strip along the bottom edge of the main menu, where the old frame's ruler was.
- **The two-tone rule.** The original button border was blue, cyan, blue. A panel title is underlined by a 1-unit `signal.blue`
  line with a 1-unit cyan (`#00E3FF` at 60%) line 3 units below it; a button's hover state lights the same cyan hairline
  inside its top edge.
- **Corner brackets.** 14 × 14, 2-unit `signal.blue` at 70%, on panel corners and on the map preview: the reticle of the
  Generals' Challenge screen and of every RTS targeting cursor.
- **Stencil titles.** Screen titles are set in Saira Stencil One (SIL OFL), uppercase, the stencil tradition the original
  `Generals` display font belonged to. Everything else stays Barlow.
- **The medals.** The battle-honours strip comes back with the real medal sprites (`HonorStreak_*`, `Endurance_*`,
  `Domination_*`, `*Campaign_*`, `Challenge_*`, `HonorBlitz5`, `Apocalypse`, `HonorBattleTank`); unearned ones sit at 25%.

## 4. Components

All states: rest, hover, pressed, focused (keyboard), disabled, and for list items: selected.

- **Menu button** (main menu): a box, as in the original: 52 tall, 440 wide, radius 8, `line.strong` border, `panel.fill` fill,
  text centred in small caps. Hover: border and text `signal.blue`, fill +10% blue. Pressed: fill `signal.blue.deep` @ 35%.
  The column is right-anchored and its right edge lines up with the logo's right edge.
- **Button**: 44 tall, radius 6, `line.strong` border, `panel.fill` fill. Hover: border `signal.blue`, fill +8% blue. Pressed:
  fill `signal.blue.deep` @ 35%. Primary: amber fill, dark text, no border.
- **Dropdown (ComboBox)**: button shape, value left, chevron right (inline stroke, 1.5 units). Open list: `panel.fill.raised`,
  rows 40, selected row `signal.blue` @ 20% with a 3-unit left bar.
- **Text entry**: button shape, caret `signal.blue`, placeholder `text.secondary`.
- **Checkbox**: 20 box, radius 4, check as a 2-unit stroke; checked fill `signal.blue`.
- **Slider**: an instrument fader, not a web slider: 4-tall track with radius 2, filled part `signal.blue`, tick marks (1 × 6,
  `line.strong`) under the track at every step, a 10 × 24 rectangular knob (radius 2, `text.primary`, 1 border, centre line)
  and the value in a 56-wide read-out box to the right in tabular numerals. Keyboard: arrows step, Home/End.
- **Table / list**: header row `text.secondary` 13, hairline below; rows 40, alternating fill 0 and 3%; selected row as above.
  Scroll bar: a 24-unit column at the right edge (buttons, track and thumb windows all 24 wide, so it can be grabbed);
  drawn as an 8-unit track in `line.hair` at 60% and an 8-unit thumb bar in `text.secondary` (hover `text.primary`,
  drag `signal.blue`); the thumb's height follows the visible share of the content and never drops below 48 units;
  scroll buttons are 5-unit chevrons. (Round 2 of the skirmish review: the 4-unit, 16 px thumb was fiddly and too close
  to its track in colour.)
- **Panel**: radius 10, `panel.fill`, hairline edge. Title inside the panel at 22.
- **Swatch**: 20×20 radius 4 for player colour; a 24 emblem image for faction.
- **Tooltip**: `panel.fill.raised`, 13 text, radius 6, 8 padding, no arrow.

## 5. Layout rules for 16:9

- The **content frame** is 16:9, centred, filling the height. On wider screens the frame is letterboxed by the shell map (the
  scene is drawn full-screen either way); on 16:10 the frame fills the width and leaves a 5% band top and bottom.
- Inside the frame, every element has a horizontal anchor (left, centre, right) and a vertical anchor (top, middle, bottom)
  with offsets in units. Groups are stacks (vertical or horizontal) with a gap; a stack's children never need coordinates.
- The main menu's commands are a **right-anchored column of boxed buttons** under the logo, the column's right edge on the
  logo's right edge. The blueprint band with rulers around the viewport is UI art too (`MainMenuRuler`, a 1024² full-screen
  image stretched to the display, which is why it reads thick and off-centre at 16:9) and is removed; the shell map then
  fills the whole screen. The only other element is the build tag, bottom-left, 13 units.
- Form screens use a **two-column split** at 60/40 with a 32 gutter; the primary action is bottom-right, Back bottom-left,
  always the same positions on every screen.
- Dialogs are centred, max 720 wide, on a 40% scrim (`scrim` + `dialog` decorations; the panel is on `panel.fill.raised`
  with a `line.strong` edge). Map selection is the first: 1000 wide, since it holds the list and the map preview side by
  side. A dialog's legacy parent window is moved over the dialog first, so its buttons are reachable (hit-testing only
  descends into children of a window under the pointer).
- Nothing is positioned by scaling an 800×600 rectangle. Redesigned screens get a layout description of their own (see 6.4).

## 6. Engine architecture

6.1 **Primitives (device layer, `W3DDisplay` over `Render2DClass`).** New `Display` virtuals implemented in `W3DDisplay`:
`drawRoundedRect(fill, stroke, radius)`, `drawRoundedRectGradient(top, bottom)`, `drawLine(width)` with clean caps,
`drawCheck`, `drawChevron`, `drawKnob`. Rounded corners are tessellated (8–12 segments per corner at 1080p, scaled with the
radius) and rely on the 4× MSAA DXVK already forces for edge quality; a no-MSAA fallback adds a 1-unit feathered outer ring.
Gradients: stops converted to linear, the quad split into 8 bands, back to sRGB per vertex. No D3D calls outside `WW3D2`.

6.2 **Style module (`GameClient/UIStyle.h/.cpp`).** The tokens in §3 as one struct with named members, a scale function
`u(units)` = `units * displayHeight / 1080`, and the font roles. Loaded with defaults from code; an optional `UI\style.ini`
override so palette experiments do not need a rebuild.

6.3 **Gadget renderers.** Built 2026-10-23 for push buttons and static text (`GameClient/UIGadgets`): the W3D draw
functions (`W3DPushButton.cpp`, `W3DStaticText.cpp`) first ask `UILayout::styleOf(window)` and hand a styled window to
`UIGadgets`, which draws the frame from `UIStyle::Draw` (rest, hover, pressed, disabled from the window's status and
state) and the window's own localised text through `UIStyle::Text` in the role the style implies. An all-caps string
("SOLO PLAY") is title-cased before small caps, so the game's strings read as the mockup. Dropdowns, entries, check
boxes, lists and sliders follow with the skirmish screen. The old draw functions remain for everything else.

6.4 **Layout descriptions.** Built 2026-10-23 (`GameClient/UILayout`). A text file per redesigned screen,
`UI\Layouts\<Screen>.layout` (repo: `config/UI/Layouts`), names windows of the `.wnd` and gives each an anchor (left,
centre, right × top, middle, bottom), offsets and size in units, or places them in a vertical `stack` with a gap; `hide`
swaps a window's draw function for a no-op (its children, input and the shell's show/hide logic are untouched: the
main menu's submenu panels are hidden this way and still toggle their buttons); an optional style (`menu`, `button`,
`primary`, `danger`, `label`, `caption`, `meta`, `section`, `title`, `panel`, `entry`, `dropdown`, `checkbox`, `list`,
`slider`, `radio`, `body` for word-wrapped message text on the stock draw, `dialog` for a USER window drawn as a dialog
panel that follows the window when code moves it) marks the windows the procedural renderers draw; `panel`, `band`,
`dialog`, `rule`, `ruler` and `scrim` lines add decorations. Each layout's decorations are drawn by `GameWindowManager::winRepaint` right before its own
root window, so they sit under that screen's windows and over every screen beneath it (a dialog's scrim dims the
screen it opened from; drawing them all first, from `InGameUI::postDraw`, let the screen underneath paint over a
dialog's panel). Windows are named `Name`, `$Name` (short for `<Screen>.wnd:Name`), `text:<label key>`
(an unnamed window by its label), `parent:<window>` or `at:l,t,r,b` (an unnamed window by its legacy 800x600 rectangle,
matched before any ancestor moves). `include <file>` reads another layout file, so blocks shared between screens (the
skirmish players table, the map and rules panels) are written once and used by the LAN and online setup screens too:
`GameSetupPlayers`, `GameSetupMap`, `GameSetupRules` and `MapSelectDialog` are the shared blocks (built 2026-10-24),
included by the skirmish and LAN screens, which add only what differs (battle honours and the game-speed slider on
skirmish; accept markers and the lobby chat on LAN). Applied at the end of
`winCreateFromScript`, before the screen's init callback, so callbacks and names are untouched and the init code
sees the final positions. Windows not mentioned keep their legacy position. The content frame (`UIStyle::frame`) is
16:9, centred: letterboxed by the scene on wider screens, bands on 16:10; one unit = frame height / 1080.

6.5 **Type.** Built 2026-10-22. Fonts ship as `.ttf` under `<gameDir>\Fonts\` (`config/Fonts` in the repo, OFL text beside
them; stage-install and package copy `config/*`) and are registered process-private on first use (`UIFonts::ensureInit`,
`AddFontResourceEx(FR_PRIVATE)`), so nothing is installed on the machine. GDI sees Google's static cuts as their own
families: `Barlow Medium`, `Barlow SemiBold`, `Barlow Semi Condensed Medium`, `Barlow Semi Condensed SemiBold`,
`Saira Stencil One`; a weight is chosen by family, never by `FW_BOLD`, which GDI would synthesise. `FontLibrary::getFont`
maps the historical names (`Arial`, `Times New Roman`, `Generals`, `Placard MT Condensed`) onto them while the system is on,
so every legacy screen and the HUD pick the typeface up at once; `-classicui` restores the originals; Courier and the Unicode
fallback are left alone. Role sizes are design units treated as pixels: `Create_GDI_Font` takes points at 96 dpi, so the
request is `units × 0.75` points. Small caps (`UIStyle::Text`): a string is split into runs, lowercase runs are uppercased
and set in the same face at 78% size, baseline-aligned by the ascent ratio; the sentence renderer reports each run's width
with the last glyph's overlap padding, measured per font as `2·w("H") − w("HH")` and subtracted at every join. Legacy
screens keep the user's resolution font adjustment until they are redesigned; the new system sizes text in units only.

6.6 **Dev tooling.** `-uiscreen <seq>` (built) runs a `;`-separated sequence a moment after the main menu is up: a
`<Screen>.wnd` item pushes that shell screen, `click:<window>` presses a button on the top screen (so
`SkirmishGameOptionsMenu.wnd;click:ButtonSelectMap` reaches map selection);
`run.ps1 -Screenshot N` focuses the game, nudges the pointer and captures the game window; `-Click "x,y;..."` posts
clicks but the shell did not act on them in testing, so `-uiscreen` is the way to reach a screen. `-uitest` draws the
component sheet. Still to do: an overlay that shows anchors and unplaced windows.

## 7. Delivery

1. Sign-off on this document and the mockups (palette, type, main menu, skirmish setup). ← **we are here**
2. ~~Primitives + style module + the component sheet rendered by the engine as a test screen.~~ Done 2026-10-22, passed.
3. ~~Fonts in, substitution table, sizing method.~~ Done 2026-10-22; captures in `docs/ui-screens` (sheet at 4K and 1080p,
   the legacy main menu already in Barlow).
4. ~~Main menu on the new system end to end.~~ Built 2026-10-23: `MainMenu.layout` (logo on its visible bounds, the
   boxed column and every submenu column, frame and panel squares hidden, shortcut buttons and emblem row placed);
   captures in `docs/ui-screens`. Signed off 2026-10-23 after a click test (panels had to cover the column for hit-testing,
   and the shell's rectangle flash transitions are skipped on layout-managed windows).
5. ~~Skirmish setup.~~ Built 2026-10-23: `SkirmishGameOptionsMenu.layout` with panel decorations, the entry, dropdown,
   check box, list and slider renderers, the medal strip (`ListboxInfo`) on the honours row; `-uiscreen <Layout.wnd>`
   opens any screen for captures (`docs/ui-screens/skirmish-4k-at-1080.jpg`). Signed off 2026-10-24 after four review
   rounds (chevron geometry, medal aspect, entry text centring, Color column width, the list scroll bar, map selection as
   a dialog with a scrim, the main menu frame bands).
5b. Remaining shell screens, in the order the co-op flow meets them. Built 2026-10-24, review pending: Direct connect
   (`NetworkDirectConnect.layout`, a centred form panel), LAN game options (`LanGameOptionsMenu.layout`, the shared
   blocks plus accept markers and chat) and its map selection (`LanMapSelectMenu.layout`), LAN lobby
   (`LanLobbyMenu.layout`), the message boxes (`MessageBox.layout`, `QuitMessageBox.layout`: the box window itself is the
   `dialog`, the button slots are arranged so the primary action lands at the right). ← **we are here**. Then Options,
   Load/Save, Replay, Solo play (difficulty, challenge), credits and the score screen.
6. Package: fonts in the release zip, `-classicui` switch kept for one release, FEATURE-PARITY and CODE-MAP updated.

Each step is a separate small commit, reviewed from screenshots before the next starts. Nothing in the simulation is touched;
`verify.ps1` runs anyway at each step as the standing rule.

## 8. Review log

- 2026-10-22, round 1: boxed buttons kept on the main menu (rounded), column right-aligned under the logo; no invented copy,
  original strings only; small caps in homage to the all-caps original; slider redrawn as an instrument fader; the old
  blueprint frame removed from the backdrop.

- 2026-10-24: skirmish setup and map selection signed off (rounds 1 to 4 above); the main menu frame returns as grid bands
  at the owner's request.
- 2026-10-23: engine main menu signed off.
- 2026-10-22, round 2: "reads as generic temp UI" and the battle-honour icons were missing. Added §3.4 motifs (grid, ruler,
  two-tone rule, corner brackets, stencil titles) and restored the medal strip with the real sprites.

Still open: palette direction (cool navy + signal blue + amber, or closer to the original saturated blue); Barlow vs Saira /
IBM Plex Sans Condensed / Rajdhani; whether the battle-honours record stays under the roster.
