// Draws a made-up neighborhood with the mod's map renderer and writes the truth of every hex
// to map_truth.json, so a blind reading of the picture can be scored.
// The same library settings as the mod's drawing file, so the records match in memory.
#define _SECURE_SCL 0
#define _HAS_ITERATOR_DEBUGGING 0
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ClaudeMapDraw.h"

static unsigned Rgb(int r, int g, int b) { return (r << 16) | (g << 8) | b; }
static int Dist(int x1, int y1, int x2, int y2)
{
	int q1 = x1 - (y1 - (y1 & 1)) / 2, q2 = x2 - (y2 - (y2 & 1)) / 2;
	int dq = q2 - q1, dr = y2 - y1, ds = -dq - dr;
	int a = abs(dq), b = abs(dr), c = abs(ds);
	return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

struct Truth { int x, y; std::string terrain, feature, resource, improvement, owner, city; bool hills, mountain, fogged, unexplored, road, river; std::vector<std::string> units; };

int main()
{
	const int cx = 51, cy = 34, radius = 5;
	const unsigned FRANCE = Rgb(65, 105, 225), PERSIA = Rgb(200, 60, 60), IROQUOIS = Rgb(240, 200, 60);
	const char* RES[] = { "Iron", "Horses", "Oil", "Coal", "Wheat", "Cattle", "Gold", "Silk" };
	const char* IMP[] = { "farm", "mine", "trade", "pastu", "well" };
	std::vector<ClaudeMapHex> hexes;
	std::vector<Truth> truth;
	for(int y = cy - radius; y <= cy + radius; ++y)
		for(int x = cx - radius - 1; x <= cx + radius + 1; ++x)
		{
			if(Dist(cx, cy, x, y) > radius) continue;
			ClaudeMapHex h;
			Truth t;
			h.x = t.x = x; h.y = t.y = y;
			t.hills = t.mountain = t.fogged = t.unexplored = t.road = t.river = false;
			if((x >= 54 && y >= 37) || (x == 56 && y == 35)) { h.unexplored = true; t.unexplored = true; hexes.push_back(h); truth.push_back(t); continue; }
			bool water = x <= 46 || (x == 47 && y % 3 == 0);
			bool hills = (x * 7 + y * 3) % 5 == 0 && !water;
			bool mountain = (x == 53 && (y == 32 || y == 33));
			int kind = water ? 0 : ((x + 2 * y) % 5);
			if(water) { t.terrain = x <= 45 ? "ocean" : "coast"; h.fill = x <= 45 ? Rgb(52, 92, 176) : Rgb(120, 178, 228); }
			else if(kind == 2) { t.terrain = "plains"; h.fill = Rgb(188, 156, 88); }
			else if(kind == 3) { t.terrain = "desert"; h.fill = Rgb(240, 226, 120); }
			else if(kind == 4) { t.terrain = "tundra"; h.fill = Rgb(152, 170, 168); }
			else { t.terrain = "grass"; h.fill = Rgb(120, 165, 90); }
			if(hills) { int r = (h.fill >> 16) & 255, g = (h.fill >> 8) & 255, b = h.fill & 255; h.fill = Rgb(r > 32 ? r - 32 : 0, g > 32 ? g - 32 : 0, b > 32 ? b - 32 : 0); t.hills = true; }
			if(mountain) { h.fill = Rgb(96, 90, 86); t.mountain = true; h.centerLabel = "MTN"; }
			h.fogged = t.fogged = (y <= cy - 4 || x >= 55);
			h.riverE = (x == 50 && y >= 32 && y <= 35);
			h.riverSE = (x == 50 && y == 35);
			h.riverSW = (x == 51 && y == 31);
			t.river = h.riverE || h.riverSE || h.riverSW;
			char buf[16]; sprintf_s(buf, "%d,%d", x, y); h.coords = buf;
			if(!water && !mountain)
			{
				unsigned owner = (x <= 51) ? FRANCE : (x >= 54 ? IROQUOIS : 0);
				if(owner)
				{
					h.owned = true; h.borderColor = owner; t.owner = owner == FRANCE ? "France" : "Iroquois";
				}
				if((x + y) % 4 == 0) { h.topRight = "F"; t.feature = "forest"; }
				else if((x * 3 + y) % 11 == 0) { h.topRight = "J"; t.feature = "jungle"; }
				else if((x + 3 * y) % 13 == 0) { h.topRight = "M"; t.feature = "marsh"; }
				if(hills) h.topRight += "h";
				if((x * 3 + y) % 4 == 0) { t.resource = RES[(x + y) % 8]; h.topLeft = t.resource.substr(0, 6); }
				if((x + y) % 5 == 0 && x <= 51) { t.improvement = IMP[(x * y) % 5]; h.bottomLeft = t.improvement; }
				if(x == 49 && y == 34) { t.improvement = "mine"; h.bottomLeft = "mine"; }
				for(int k = 0; k < 6; ++k) h.road[k] = (y == 34 && x >= 48 && x <= 52 && (k == 1 || k == 4));
				h.hasRoad = t.road = (y == 34 && x >= 48 && x <= 52);
			}
			if(x == 49 && y == 33) { h.city = true; h.cityColor = FRANCE; h.cityLabel = "Dijon"; t.city = "Dijon"; t.owner = "France"; }
			if(x == 55 && y == 35) { h.city = true; h.cityColor = IROQUOIS; h.cityLabel = "Osininka"; t.city = "Osininka"; t.owner = "Iroquois"; }
			if(!h.fogged)
			{
				if(x == 51 && y == 34) { ClaudeMapUnit u; u.label = "Art"; u.fill = PERSIA; u.ring = Rgb(255, 255, 255); h.units.push_back(u); h.units.push_back(u); t.units.push_back("yours Artillery"); t.units.push_back("yours Artillery"); }
				if(x == 52 && y == 35) { ClaudeMapUnit u; u.label = "Rif"; u.fill = FRANCE; u.ring = Rgb(60, 230, 90); h.units.push_back(u); t.units.push_back("teammate Rifleman"); }
				if(x == 54 && y == 35) { ClaudeMapUnit u; u.label = "Pik"; u.fill = IROQUOIS; u.ring = Rgb(240, 40, 40); h.units.push_back(u); u.label = "Cat"; h.units.push_back(u); h.extraUnits = 3; t.units.push_back("enemy Pikeman"); t.units.push_back("enemy Catapult"); for(int k = 0; k < 3; ++k) t.units.push_back("enemy (not drawn)"); }
				if(x == 50 && y == 36) { ClaudeMapUnit u; u.label = "Wor"; u.fill = PERSIA; u.ring = Rgb(255, 255, 255); h.units.push_back(u); t.units.push_back("yours Worker"); }
				if(x == 48 && y == 37) { ClaudeMapUnit u; u.label = "Car"; u.fill = Rgb(120, 200, 120); u.ring = Rgb(150, 150, 150); h.units.push_back(u); t.units.push_back("other Caravan"); }
			}
			hexes.push_back(h);
			truth.push_back(t);
		}
	// Borders as the mod draws them: on every edge whose neighbor has a different owner.
	for(size_t i = 0; i < hexes.size(); ++i)
	{
		if(!hexes[i].owned) continue;
		int x = hexes[i].x, y = hexes[i].y, odd = y & 1;
		int nx[6] = { x + odd, x + 1, x + odd, x - 1 + odd, x - 1, x - 1 + odd };
		int ny[6] = { y + 1, y, y - 1, y - 1, y, y + 1 };
		for(int k = 0; k < 6; ++k)
		{
			bool same = false;
			for(size_t j = 0; j < hexes.size(); ++j)
				if(hexes[j].x == nx[k] && hexes[j].y == ny[k]) { same = !hexes[j].unexplored && hexes[j].owned && hexes[j].borderColor == hexes[i].borderColor; break; }
			hexes[i].border[k] = !same;
		}
	}
	std::vector<ClaudeMapLegendEntry> legend;
	ClaudeMapLegendEntry e;
	e.color = PERSIA; e.name = "Persia (you)"; legend.push_back(e);
	e.color = FRANCE; e.name = "France (teammate)"; legend.push_back(e);
	e.color = IROQUOIS; e.name = "The Iroquois (at war)"; legend.push_back(e);
	e.color = Rgb(120, 200, 120); e.name = "Milan"; legend.push_back(e);
	std::string err;
	std::string png = ClaudeDrawHexMapPng(hexes, legend, "center 51,34  radius 5  hatched = out of sight now  ? = unexplored", cx - radius - 1, cy + radius, 2 * radius + 3, 2 * radius + 1, err);
	if(png.empty()) { printf("failed: %s\n", err.c_str()); return 1; }
	FILE* f = NULL;
	fopen_s(&f, "map_example.png", "wb"); fwrite(png.data(), 1, png.size(), f); fclose(f);
	fopen_s(&f, "map_truth.json", "w");
	fprintf(f, "[\n");
	for(size_t i = 0; i < truth.size(); ++i)
	{
		const Truth& t = truth[i];
		fprintf(f, "{\"x\":%d,\"y\":%d,\"unexplored\":%s,\"fogged\":%s,\"terrain\":\"%s\",\"hills\":%s,\"mountain\":%s,\"feature\":\"%s\",\"resource\":\"%s\",\"improvement\":\"%s\",\"owner\":\"%s\",\"city\":\"%s\",\"road\":%s,\"river\":%s,\"units\":[",
			t.x, t.y, t.unexplored ? "true" : "false", t.fogged ? "true" : "false", t.terrain.c_str(), t.hills ? "true" : "false", t.mountain ? "true" : "false",
			t.feature.c_str(), t.resource.c_str(), t.improvement.c_str(), t.owner.c_str(), t.city.c_str(), t.road ? "true" : "false", t.river ? "true" : "false");
		for(size_t k = 0; k < t.units.size(); ++k) fprintf(f, "%s\"%s\"", k ? "," : "", t.units[k].c_str());
		fprintf(f, "]}%s\n", i + 1 < truth.size() ? "," : "");
	}
	fprintf(f, "]\n");
	fclose(f);
	printf("wrote map_example.png (%u bytes) and map_truth.json (%u hexes)\n", (unsigned)png.size(), (unsigned)truth.size());
	return 0;
}
