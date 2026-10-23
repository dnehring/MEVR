#pragma once

// The main menu's own RmlUi decorators (research/28), for what the approved mockup draws that RCSS can't: CSS's
// clip-path chamfers and the mockup's inline SVG icons. Registered once, before any document loads (Ui::Init).
//
//   decorator: chamfer(<cut> <fill-top> <fill-bottom> <edge-top> <edge-bottom> <edge-width> <highlight>);
//       A box with its top-left and bottom-right corners cut at 45 degrees (`cut`, 0: a plain box): a vertical
//       gradient fill inside a ring of `edge-width` (a vertical gradient too: the buttons' top-lit edge), and a
//       1 px `highlight` line along the inside of the top edge. Values fall through in that order; trailing ones
//       may be left out (edge: none, highlight: none).
//   decorator: backdrop(<cut> <image> <tint> <edge> <accent> <accent-length> <scanline>);
//       The whole menu: `image` (a texture, "mevr:backdrop": the blurred game behind it) across a chamfered panel,
//       multiplied by `tint`, a 1 px `edge`, `accent` strokes 3 px wide along the two cut corners and on along the
//       top and bottom edges for `accent-length`, and faint `scanline` rows every 3 px.
//   decorator: icon(<name>);
//       One of the mockup's line icons (MenuIcons below) in the element's `color`, fit to its box.
namespace mevr::menu {

void RegisterDecorators();

// The icons' names, for the preview and the tests.
char const* const* IconNames(int& count);

} // namespace mevr::menu
