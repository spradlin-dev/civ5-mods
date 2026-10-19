# Map picture check

A way to test the map_image drawing without the game, and to measure whether a model
can read it.

1. Build the harness against the mod's drawing file (Visual Studio 2008 command prompt):

       cl /EHsc /nologo /D_CRT_SECURE_NO_WARNINGS test_mapdraw.cpp ..\..\CvGameCoreSource\CvGameCoreDLL_Expansion2\Claude\ClaudeMapDraw.cpp

2. Run `test_mapdraw.exe`. It writes `map_example.png`, a made-up neighborhood, and
   `map_truth.json`, the true contents of every hex in it.

3. Give the picture and the tool's legend to a model with no other context and have it
   transcribe every hex to a JSON file with the same keys as the truth file.

4. Score it: `python score.py map_truth.json <transcription>.json`. It prints the share of
   hexes read right per field and lists every mistake.

On 2026-10-18 a Claude Sonnet reader scored 99% on the 91 hexes, with every field at
100% except terrain type (desert, plains and tundra under a border tint).
