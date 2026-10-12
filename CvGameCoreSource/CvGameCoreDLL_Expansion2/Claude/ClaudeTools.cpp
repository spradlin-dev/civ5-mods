#include "CvGameCoreDLLPCH.h"
#include "ClaudeTools.h"
#include "ClaudeToolSupport.h"
#include "ClaudeSeat.h"
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
	o["alive"] = J(me->isAlive());
	o["civilization"] = J(me->getCivilizationShortDescription());
	CvLeaderHeadInfo* pLeader = GC.getLeaderHeadInfo(me->getLeaderType());
	o["leader"] = J(pLeader ? pLeader->GetDescription() : "");
	o["is_my_turn"] = J(ClaudeIsPlayerTurn(*me));
	o["ai_processing"] = J(AiProcessing());
	o["end_turn_blocking"] = J(BlockingName(EndTurnBlocking(me)));
	o["units_needing_orders"] = J(me->GetCountReadyUnits());
	o["seat"] = J(ClaudeSeatOnScreen() ? "on screen: the human player's own civilization" : "claimed: off screen, the human on screen plays another civilization");
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
	CvUnit* u = RequireUnit(me, args, err);
	if(!u) return JVal();
	CvPlot* p = u->plot();
	JObj o;
	o["unit"] = UnitJson(u);
	JArr actions, waiting;
	size_t count = 0;
	const UnitActionDef* defs = UnitActions(count);
	for(size_t i = 0; i < count; ++i)
	{
		if(!defs[i].can(u, p)) continue;
		if(defs[i].needsMoves && !u->canMove()) waiting.push_back(J(defs[i].name));
		else actions.push_back(J(defs[i].name));
	}
	o["actions"] = JVal(actions);
	if(!waiting.empty())
	{
		o["actions_needing_moves"] = JVal(waiting);
		o["moves_note"] = J("the unit has no moves left this turn, so the actions_needing_moves can be used next turn");
	}

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
	CvCity* c = RequireCity(me, args, err);
	if(!c) return JVal();
	JObj o;
	o["city"] = CityJson(c);
	o["gold"] = J(me->GetTreasury()->GetGold());
	JArr units;
	for(int i = 0; i < GC.getNumUnitInfos(); ++i)
	{
		UnitTypes e = (UnitTypes)i;
		bool bTrain = c->canTrain(e);
		bool bBuy = c->IsCanPurchase(false, true, e, NO_BUILDING, NO_PROJECT, YIELD_GOLD);
		bool bBuyFaith = c->IsCanPurchase(false, true, e, NO_BUILDING, NO_PROJECT, YIELD_FAITH);
		if(!bTrain && !bBuy && !bBuyFaith) continue;
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
		if(bBuyFaith) u["buy_faith"] = J(c->GetFaithPurchaseCost(e, true));
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
		bool bBuyFaith = c->IsCanPurchase(false, true, NO_UNIT, e, NO_PROJECT, YIELD_FAITH);
		if(!bBuild && !bBuy && !bBuyFaith) continue;
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
		if(bBuyFaith) b["buy_faith"] = J(c->GetFaithPurchaseCost(e));
		buildings.push_back(JVal(b));
	}
	o["buildings"] = JVal(buildings);
	JArr projects;
	for(int i = 0; i < GC.getNumProjectInfos(); ++i)
	{
		ProjectTypes e = (ProjectTypes)i;
		if(!c->canCreate(e)) continue;
		CvProjectEntry* info = GC.getProjectInfo(e);
		JObj pj;
		pj["type"] = J(info->GetType());
		pj["name"] = J(info->GetDescription());
		pj["cost"] = J(c->getProductionNeeded(e));
		projects.push_back(JVal(pj));
	}
	if(!projects.empty()) o["projects"] = JVal(projects);
	o["faith"] = J(me->GetFaith());

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
	int x = 0, y = 0;
	CvUnit* u = RequireUnit(me, args, err);
	if(!u || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
	CvMap& map = GC.getMap();
	if(x < 0 || y < 0 || x >= map.getGridWidth() || y >= map.getGridHeight()) { err = "destination is off the map"; return JVal(); }
	CvPlot* dest = map.plot(x, y);
	if(!dest->isRevealed(me->getTeam())) { err = "destination tile is unexplored"; return JVal(); }
	if(dest->isMountain()) { err = "destination is a mountain: impassable"; return JVal(); }
	if(dest->isVisible(me->getTeam()))
	{
		CvCity* c = dest->getPlotCity();
		if((c && IsEnemyOf(me, c->getOwner())) || dest->isVisibleEnemyDefender(u))
		{
			err = "a defending enemy is on that tile; use the attack tool instead of move_unit (a lone enemy civilian is captured by moving onto it)";
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
	CvUnit* u = RequireUnit(me, args, err);
	if(!u) return JVal();
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

	if(action == "rebase")
	{
		int x = 0, y = 0;
		if(!RequireMoves(u, err) || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
		if(!u->canRebaseAt(p, x, y)) { err = "this unit cannot rebase there (not an aircraft, out of range, or no city or carrier of yours at the tile)"; return JVal(); }
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_REBASE(), x, y);
		o["ok"] = J(true);
		o["unit"] = UnitJson(u);
		return JVal(o);
	}
	if(action == "air_sweep")
	{
		int x = 0, y = 0;
		if(!RequireMoves(u, err) || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
		if(!u->canAirSweepAt(x, y)) { err = "this unit cannot sweep that tile (not a fighter, out of range, or it already attacked this turn)"; return JVal(); }
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_AIR_SWEEP(), x, y);
		o["ok"] = J(true);
		o["unit"] = UnitJson(u);
		return JVal(o);
	}
	if(action == "paradrop")
	{
		int x = 0, y = 0;
		if(!RequireMoves(u, err) || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
		if(!u->canParadropAt(p, x, y)) { err = "this unit cannot drop there (not a paratrooper, must start in your territory, target within range and not enemy-held)"; return JVal(); }
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_PARADROP(), x, y);
		o["ok"] = J(true);
		o["unit"] = UnitJson(u);
		return JVal(o);
	}
	if(action == "nuke")
	{
		int x = 0, y = 0;
		if(!RequireMoves(u, err) || !RequireInt(args, "x", x, err) || !RequireInt(args, "y", y, err)) return JVal();
		if(!u->canNukeAt(p, x, y)) { err = "this unit cannot strike that tile (not a nuclear weapon, out of range, or the blast would hit someone you cannot declare war on)"; return JVal(); }
		CvPlot* pTarget = GC.getMap().plot(x, y);
		if(!pTarget) { err = "that tile is off the map"; return JVal(); }
		CvTeam& myTeam = GET_TEAM(me->getTeam());
		for(int i = 0; i < MAX_TEAMS; ++i)
		{
			TeamTypes eTeam = (TeamTypes)i;
			if(u->isNukeVictim(pTarget, eTeam) && !myTeam.isAtWar(eTeam))
			{
				err = "the blast would hit " + CivName(GET_TEAM(eTeam).getLeaderID()) + ", who you are not at war with; declare war first if you mean to";
				return JVal();
			}
		}
		NetMessageScope scope;
		u->PushMission(CvTypes::getMISSION_NUKE(), x, y);
		o["ok"] = J(true);
		o["unit"] = UnitJson(u);
		return JVal(o);
	}
	if(action == "disband")
	{
		if(!u->canScrap()) { err = "this unit cannot be disbanded right now"; return JVal(); }
		std::string name = UnitTypeName(u->getUnitType());
		u->scrap();
		o["ok"] = J(true);
		o["disbanded"] = J(name);
		return JVal(o);
	}

	size_t count = 0;
	const UnitActionDef* defs = UnitActions(count);
	for(size_t i = 0; i < count; ++i)
	{
		const UnitActionDef& def = defs[i];
		if(action != def.name) continue;
		if(!def.can(u, p)) { err = def.refusal; return JVal(); }
		if(def.needsMoves && !RequireMoves(u, err)) return JVal();
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

	err = "unknown action '" + action + "'; use one of " + UnitActionNames() + ", build, rebase, air_sweep, paradrop, nuke or disband";
	return JVal();
}

CLAUDE_TOOL(ToolSetProduction)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvCity* c = RequireCity(me, args, err);
	if(!c) return JVal();
	std::string item = ArgStr(args, "item");
	bool bAppend = ArgBool(args, "append", false);

	CityItem found = FindCityItem(me, item);
	UnitTypes eUnit = found.eUnit;
	BuildingTypes eBuilding = found.eBuilding;
	ProjectTypes eProject = (eUnit == NO_UNIT && eBuilding == NO_BUILDING) ? FindProjectType(item) : NO_PROJECT;
	if(eUnit != NO_UNIT)
	{
		CvString why;
		if(!c->canTrain(eUnit, false, false, false, false, &why)) { err = "this city cannot train " + std::string(GC.getUnitInfo(eUnit)->GetType()) + " right now: " + std::string(why.c_str()); return JVal(); }
		c->pushOrder(ORDER_TRAIN, eUnit, -1, false, !bAppend, bAppend);
	}
	else if(eBuilding != NO_BUILDING)
	{
		CvString why;
		if(!c->canConstruct(eBuilding, false, false, false, &why)) { err = "this city cannot build " + std::string(GC.getBuildingInfo(eBuilding)->GetType()) + " right now: " + std::string(why.c_str()); return JVal(); }
		c->pushOrder(ORDER_CONSTRUCT, eBuilding, -1, false, !bAppend, bAppend);
	}
	else if(eProject != NO_PROJECT)
	{
		if(!c->canCreate(eProject)) { err = "this city cannot work on " + item + " right now"; return JVal(); }
		c->pushOrder(ORDER_CREATE, eProject, -1, false, !bAppend, bAppend);
	}
	else
	{
		err = "unknown item '" + item + "'; use a type from city_options such as UNIT_WARRIOR, BUILDING_MONUMENT or PROJECT_APOLLO_PROGRAM";
		return JVal();
	}
	JObj o;
	o["ok"] = J(true);
	if(!found.substituted.empty()) o["substituted"] = J(found.substituted);
	o["city"] = CityJson(c);
	return JVal(o);
}

CLAUDE_TOOL(ToolSetResearch)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	std::string key = ArgStr(args, "tech");
	TechTypes eTech = FindTechType(key);
	if(eTech == NO_TECH) { err = "unknown tech '" + key + "'; use a name or type from research_options"; return JVal(); }
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
	if(ClaudeSeatOnScreen() && AiProcessing())
	{
		err = "the game is still processing other civilizations; wait a moment and try again";
		return JVal();
	}
	if(bIdleUnits) me->EndTurnsForReadyUnits();
	if(ClaudeSeatOnScreen())
	{
		if(!g.isNetworkMultiPlayer() && GC.GetPostTurnAutosaves())
			gDLL->AutoSave(false, true);
		gDLL->sendTurnComplete();
	}
	else
	{
		me->setEndTurn(true);
		if(!me->isEndTurn())
		{
			if(me->hasReadyUnit()) { err = "a unit still needs orders; see units"; return JVal(); }
			err = "a unit or city stayed busy for several seconds; call end_turn again";
			ClaudeBridge::RetryLater();
			return JVal();
		}
	}
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

CLAUDE_TOOL(ToolTakeSeat)
{
	std::string key = ArgStr(args, "civ");
	PlayerTypes e = FindPlayer(key);
	if(e == NO_PLAYER) { err = "unknown civilization '" + key + "'; use a name from civs"; return JVal(); }
	if(!ClaudeClaimSeat(e, err)) return JVal();
	JObj o;
	o["ok"] = J(true);
	o["playing"] = J(CivName(e));
	o["next"] = J("call wait_for_my_turn; your turn comes after the human's each round");
	return JVal(o);
}

CLAUDE_TOOL(ToolReleaseSeat)
{
	if(!ClaudeReleaseSeat(err)) return JVal();
	JObj o;
	o["ok"] = J(true);
	return JVal(o);
}

CLAUDE_TOOL(ToolSkipHumanTurn)
{
	if(!ClaudeBridge::Get().TestingMode()) { err = "skip_human_turn is a testing tool: call testing_mode with enabled=true first"; return JVal(); }
	PlayerTypes e = GC.getGame().getActivePlayer();
	if(e == NO_PLAYER || !GET_PLAYER(e).isHuman()) { err = "no human player is on screen"; return JVal(); }
	if(ClaudeSeatOnScreen()) { err = "the player on screen is your own seat; use end_turn"; return JVal(); }
	CvPlayer& p = GET_PLAYER(e);
	if(!ClaudeIsPlayerTurn(p)) { err = "it is not the screen player's turn right now"; return JVal(); }
	p.EndTurnsForReadyUnits();
	gDLL->sendTurnComplete();
	JObj o;
	o["ok"] = J(true);
	o["skipped_turn_of"] = J(CivName(e));
	return JVal(o);
}

CLAUDE_TOOL(ToolTestingMode)
{
	bool bOn = ArgBool(args, "enabled", false);
	ClaudeBridge::Get().SetTestingMode(bOn);
	JObj o;
	o["ok"] = J(true);
	o["enabled"] = J(bOn);
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
		{ CLAUDE_TOOL_STATUS,
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
		  "Everything one city can build or buy right now: units, buildings and projects with production cost and turns, gold and faith prices, and the tiles it can buy. Use the type strings with set_production or buy.",
		  SCHEMA_CITY, ToolCityOptions, false, false },
		{ "map",
		  "Explored tiles within radius (default 3, max 8) of a point (default: your capital). Hex grid with offset rows: odd rows sit half a tile east of even rows, so use the dist field rather than computing distances. Unexplored tiles are counted, not listed. Only visible_now tiles show units. Compass: y increases NORTHWARD (y=0 is the south edge) and x increases eastward.",
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
		  "Give a unit an order on its current tile. action is one of the names unit_options lists under actions, build (with build=BUILD_... from builds_here), rebase (aircraft, with x and y of one of your cities or carriers), air_sweep (fighters, x and y of the tile to clear of interceptors), paradrop (paratroopers, x and y of the landing tile), nuke (atomic bombs and nuclear missiles, x and y of the target), or disband. Aircraft bomb with the attack tool.",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\"},\"build\":{\"type\":\"string\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"unit_id\",\"action\"]}", ToolUnitAction, true, false },
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
		  "Buy a unit or building in a city with gold (item type from city_options; the price is buy_gold), or with faith when with_faith=true (price buy_faith).",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"item\":{\"type\":\"string\"},\"with_faith\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"item\"]}", ToolBuy, true, false },
		{ "buy_tile",
		  "Buy a tile for a city with gold (tiles and prices from city_options.buyable_tiles).",
		  SCHEMA_CITY_XY, ToolBuyTile, true, false },
		{ "set_city_focus",
		  "Set how a city assigns its citizens (focus names as shown in cities.focus; an unknown name lists them). Optional avoid_growth=true/false.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"focus\":{\"type\":\"string\"},\"avoid_growth\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"focus\"]}", ToolSetCityFocus, true, false },
		{ "city_citizens",
		  "What every citizen of a city does: worked tiles (locked ones are your choice), tiles it could work instead, specialists per building, and unemployed citizens.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"}},\"required\":[\"city_id\"]}", ToolCityCitizens, false, false },
		{ "city_work_tile",
		  "Lock a tile so the city works it, or free a locked or worked tile (the citizen becomes unemployed until reassigned), exactly like clicking the tile in the city screen. A tile another of your cities works is moved to this city first. The city's own tile resets every tile and specialist to automatic.",
		  SCHEMA_CITY_XY, ToolCityWorkTile, true, false },
		{ "city_specialist",
		  "Put a citizen to work as a specialist in a building (type from city_citizens.specialists), or take one out with remove=true.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"building\":{\"type\":\"string\"},\"remove\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"building\"]}", ToolCitySpecialist, true, false },
		{ "set_research",
		  "Choose the technology to research (type string from research_options, e.g. TECH_POTTERY). When you have a free technology to claim, this grants it immediately instead.",
		  "{\"type\":\"object\",\"properties\":{\"tech\":{\"type\":\"string\"}},\"required\":[\"tech\"]}", ToolSetResearch, true, false },
		{ "adopt_policy",
		  "Adopt a social policy (POLICY_...) or open a policy branch (POLICY_BRANCH_...) using your culture; see policy_options. Switching to a branch blocked by a rival branch costs a turn of anarchy and needs accept_anarchy=true.",
		  "{\"type\":\"object\",\"properties\":{\"policy\":{\"type\":\"string\"},\"accept_anarchy\":{\"type\":\"boolean\"}},\"required\":[\"policy\"]}", ToolAdoptPolicy, true, false },
		{ "declare_war",
		  "Declare war on a civilization or city-state (by name from civs). Irreversible for many turns; be sure.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"}},\"required\":[\"civ\"]}", ToolDeclareWar, true, false },
		{ CLAUDE_TOOL_END_TURN,
		  "End your turn, after reading the briefing and giving orders. Refused while a decision is pending (research, production, policy...). If units still await orders it is refused too, unless force=true, which makes them skip this turn.",
		  "{\"type\":\"object\",\"properties\":{\"force\":{\"type\":\"boolean\"}}}", ToolEndTurn, true, false },
		{ "acknowledge_messages",
		  "Confirm you have read game messages (briefing.game_messages entries with unread=true). Actions are refused while any are unread. ids lists specific message ids; omit it to acknowledge all. Informational dialogs close in the game once acknowledged.",
		  "{\"type\":\"object\",\"properties\":{\"ids\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}}}", ToolAcknowledgeMessages, true, false },
		{ "diplomacy",
		  "Relations with every civilization you have met (war, embassies, open borders, pacts, friendship), the deals in force, and requests from other leaders waiting for your answer. Requests are kept in memory only: they do not survive a game reload.",
		  SCHEMA_EMPTY, ToolDiplomacy, false, false },
		{ "respond_to_request",
		  "Answer a request from another leader (id and choices from diplomacy.requests or briefing.diplomacy_requests). Deal offers take accept or decline; warnings take the listed choices; statements take acknowledge.",
		  "{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\"},\"choice\":{\"type\":\"string\"}},\"required\":[\"id\",\"choice\"]}", ToolRespondToRequest, true, false },
		{ "propose_deal",
		  "Offer a deal to a civilization. give and get are lists of items: {item: gold|gold_per_turn|resource|open_borders|embassy|defensive_pact|research_agreement|maps|declaration_of_friendship|city|third_party_war|third_party_peace, amount, resource, city_id, civ}. With preview_only=true you learn whether the game's AI would accept and what would make it work, without proposing. A human decides on their own trade screen.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"give\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}},\"get\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}},\"preview_only\":{\"type\":\"boolean\"}},\"required\":[\"civ\"]}", ToolProposeDeal, true, false },
		{ "make_peace",
		  "Negotiate peace with a civilization you are at war with. The game's AI sets the terms it will accept (white peace, or a surrender one way or the other); preview_only=true shows them without agreeing.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"preview_only\":{\"type\":\"boolean\"}},\"required\":[\"civ\"]}", ToolMakePeace, true, false },
		{ "answer_deal",
		  "Accept or decline a deal a human proposed to you (listed in diplomacy.requests as human_deal_offer): civ names them, accept is true or false. withdraw=true instead takes back an offer of yours they have not answered.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"accept\":{\"type\":\"boolean\"},\"withdraw\":{\"type\":\"boolean\"}},\"required\":[\"civ\"]}", ToolAnswerDeal, true, false },
		{ "demand",
		  "Demand tribute from an AI civilization: get lists the items (as in propose_deal). They give in only if they fear you; refusals sour relations.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"get\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}}},\"required\":[\"civ\",\"get\"]}", ToolDemand, true, false },
		{ "discuss",
		  "Talk to a leader: topic is ask_friendship, end_friendship, denounce, stop_settling, stop_spying, stop_converting, stop_digging or joint_war (with target: the civilization to fight).",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"topic\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"}},\"required\":[\"civ\",\"topic\"]}", ToolDiscuss, true, false },
		{ "religion_options",
		  "Your faith, pantheon and religion, and every religious choice waiting for you: pantheon beliefs, founding or enhancing a religion, reformation beliefs, and great people to claim (faith, free or Maya).",
		  SCHEMA_EMPTY, ToolReligionOptions, false, false },
		{ "found_pantheon", "Found your pantheon with a belief from religion_options.",
		  "{\"type\":\"object\",\"properties\":{\"belief\":{\"type\":\"string\"}},\"required\":[\"belief\"]}", ToolFoundPantheon, true, false },
		{ "found_religion", "Found a religion once a Great Prophet has started it (unit_action found_religion): religion, beliefs [founder, follower] and optionally name and city_id for the holy city.",
		  "{\"type\":\"object\",\"properties\":{\"religion\":{\"type\":\"string\"},\"beliefs\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"name\":{\"type\":\"string\"},\"city_id\":{\"type\":\"integer\"}},\"required\":[\"religion\",\"beliefs\"]}", ToolFoundReligion, true, false },
		{ "enhance_religion", "Enhance your religion with beliefs [follower, enhancer] after a Great Prophet started it (unit_action enhance_religion).",
		  "{\"type\":\"object\",\"properties\":{\"beliefs\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},\"required\":[\"beliefs\"]}", ToolEnhanceReligion, true, false },
		{ "add_reformation_belief", "Add the reformation belief your religion has earned.",
		  "{\"type\":\"object\",\"properties\":{\"belief\":{\"type\":\"string\"}},\"required\":[\"belief\"]}", ToolAddReformationBelief, true, false },
		{ "choose_great_person", "Claim a great person you are owed: unit is a great person type from religion_options, source is faith, free or maya.",
		  "{\"type\":\"object\",\"properties\":{\"unit\":{\"type\":\"string\"},\"source\":{\"type\":\"string\"}},\"required\":[\"unit\",\"source\"]}", ToolChooseGreatPerson, true, false },
		{ "congress", "The World Congress: session timing, your votes and proposals, resolutions in force, proposals awaiting votes with their choices, and what you can propose.",
		  SCHEMA_EMPTY, ToolCongress, false, false },
		{ "propose_resolution", "Propose a resolution (resolution type from congress with a choice where one is needed) or a repeal (repeal_id).",
		  "{\"type\":\"object\",\"properties\":{\"resolution\":{\"type\":\"string\"},\"choice\":{},\"repeal_id\":{\"type\":\"integer\"}}}", ToolProposeResolution, true, false },
		{ "vote_resolution", "Cast votes on a proposal awaiting votes: resolution_id, choice (number or text from congress) and votes (default: all remaining).",
		  "{\"type\":\"object\",\"properties\":{\"resolution_id\":{\"type\":\"integer\"},\"choice\":{},\"votes\":{\"type\":\"integer\"}},\"required\":[\"resolution_id\"]}", ToolVoteResolution, true, false },
		{ "abstain", "Abstain with some or all of your remaining votes.",
		  "{\"type\":\"object\",\"properties\":{\"votes\":{\"type\":\"integer\"}}}", ToolAbstain, true, false },
		{ "spies", "Your spies: where they are, what they are doing, coup chances, and technologies waiting to be stolen.",
		  SCHEMA_EMPTY, ToolSpies, false, false },
		{ "move_spy", "Send a spy (spy_index) to a city named by x,y or by civ and city, optionally as_diplomat; or withdraw=true to pull it out.",
		  "{\"type\":\"object\",\"properties\":{\"spy_index\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"civ\":{\"type\":\"string\"},\"city\":{\"type\":\"string\"},\"as_diplomat\":{\"type\":\"boolean\"},\"withdraw\":{\"type\":\"boolean\"}},\"required\":[\"spy_index\"]}", ToolMoveSpy, true, false },
		{ "stage_coup", "Attempt a coup in the city-state where the spy is; the spy may die.",
		  "{\"type\":\"object\",\"properties\":{\"spy_index\":{\"type\":\"integer\"}},\"required\":[\"spy_index\"]}", ToolStageCoup, true, false },
		{ "steal_tech", "Take a stolen technology: civ it was stolen from and tech from spies.techs_to_steal.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"tech\":{\"type\":\"string\"}},\"required\":[\"civ\",\"tech\"]}", ToolStealTech, true, false },
		{ "city_state", "City-states you have met (or one, with civ): influence, status, quests for you, and the actions open to you.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"}}}", ToolCityState, false, false },
		{ "city_state_action", "Act toward a city-state: gift_gold (amount), gift_unit (unit_id), bully_gold, bully_worker, pledge_protection, revoke_protection, buyout, make_peace.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"},\"action\":{\"type\":\"string\"},\"amount\":{\"type\":\"integer\"},\"unit_id\":{\"type\":\"integer\"}},\"required\":[\"civ\",\"action\"]}", ToolCityStateAction, true, false },
		{ "choose_ideology", "Pick your ideology (Freedom, Order or Autocracy) when the game allows it; without ideology it lists them and whether you can choose now.",
		  "{\"type\":\"object\",\"properties\":{\"ideology\":{\"type\":\"string\"}}}", ToolChooseIdeology, true, false },
		{ "switch_ideology", "Give in to public opinion and switch to the ideology your people prefer (costs turns of anarchy).",
		  SCHEMA_EMPTY, ToolSwitchIdeology, true, false },
		{ "archaeology", "Completed digs: without choice it shows the dig and the options; with choice it decides (artifact, artifact_for_other_civ, landmark, culture_boost, written_artifact, do_nothing).",
		  "{\"type\":\"object\",\"properties\":{\"choice\":{\"type\":\"string\"}}}", ToolArchaeology, true, false },
		{ "great_works", "Your great works by city and building slot, and your tourism.",
		  SCHEMA_EMPTY, ToolGreatWorks, false, false },
		{ "move_great_work", "Move a great work between slots (swapping if the target is full): from_city_id, from_building, from_slot, to_city_id, to_building, to_slot.",
		  "{\"type\":\"object\",\"properties\":{\"from_city_id\":{\"type\":\"integer\"},\"from_building\":{\"type\":\"string\"},\"from_slot\":{\"type\":\"integer\"},\"to_city_id\":{\"type\":\"integer\"},\"to_building\":{\"type\":\"string\"},\"to_slot\":{\"type\":\"integer\"}},\"required\":[\"from_city_id\",\"from_building\",\"from_slot\",\"to_city_id\",\"to_building\",\"to_slot\"]}", ToolMoveGreatWork, true, false },
		{ "trade_route_options", "Where a caravan or cargo ship (unit_id, inside one of your cities) can open a trade route, and of which type.",
		  SCHEMA_UNIT, ToolTradeRouteOptions, false, false },
		{ "establish_trade_route", "Send a caravan or cargo ship (unit_id) to the city at x,y with type international, food or production.",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"type\":{\"type\":\"string\"}},\"required\":[\"unit_id\",\"x\",\"y\",\"type\"]}", ToolEstablishTradeRoute, true, false },
		{ "launch_spaceship", "Launch the completed spaceship for a science victory.",
		  SCHEMA_EMPTY, ToolLaunchSpaceship, true, false },
		{ CLAUDE_TOOL_ADVANCE_ROUND,
		  "TESTING ONLY (needs testing_mode enabled). Ends your turn, ends the on-screen human's turn when it opens, and waits for your next turn (about a minute at most). Returns status plus round_advanced / turn_reopened.",
		  "{\"type\":\"object\",\"properties\":{\"force\":{\"type\":\"boolean\"}}}", NULL, true, false },
		{ "city_task",
		  "Decide a city's fate or status: annex, puppet, raze, stop_razing or liberate (give a captured city back to its original owner).",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"task\":{\"type\":\"string\"}},\"required\":[\"city_id\",\"task\"]}", ToolCityTask, true, false },
		{ "take_seat",
		  "Play a computer-controlled civilization in a shared game: it becomes a human seat named Claude at the human's difficulty, and the game's AI stops acting for it. The human on screen tells you which civilization; claim it during their turn. Until a seat is claimed every tool acts on the human's own civilization.",
		  "{\"type\":\"object\",\"properties\":{\"civ\":{\"type\":\"string\"}},\"required\":[\"civ\"]}", ToolTakeSeat, true, false },
		{ "release_seat",
		  "Hand the claimed civilization back to the game's AI.",
		  SCHEMA_EMPTY, ToolReleaseSeat, true, false },
		{ CLAUDE_TOOL_SKIP_HUMAN_TURN,
		  "TESTING ONLY. Ends the turn of the human on screen as if they clicked End Turn, so a test game can run with nobody at the keyboard. Never call it in a real shared game: it ends the human's turn for them.",
		  SCHEMA_EMPTY, ToolSkipHumanTurn, true, false },
		{ "testing_mode",
		  "TESTING ONLY. enabled=true unlocks skip_human_turn and advance_round and closes the idle human's own popups and leader screens unread, so an unattended test game does not stall. Off by default and reset every new game. Never enable it in a real shared game.",
		  "{\"type\":\"object\",\"properties\":{\"enabled\":{\"type\":\"boolean\"}}}", ToolTestingMode, true, false },
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
