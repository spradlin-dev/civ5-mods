# Plan: map image tool

Not started. Saved on 2026-10-14.

## Why

The map tool answers tile by tile, so a model only learns what is on a tile if it
asks about that tile. A human sees the whole map at once: borders, armies, the shape
of a front. The model missed that its troops stood inside another civilization's
borders because it never asked about those tiles.

A real screenshot of another place is not possible: the game has one camera and all
drawing happens in the game's own program, out of the mod's reach. Moving the camera
to take a picture would jump the human's view every time.

## What to build

A `map_image` tool that draws a picture from the same data the map tool reports and
returns it as an image. It takes a center tile, a radius and an optional choice of
what to show. Nothing on the human's screen changes.

- A hex grid with coordinates along the edges, so the model can read positions.
- Terrain, hills, mountains, rivers and water in plain colors.
- Borders and cities in each civilization's color, with city names.
- Units as small marks in their owner's color, with a legend.
- Fog follows the map tool: tiles the model's civilization has never seen are blank;
  tiles it has seen but cannot see now show only what it last knew, with no units.

## How

The DLL draws the picture itself and encodes it as a PNG without any library (an
uncompressed PNG is simple to write). The bridge returns it as MCP image content.

## Open questions

- Image size and radius that stay readable for the model.
- Whether to offer a whole-map overview at low detail as well.
