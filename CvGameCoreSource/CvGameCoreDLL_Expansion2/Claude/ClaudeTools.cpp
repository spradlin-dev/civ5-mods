#include "CvGameCoreDLLPCH.h"
#include "ClaudeTools.h"
#include "ClaudeToolSupport.h"
#include "ClaudeToolHandlers.h"
#include "ClaudeBridge.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvTeam.h"
#include "../CvCity.h"
#include "../CvUnit.h"
#include "../CvPlot.h"
#include "../CvMap.h"
#include "../CvGlobals.h"
#include "../CvGameCoreUtils.h"
#include "../CvTypes.h"
#include "../CvTechClasses.h"
#include "../CvUnitClasses.h"
#include "../CvBuildingClasses.h"
#include "../CvTreasury.h"
#include "../CvInfos.h"

using namespace ClaudeTool;

// ------------------------------------------------------------------ queries

CLAUDE_TOOL(ToolStatus)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	CvGame& g = GC.getGame();
	JObj o;
	o["turn"] = J(g.getGameTurn());
	o["year"] = J(g.getGameTurnYear());
	o["max_turns"] = J(g.getMaxTurns());
	o["player_id"] = J((int)me->GetID());
	o["player_name"] = J(me->getName());
	o["civilization"] = J(me->getCivilizationShortDescription());
	CvLeaderHeadInfo* pLeader = GC.getLeaderHeadInfo(me->getLeaderType());
	o["leader"] = J(pLeader ? pLeader->GetDescription() : "");
	o["is_my_turn"] = J(ClaudeIsPlayerTurn(*me));
	o["ai_processing"] = J(AiProcessing());
	o["end_turn_blocking"] = J(BlockingName(EndTurnBlocking(me)));
	o["units_needing_orders"] = J(me->GetCountReadyUnits());
	return JVal(o);
}

CLAUDE_TOOL(ToolOverview)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	JObj o;
	o["turn"] = J(GC.getGame().getGameTurn());
	o["era"] = J(EraName(me->GetCurrentEra()));
	o["score"] = J(me->GetScore());
	o["gold"] = J(me->GetTreasury()->GetGold());
	o["gold_per_turn"] = J(me->calculateGoldRate());
	o["science_per_turn"] = J(me->GetScience());
	o["culture"] = J(me->getJONSCulture());
	o["culture_per_turn"] = J(me->GetTotalJONSCulturePerTurn());
	o["next_policy_cost"] = J(me->getNextPolicyCost());
	o["faith"] = J(me->GetFaith());
	o["faith_per_turn"] = J(me->GetTotalFaithPerTurn());
	o["happiness"] = J(me->GetExcessHappiness());
	o["techs_known"] = J(GET_TEAM(me->getTeam()).GetTeamTechs()->GetNumTechsKnown());
	o["research"] = ResearchJson(me);
	o["num_cities"] = J(me->getNumCities());
	o["num_units"] = J(me->getNumUnits());
	o["military_units"] = J(me->getNumMilitaryUnits());
	o["units_supported_free"] = J(me->GetNumUnitsSupplied());
	o["end_turn_blocking"] = J(BlockingName(EndTurnBlocking(me)));
	o["cities"] = JVal(CitiesJson(me));
	o["units"] = JVal(UnitsJson(me));
	return JVal(o);
}

CLAUDE_TOOL(ToolUnits)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	JObj o;
	o["units"] = JVal(UnitsJson(me));
	return JVal(o);
}

namespace
{
	JArr AttackTargets(CvPlayer* me, CvUnit* u)
	{
		JArr targets;
		if(u->GetBaseCombatStrength() <= 0) return targets;
		int range = u->isRanged() ? u->GetRange() : 1;
		for(int dy = -range; dy <= range; ++dy)
		{
			for(int dx = -range; dx <= range; ++dx)
			{
				CvPlot* p = plotXYWithRangeCheck(u->getX(), u->getY(), dx, dy, range);
				if(!p) continue;
				AttackTarget t;
				std::string why;
				if(!ResolveAttackTarget(me, u, p, t, why)) continue;
				JObj o;
				o["x"] = J(p->getX());
				o["y"] = J(p->getY());
				if(t.city)
				{
					o["city"] = J(t.city->getName().c_str());
					o["owner"] = J(CivName(t.city->getOwner()));
					o["hp"] = J(t.city->GetMaxHitPoints() - t.city->getDamage());
				}
				else
				{
					o["unit"] = J(UnitTypeName(t.unit->getUnitType()));
					o["owner"] = J(CivName(t.unit->getOwner()));
					o["hp"] = J(t.unit->GetCurrHitPoints());
					o["strength"] = J(t.unit->GetBaseCombatStrength());
				}
				targets.push_back(JVal(o));
			}
		}
		return targets;
	}
}

CLAUDE_TOOL(ToolUnitOptions)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	int iUnit = 0;
	if(!RequireInt(args, "unit_id", iUnit, err)) return JVal();
	CvUnit* u = me->getUnit(iUnit);
	if(!u) { err = "no unit with that id"; return JVal(); }
	CvPlot* p = u->plot();
	JObj o;
	o["unit"] = UnitJson(u);
	JArr actions;
	size_t count = 0;
	const UnitActionDef* defs = UnitActions(count);
	for(size_t i = 0; i < count; ++i)
		if(defs[i].can(u, p)) actions.push_back(J(defs[i].name));
	o["actions"] = JVal(actions);

	JArr builds;
	for(int i = 0; i < GC.getNumBuildInfos(); ++i)
	{
		BuildTypes eBuild = (BuildTypes)i;
		if(u->canBuild(p, eBuild))
		{
			JObj b;
			b["type"] = J(GC.getBuildInfo(eBuild)->GetType());
			b["name"] = J(BuildName(eBuild));
			builds.push_back(JVal(b));
		}
	}
	o["builds_here"] = JVal(builds);

	JArr promotions = PromotionsAvailable(u);
	if(!promotions.empty()) o["promotions_available"] = JVal(promotions);
	JArr targets = AttackTargets(me, u);
	if(!targets.empty()) o["attack_targets"] = JVal(targets);

	JArr adjacent;
	for(int d = 0; d < NUM_DIRECTION_TYPES; ++d)
	{
		CvPlot* n = plotDirection(u->getX(), u->getY(), (DirectionTypes)d);
		if(!n || !n->isRevealed(me->getTeam())) continue;
		JObj a;
		a["x"] = J(n->getX());
		a["y"] = J(n->getY());
		a["terrain"] = J(TerrainName(n->getTerrainType()));
		if(n->isHills()) a["hills"] = J(true);
		if(n->getFeatureType() != NO_FEATURE) a["feature"] = J(FeatureName(n->getFeatureType()));
		a["can_move_there"] = J(u->canMoveInto(*n));
		adjacent.push_back(JVal(a));
	}
	o["adjacent_tiles"] = JVal(adjacent);
	return JVal(o);
}

CLAUDE_TOOL(ToolCities)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	JObj o;
	o["cities"] = JVal(CitiesJson(me));
	return JVal(o);
}

CLAUDE_TOOL(ToolCityOptions)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	int iCity = 0;
	if(!RequireInt(args, "city_id", iCity, err)) return JVal();
	CvCity* c = me->getCity(iCity);
	if(!c) { err = "no city with that id"; return JVal(); }
	JObj o;
	o["city"] = CityJson(c);
	o["gold"] = J(me->GetTreasury()->GetGold());
	JArr units;
	for(int i = 0; i < GC.getNumUnitInfos(); ++i)
	{
		UnitTypes e = (UnitTypes)i;
		bool bTrain = c->canTrain(e);
		bool bBuy = c->IsCanPurchase(false, true, e, NO_BUILDING, NO_PROJECT, YIELD_GOLD);
		if(!bTrain && !bBuy) continue;
		CvUnitEntry* info = GC.getUnitInfo(e);
		JObj u;
		u["type"] = J(info->GetType());
		u["name"] = J(info->GetDescription());
		if(bTrain)
		{
			u["cost"] = J(c->getProductionNeeded(e));
			u["turns"] = J(c->getProductionTurnsLeft(e, 0));
		}
		if(bBuy) u["buy_gold"] = J(c->GetPurchaseCost(e));
		if(info->GetCombat() > 0) u["strength"] = J(info->GetCombat());
		if(info->GetRangedCombat() > 0) u["ranged_strength"] = J(info->GetRangedCombat());
		u["moves"] = J(info->GetMoves());
		units.push_back(JVal(u));
	}
	o["units"] = JVal(units);
	JArr buildings;
	for(int i = 0; i < GC.getNumBuildingInfos(); ++i)
	{
		BuildingTypes e = (BuildingTypes)i;
		bool bBuild = c->canConstruct(e);
		bool bBuy = c->IsCanPurchase(false, true, NO_UNIT, e, NO_PROJECT, YIELD_GOLD);
		if(!bBuild && !bBuy) continue;
		CvBuildingEntry* info = GC.getBuildingInfo(e);
		JObj b;
		b["type"] = J(info->GetType());
		b["name"] = J(info->GetDescription());
		if(bBuild)
		{
			b["cost"] = J(c->getProductionNeeded(e));
			b["turns"] = J(c->getProductionTurnsLeft(e, 0));
		}
		if(bBuy) b["buy_gold"] = J(c->GetPurchaseCost(e));
		buildings.push_back(JVal(b));
	}
	o["buildings"] = JVal(buildings);

	JArr tiles;
	for(int dy = -3; dy <= 3; ++dy)
	{
		for(int dx = -3; dx <= 3; ++dx)
		{
			CvPlot* p = plotXYWithRangeCheck(c->getX(), c->getY(), dx, dy, 3);
			if(!p || !c->CanBuyPlot(p->getX(), p->getY())) continue;
			JObj t;
			t["x"] = J(p->getX());
			t["y"] = J(p->getY());
			t["gold"] = J(c->GetBuyPlotCost(p->getX(), p->getY()));
			t["terrain"] = J(TerrainName(p->getTerrainType()));
			if(p->getFeatureType() != NO_FEATURE) t["feature"] = J(FeatureName(p->getFeatureType()));
			ResourceTypes eRes = p->getResourceType(me->getTeam());
			if(eRes != NO_RESOURCE) t["resource"] = J(ResourceName(eRes));
			tiles.push_back(JVal(t));
		}
	}
	o["buyable_tiles"] = JVal(tiles);
	return JVal(o);
}

CLAUDE_TOOL(ToolMap)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	int cx = 0, cy = 0;
	bool bHasX = HasArg(args, "x"), bHasY = HasArg(args, "y");
	if(bHasX != bHasY) { err = "give both x and y, or neither to center on your capital"; return JVal(); }
	if(bHasX)
	{
		if(!RequireInt(args, "x", cx, err) || !RequireInt(args, "y", cy, err)) return JVal();
	}
	else
	{
		CvCity* cap = me->getCapitalCity();
		int iLoop = 0;
		CvUnit* u = cap ? NULL : me->firstUnit(&iLoop);
		if(cap) { cx = cap->getX(); cy = cap->getY(); }
		else if(u) { cx = u->getX(); cy = u->getY(); }
		else { err = "give x and y: you have no capital or units to center on"; return JVal(); }
	}
	int radius = ArgInt(args, "radius", 3);
	if(radius < 1) radius = 1;
	if(radius > 8) radius = 8;
	CvMap& map = GC.getMap();
	if(cx < 0 || cy < 0 || cx >= map.getGridWidth() || cy >= map.getGridHeight()) { err = "center is off the map"; return JVal(); }

	JObj o;
	o["center_x"] = J(cx);
	o["center_y"] = J(cy);
	o["radius"] = J(radius);
	o["map_width"] = J(map.getGridWidth());
	o["map_height"] = J(map.getGridHeight());
	o["wraps_east_west"] = J(map.isWrapX());
	JArr tiles;
	int unknown = 0;
	for(int dy = -radius; dy <= radius; ++dy)
	{
		for(int dx = -radius; dx <= radius; ++dx)
		{
			CvPlot* p = plotXYWithRangeCheck(cx, cy, dx, dy, radius);
			if(!p) continue;
			if(!p->isRevealed(me->getTeam())) { ++unknown; continue; }
			tiles.push_back(PlotJson(p, me->getTeam(), me->GetID(), cx, cy));
		}
	}
	o["tiles"] = JVal(tiles);
	o["unexplored_tiles_in_range"] = J(unknown);
	return JVal(o);
}

CLAUDE_TOOL(ToolResearchOptions)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	JObj o;
	o["research"] = ResearchJson(me);
	JArr techs;
	for(int i = 0; i < GC.getNumTechInfos(); ++i)
	{
		TechTypes e = (TechTypes)i;
		if(!me->GetPlayerTechs()->CanResearch(e)) continue;
		CvTechEntry* info = GC.getTechInfo(e);
		JObj t;
		t["type"] = J(info->GetType());
		t["name"] = J(info->GetDescription());
		t["era"] = J(EraName((EraTypes)info->GetEra()));
		t["cost"] = J(GET_TEAM(me->getTeam()).GetTeamTechs()->GetResearchCost(e));
		t["turns"] = J(me->GetPlayerTechs()->GetResearchTurnsLeft(e, true));
		t["help"] = J(Localized(info->GetHelp()));
		techs.push_back(JVal(t));
	}
	o["available"] = JVal(techs);
	return JVal(o);
}

// ------------------------------------------------------------------ actions

CLAUDE_TOOL(ToolMoveUnit)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	int iUnit = 0, x = 0, y = 0;
	if(!RequireInt(args, "unit_id", iUnit, err) || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
	CvUnit* u = me->getUnit(iUnit);
	if(!u) { err = "no unit with that id"; return JVal(); }
	CvMap& map = GC.getMap();
	if(x < 0 || y < 0 || x >= map.getGridWidth() || y >= map.getGridHeight()) { err = "destination is off the map"; return JVal(); }
	CvPlot* dest = map.plot(x, y);
	if(!dest->isRevealed(me->getTeam())) { err = "destination tile is unexplored"; return JVal(); }
	if(dest->isMountain()) { err = "destination is a mountain: impassable"; return JVal(); }
	if(dest->isVisible(me->getTeam()))
	{
		CvCity* c = dest->getPlotCity();
		if((c && IsEnemyOf(me, c->getOwner())) || dest->isVisibleEnemyUnit(u))
		{
			err = "an enemy is on that tile; use the attack tool instead of move_unit";
			return JVal();
		}
	}
	if(!u->canMove()) { err = "unit has no moves left this turn; the order can be given next turn"; return JVal(); }
	int iTurns = 0;
	if(!u->GeneratePath(dest, 0, false, &iTurns))
	{
		err = "no path to that tile";
		if(dest->isWater() && u->getDomainType() == DOMAIN_LAND) err += " (water: this unit cannot embark yet or no sea route)";
		return JVal();
	}
	int fromX = u->getX(), fromY = u->getY();
	{
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_MOVE_TO(), x, y, 0, false, true);
	}
	JObj o;
	o["unit_id"] = J(u->GetID());
	o["from"] = J(ClaudeFormat("(%d,%d)", fromX, fromY));
	o["now_at"] = J(ClaudeFormat("(%d,%d)", u->getX(), u->getY()));
	o["destination"] = J(ClaudeFormat("(%d,%d)", x, y));
	bool bArrived = (u->getX() == x && u->getY() == y);
	o["arrived"] = J(bArrived);
	if(!bArrived) o["note"] = J("the unit continues toward the destination when you end the turn, so it is still here when your next turn opens; do not re-issue the order");
	o["estimated_turns_total"] = J(iTurns);
	o["moves_left"] = J(Moves(u->movesLeft()));
	return JVal(o);
}

CLAUDE_TOOL(ToolUnitAction)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	int iUnit = 0;
	if(!RequireInt(args, "unit_id", iUnit, err)) return JVal();
	CvUnit* u = me->getUnit(iUnit);
	if(!u) { err = "no unit with that id"; return JVal(); }
	CvPlot* p = u->plot();
	std::string action = ArgStr(args, "action");
	JObj o;

	if(action == "build")
	{
		std::string key = ArgStr(args, "build");
		BuildTypes eBuild = FindBuildType(key);
		if(eBuild == NO_BUILD) { err = "unknown build type '" + key + "' (use unit_options to list builds_here)"; return JVal(); }
		if(!u->canBuild(p, eBuild)) { err = "cannot do that build on this tile (wrong unit, tech missing, or terrain)"; return JVal(); }
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_BUILD(), eBuild);
		o["ok"] = J(true);
		o["unit"] = UnitJson(u);
		return JVal(o);
	}

	size_t count = 0;
	const UnitActionDef* defs = UnitActions(count);
	for(size_t i = 0; i < count; ++i)
	{
		const UnitActionDef& def = defs[i];
		if(action != def.name) continue;
		if(!def.can(u, p)) { err = def.refusal; return JVal(); }
		{
			NetMessageScope scope;
			u->PushMission(def.mission());
		}
		o["ok"] = J(true);
		if(action == "found_city")
		{
			CvCity* c = p->getPlotCity();
			if(c) o["city_founded"] = J(c->getName().c_str());
		}
		else
		{
			o["unit"] = UnitJson(u);
		}
		return JVal(o);
	}

	err = "unknown action '" + action + "'; use one of " + UnitActionNames() + ", or build";
	return JVal();
}

CLAUDE_TOOL(ToolSetProduction)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	int iCity = 0;
	if(!RequireInt(args, "city_id", iCity, err)) return JVal();
	CvCity* c = me->getCity(iCity);
	if(!c) { err = "no city with that id"; return JVal(); }
	std::string item = ArgStr(args, "item");
	bool bAppend = ArgBool(args, "append", false);

	UnitTypes eUnit = FindUnitType(item);
	BuildingTypes eBuilding = (eUnit == NO_UNIT) ? FindBuildingType(item) : NO_BUILDING;
	if(eUnit != NO_UNIT)
	{
		if(!c->canTrain(eUnit)) { err = "this city cannot train " + item + " right now"; return JVal(); }
		c->pushOrder(ORDER_TRAIN, eUnit, -1, false, !bAppend, bAppend);
	}
	else if(eBuilding != NO_BUILDING)
	{
		if(!c->canConstruct(eBuilding)) { err = "this city cannot build " + item + " right now"; return JVal(); }
		c->pushOrder(ORDER_CONSTRUCT, eBuilding, -1, false, !bAppend, bAppend);
	}
	else
	{
		err = "unknown item '" + item + "'; use a type from city_options such as UNIT_WARRIOR or BUILDING_MONUMENT";
		return JVal();
	}
	JObj o;
	o["ok"] = J(true);
	o["city"] = CityJson(c);
	return JVal(o);
}

CLAUDE_TOOL(ToolSetResearch)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	std::string key = ArgStr(args, "tech");
	TechTypes eTech = FindTechType(key);
	if(eTech == NO_TECH) { err = "unknown tech '" + key + "'; use a type from research_options such as TECH_POTTERY"; return JVal(); }
	JObj o;
	if(me->GetNumFreeTechs() > 0)
	{
		if(!me->GetPlayerTechs()->CanResearchForFree(eTech)) { err = "that technology cannot be taken as the free one (prerequisites missing or already known)"; return JVal(); }
		if(me->GetPlayerTechs()->GetCurrentResearch() == eTech) me->clearResearchQueue();
		GET_TEAM(me->getTeam()).setHasTech(eTech, true, me->GetID(), true, true);
		me->SetNumFreeTechs(me->GetNumFreeTechs() - 1);
		o["free_tech_claimed"] = J(TechName(eTech));
	}
	else
	{
		if(!me->GetPlayerTechs()->CanResearch(eTech)) { err = "cannot research " + key + " now (prerequisites missing or already known)"; return JVal(); }
		me->clearResearchQueue();
		me->pushResearch(eTech, true);
	}
	o["ok"] = J(true);
	o["research"] = ResearchJson(me);
	return JVal(o);
}

CLAUDE_TOOL(ToolEndTurn)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvGame& g = GC.getGame();
	bool bForce = ArgBool(args, "force", false);
	EndTurnBlockingTypes eBlock = EndTurnBlocking(me);
	bool bIdleUnits = (eBlock == ENDTURN_BLOCKING_UNITS);
	if(eBlock != NO_ENDTURN_BLOCKING_TYPE && !(bIdleUnits && bForce))
	{
		err = "end of turn blocked - " + BlockingName(eBlock);
		return JVal();
	}
	if(AiProcessing())
	{
		err = "the game is still processing other civilizations; wait a moment and try again";
		return JVal();
	}
	if(bIdleUnits) me->EndTurnsForReadyUnits();
	if(!g.isNetworkMultiPlayer() && GC.GetPostTurnAutosaves())
		gDLL->AutoSave(false, true);
	gDLL->sendTurnComplete();
	JObj o;
	o["ok"] = J(true);
	o["ended_turn"] = J(g.getGameTurn());
	o["next"] = J("call wait_for_my_turn, then briefing; the turn comes back to you early if a unit finishes a queued move with moves to spare");
	return JVal(o);
}

CLAUDE_TOOL(ToolAcknowledgeMessages)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	std::vector<int> ids;
	JObj::const_iterator it = args.find("ids");
	if(it != args.end())
	{
		if(!it->second.is<JArr>() || it->second.get<JArr>().empty()) { err = "ids must be a non-empty array of message ids; omit it to acknowledge every unread message"; return JVal(); }
		const JArr& list = it->second.get<JArr>();
		for(size_t i = 0; i < list.size(); ++i)
		{
			int id = 0;
			if(!IsInt(list[i], id)) { err = ClaudeFormat("ids[%d] is not an integer message id", (int)i); return JVal(); }
			ids.push_back(id);
		}
	}
	int n = ClaudeBridge::Get().Acknowledge(ids.empty() ? NULL : &ids);
	JObj o;
	o["acknowledged"] = J(n);
	o["still_unread"] = J(ClaudeBridge::Get().CountUnread());
	return JVal(o);
}

CLAUDE_TOOL(ToolLog)
{
	std::string text = ArgStr(args, "text");
	if(text.empty()) { err = "text is required"; return JVal(); }
	ClaudeBridge::Get().LogLine(text);
	JObj o;
	o["ok"] = J(true);
	return JVal(o);
}

// ------------------------------------------------------------------ registry

namespace
{
	const char* SCHEMA_EMPTY = "{\"type\":\"object\",\"properties\":{}}";
	const char* SCHEMA_UNIT = "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"}},\"required\":[\"unit_id\"]}";
	const char* SCHEMA_CITY = "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"}},\"required\":[\"city_id\"]}";
	const char* SCHEMA_UNIT_XY = "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"unit_id\",\"x\",\"y\"]}";
	const char* SCHEMA_CITY_XY = "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"city_id\",\"x\",\"y\"]}";

	const ClaudeToolDef TOOLS[] =
	{
		{ "status",
		  "Current turn, who you are, whether it is your turn, and what blocks ending it. Cheap; call it freely.",
		  SCHEMA_EMPTY, ToolStatus, false, false },
		{ "wait_for_my_turn",
		  "Blocks until it is your turn (about half a minute at most; pass timeout_seconds to shorten), then returns status. Call it again if is_my_turn is still false.",
		  "{\"type\":\"object\",\"properties\":{\"timeout_seconds\":{\"type\":\"number\"}}}", ToolStatus, false, true },
		{ "briefing",
		  "Start every turn with this: what happened since your last turn, game messages you must acknowledge, decisions waiting for you, nearby enemies, economy warnings, and a diplomacy snapshot. Optional since_turn widens the event window.",
		  "{\"type\":\"object\",\"properties\":{\"since_turn\":{\"type\":\"integer\"}}}", ToolBriefing, false, false },
		{ "overview",
		  "Your empire at a glance: treasury, yields, happiness, research, every city and every unit with its activity.",
		  SCHEMA_EMPTY, ToolOverview, false, false },
		{ "units",
		  "All your units with position, moves left, health, strength and current activity. needs_orders=true means the unit is idle this turn.",
		  SCHEMA_EMPTY, ToolUnits, false, false },
		{ "unit_options",
		  "What one unit can do right now: actions for unit_action, tile improvements it can build here, promotions available, enemies it can attack, and which adjacent tiles it can enter.",
		  SCHEMA_UNIT, ToolUnitOptions, false, false },
		{ "cities",
		  "All your cities: population, growth, production, focus and per-turn yields.",
		  SCHEMA_EMPTY, ToolCities, false, false },
		{ "city_options",
		  "Everything one city can build or buy right now (units and buildings with production cost, turns and gold price) and the tiles it can buy. Use the type strings with set_production or buy.",
		  SCHEMA_CITY, ToolCityOptions, false, false },
		{ "map",
		  "Explored tiles within radius (default 3, max 8) of a point (default: your capital). Hex grid with offset rows: odd rows sit half a tile east of even rows, so use the dist field rather than computing distances. Unexplored tiles are counted, not listed. Only visible_now tiles show units.",
		  "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"radius\":{\"type\":\"integer\"}}}", ToolMap, false, false },
		{ "research_options",
		  "Technologies you can research now, with cost, turns and what they give.",
		  SCHEMA_EMPTY, ToolResearchOptions, false, false },
		{ "policy_options",
		  "Social policy branches and policies: what is unlocked, adoptable now, and what each does; plus your culture and the next policy cost.",
		  SCHEMA_EMPTY, ToolPolicyOptions, false, false },
		{ "civs",
		  "Every civilization and city-state you have met: war or peace, their attitude toward you, friendship levels, relative strength, and known capitals.",
		  SCHEMA_EMPTY, ToolCivs, false, false },
		{ "lookup",
		  "The game's own rules text for a type key: TECH_..., UNIT_..., BUILDING_..., POLICY_..., POLICY_BRANCH_..., PROMOTION_..., IMPROVEMENT_..., RESOURCE_... Includes stats and, for a tech, what it unlocks.",
		  "{\"type\":\"object\",\"properties\":{\"type\":{\"type\":\"string\"}},\"required\":[\"type\"]}", ToolLookup, false, false },
		{ "move_unit",
		  "Order a unit to move to a tile. Multi-turn moves continue when you end each turn (the unit is still in place when your next turn opens); do not re-issue them. Fails with a reason if there is no path. To attack, use attack.",
		  SCHEMA_UNIT_XY, ToolMoveUnit, true, false },
		{ "unit_action",
		  "Give a unit an order on its current tile. action is one of the names unit_options lists under actions, or build (with build=BUILD_... from builds_here).",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\"},\"build\":{\"type\":\"string\"}},\"required\":[\"unit_id\",\"action\"]}", ToolUnitAction, true, false },
		{ "attack",
		  "Attack the enemy unit or city on a tile with one of your units (melee needs to be adjacent; ranged units strike from range). Returns the game's own damage estimate; pass preview_only=true to see the estimate without attacking.",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"preview_only\":{\"type\":\"boolean\"}},\"required\":[\"unit_id\",\"x\",\"y\"]}", ToolAttack, true, false },
		{ "city_attack",
		  "Have a city bombard an enemy unit in range (cities with can_attack=true).",
		  SCHEMA_CITY_XY, ToolCityAttack, true, false },
		{ "promote_unit",
		  "Give a unit a promotion (type from unit_options.promotions_available).",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"promotion\":{\"type\":\"string\"}},\"required\":[\"unit_id\",\"promotion\"]}", ToolPromoteUnit, true, false },
		{ "upgrade_unit",
		  "Upgrade a unit for gold (units list upgrade_available_to and upgrade_cost). The unit keeps its position; its id changes.",
		  SCHEMA_UNIT, ToolUpgradeUnit, true, false },
		{ "set_production",
		  "Set what a city builds. item is a type string from city_options (UNIT_... or BUILDING_...). Replaces the current item unless append=true, which adds it to the end of the queue.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"item\":{\"type\":\"string\"},\"append\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"item\"]}", ToolSetProduction, true, false },
		{ "buy",
		  "Buy a unit or building in a city with gold (item type from city_options; the price is buy_gold).",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"item\":{\"type\":\"string\"}},\"required\":[\"city_id\",\"item\"]}", ToolBuy, true, false },
		{ "buy_tile",
		  "Buy a tile for a city with gold (tiles and prices from city_options.buyable_tiles).",
		  SCHEMA_CITY_XY, ToolBuyTile, true, false },
		{ "set_city_focus",
		  "Set how a city assigns its citizens (focus names as shown in cities.focus; an unknown name lists them). Optional avoid_growth=true/false.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"focus\":{\"type\":\"string\"},\"avoid_growth\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"focus\"]}", ToolSetCityFocus, true, false },
		{ "set_research",
		  "Choose the technology to research (type string from research_options, e.g. TECH_POTTERY). When you have a free technology to claim, this grants it immediately instead.",
		  "{\"type\":\"object\",\"properties\":{\"tech\":{\"type\":\"string\"}},\"required\":[\"tech\"]}", ToolSetResearch, true, false },
		{ "adopt_policy",
		  "Adopt a social policy (POLICY_...) or open a policy branch (POLICY_BRANCH_...) using your culture; see policy_options. Switching to a branch blocked by a rival branch costs a turn of anarchy and needs accept_anarchy=true.",
		  "{\"type\":\"object\",\"properties\":{\"policy\":{\"type\":\"string\"},\"accept_anarchy\":{\"type\":\"boolean\"}},\"required\":[\"policy\"]}", ToolAdoptPolicy, true, false },
		{ "declare_war",
		  "Declare war on a civilization or city-state (by name from civs). Irreversible for many turns; be sure.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"}},\"required\":[\"civ\"]}", ToolDeclareWar, true, false },
		{ "end_turn",
		  "End your turn, after reading the briefing and giving orders. Refused while a decision is pending (research, production, policy...). If units still await orders it is refused too, unless force=true, which makes them skip this turn.",
		  "{\"type\":\"object\",\"properties\":{\"force\":{\"type\":\"boolean\"}}}", ToolEndTurn, true, false },
		{ "acknowledge_messages",
		  "Confirm you have read game messages (briefing.game_messages entries with unread=true). Actions are refused while any are unread. ids lists specific message ids; omit it to acknowledge all. Informational dialogs close in the game once acknowledged.",
		  "{\"type\":\"object\",\"properties\":{\"ids\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}}}", ToolAcknowledgeMessages, true, false },
		{ "log",
		  "Write a line to the in-game log the human can read. Use it for your reasoning and plans each turn: what you see, what you decided and why.",
		  "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}", ToolLog, false, false },
	};
}

const ClaudeToolDef* ClaudeGetTools(size_t& count)
{
	count = sizeof(TOOLS) / sizeof(TOOLS[0]);
	return TOOLS;
}

std::string ClaudeCheckArgs(const ClaudeToolDef& tool, const picojson::object& args)
{
	picojson::value schema;
	if(!picojson::parse(schema, std::string(tool.inputSchema)).empty() || !schema.is<JObj>())
		return ClaudeFormat("the argument schema for %s is broken; this is a bug in the mod", tool.name);
	const JObj& s = schema.get<JObj>();
	JObj props;
	JObj::const_iterator pit = s.find("properties");
	if(pit != s.end() && pit->second.is<JObj>()) props = pit->second.get<JObj>();
	for(JObj::const_iterator it = args.begin(); it != args.end(); ++it)
	{
		if(props.find(it->first) != props.end()) continue;
		std::string known;
		for(JObj::const_iterator k = props.begin(); k != props.end(); ++k) known += (known.empty() ? "" : ", ") + k->first;
		return ClaudeFormat("unknown argument '%s' for %s; it accepts %s", it->first.c_str(), tool.name, known.empty() ? "no arguments" : known.c_str());
	}
	JObj::const_iterator rit = s.find("required");
	if(rit != s.end() && rit->second.is<JArr>())
	{
		const JArr& req = rit->second.get<JArr>();
		for(size_t i = 0; i < req.size(); ++i)
			if(req[i].is<std::string>() && args.find(req[i].get<std::string>()) == args.end())
				return ClaudeFormat("missing required argument '%s' for %s", req[i].get<std::string>().c_str(), tool.name);
	}
	return "";
}

const ClaudeToolDef* ClaudeFindTool(const std::string& name)
{
	for(size_t i = 0; i < sizeof(TOOLS) / sizeof(TOOLS[0]); ++i)
		if(name == TOOLS[i].name) return &TOOLS[i];
	return NULL;
}
