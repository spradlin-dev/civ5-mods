# Plan: map image tool

Started 2026-10-18 with the close view. Saved on 2026-10-14.

## Why

The map tool answers tile by tile, so a model only learns what is on a tile if it
asks about that tile. A human sees the whole map at once: borders, armies, the shape
of a front. The model missed that its troops stood inside another civilization's
borders because it never asked about those tiles.

A real screenshot of another place is not possible: the game has one camera and all
drawing happens in the game's own program, out of the mod's reach, and so does the
strategic view. Moving the camera to take a picture would jump the human's view
every time. The mod draws its own picture from the same data the map tool reports.

## Design (agreed 2026-10-18)

**Pictures are cheap.** A model is charged about one token per 750 pixels. A
radius-5 view at a 96-pixel hex is roughly 1190 by 940 pixels, about 1500 tokens,
the size the user chose after smaller drafts left readers guessing at the small
labels; the map tool's text for the same area runs to thousands. The saving comes
from replacing text, not from shrinking the picture. Wider views are scaled down so
the picture never exceeds the longest edge a model is sent.

**Legible, not coded.** Models read pictures in patches, not pixel by pixel, so a
one-pixel colour code is the one thing they read badly and no legend would rescue
it. The picture copies the game's strategic view: flat colours, short labels in 10
to 13 pixel type, clear outlines.

**The legend lives in the tool description.** The description is sent once per
session and cached, so the fixed legend costs nothing per call. Only what varies
per picture, which civilization has which colour, is drawn on the picture itself.

**What is drawn** (settled over seven rounds of blind reading by a model on
2026-10-18, each round answering "what is where" from the picture and a legend only).
A hex grid, 96-pixel hexes, with the coordinates in bold inside every hex, each row's
y at the left edge and the top row's x along the top, so the model can act on what it
sees without a second lookup. Crisp unsmoothed text, Verdana 15 to 20 pixels; smoothed
text that small reads as a blur. Terrain as flat colour, hills darker with an "h",
mountains a grey hex with MTN in the middle; owned hexes carry a grid of dots in the
owner's colour over a light tint, with a thick border line just inside the territory's
edge (the user's choice after tint alone read too faint); rivers as cyan edges with a
dark outline, roads as dark brown lines with a dot; the text inside a hex in three
centred rows, coordinates on top, the city, units or mountain mark in the middle, and
resource, feature and improvement along the bottom, each word outlined in the opposite
tone so it reads over the dots and the hatching. Cities as labelled boxes
in the owner's colour on a white-outlined hex. Units as circles in the owner's colour
with a short name and a thick ring that says whose: white yours, green a teammate's,
red an enemy's, grey anyone else's; two drawn per hex and "+N" for the rest. Fog as a
diagonal hatch over tiles seen but not visible now (no units shown, white lettering);
never-seen tiles as a dark hex with "?". The legend names each civilization's colour
and relation. Beside the picture the tool returns a roster of the cities and units in
it with exact coordinates, so the picture carries the shape and the text the facts.

**Two sizes.** A close view, radius up to 8 around a tile, for tactical decisions
(built first). An overview of the whole explored map at tiny hexes for borders and
armies, the picture to look at before deciding on a war (not started).

**How.** The DLL draws with the Windows drawing library (GDI+) and saves a PNG; no
extra dependency. The bridge returns it as MCP image content beside a short text
result. The map tool stays the authoritative data. A copy of the last picture is
saved as Logs/map_image_last.png so the human can see what the model saw.

## Measured (2026-10-18)

The check in tools/mapdemo draws a made-up neighborhood with the mod's own drawing
code, writes the truth of every hex, and lets a model with nothing but the picture and
the legend transcribe all 91 hexes; a script scores the transcription field by field.
A Claude Sonnet reader scored 97% on the first measured draft and 99% on the next,
with cities, units, roads, rivers, resources, improvements, features, fog and
ownership all at 100% and only terrain type (desert, plains, tundra under a border
tint) below that. Rerun the check after any change to the drawing.

## Open questions

- Whether radius 5 is the right default once a model has used it in play.
- The overview's hex size and whether it needs labels at all.
