#include "CvGameCoreDLLPCH.h"
#include <stdarg.h>
#include "ClaudeTools.h"
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
#include "../CvImprovementClasses.h"
#include "../CvTreasury.h"
#include "../CvInfos.h"

std::string ClaudeFormat(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	return buf;
}

bool ClaudeIsPlayerTurn(const CvPlayer& kPlayer)
{
	return kPlayer.isHuman() && kPlayer.isAlive() && kPlayer.isTurnActive() && !kPlayer.isEndTurn() && !gDLL->HasSentTurnComplete();
}

namespace
{
	typedef picojson::object JObj;
	typedef picojson::array JArr;
	typedef picojson::value JVal;

	JVal J(int v) { return JVal((double)v); }
	JVal J(double v) { return JVal(v); }
	JVal J(bool v) { return JVal(v); }
	JVal J(const char* s) { return JVal(std::string(s ? s : "")); }
	JVal J(const std::string& s) { return JVal(s); }

	bool ReadInt(const JObj& a, const char* k, int& out)
	{
		JObj::const_iterator it = a.find(k);
		if(it == a.end() || !it->second.is<double>()) return false;
		double d = it->second.get<double>();
		if(d < -2147483648.0 || d > 2147483647.0) return false;
		out = (int)d;
		return true;
	}
	int ArgInt(const JObj& a, const char* k, int def)
	{
		int v = def;
		return ReadInt(a, k, v) ? v : def;
	}
	bool RequireInt(const JObj& a, const char* k, int& out, std::string& err)
	{
		if(ReadInt(a, k, out)) return true;
		err = std::string("missing or invalid integer argument '") + k + "'";
		return false;
	}
	bool ArgBool(const JObj& a, const char* k, bool def)
	{
		JObj::const_iterator it = a.find(k);
		return (it != a.end() && it->second.is<bool>()) ? it->second.get<bool>() : def;
	}
	std::string ArgStr(const JObj& a, const char* k)
	{
		JObj::const_iterator it = a.find(k);
		return (it != a.end() && it->second.is<std::string>()) ? it->second.get<std::string>() : std::string();
	}
	bool HasArg(const JObj& a, const char* k) { return a.find(k) != a.end(); }

	CvPlayer* Me(std::string& err)
	{
		PlayerTypes e = GC.getGame().getActivePlayer();
		if(e == NO_PLAYER) { err = "no active player"; return NULL; }
		CvPlayer& p = GET_PLAYER(e);
		if(!p.isHuman()) { err = "the active player is not a human slot"; return NULL; }
		return &p;
	}

	// For tools that change game state: orders are only accepted while the turn is open.
	CvPlayer* MeActing(std::string& err)
	{
		CvPlayer* me = Me(err);
		if(me && !ClaudeIsPlayerTurn(*me))
		{
			err = "it is not your turn right now (the turn is ending or other civilizations are moving); call wait_for_my_turn";
			return NULL;
		}
		return me;
	}

	// The game flags unit orders for human players that arrive outside its network
	// message path; its own handlers wrap such calls exactly like this.
	struct NetMessageScope
	{
		NetMessageScope() { CvUnit::dispatchingNetMessage(true); }
		~NetMessageScope() { CvUnit::dispatchingNetMessage(false); }
	};

	double Moves(int iMoves) { return iMoves / (double)GC.getMOVE_DENOMINATOR(); }

	const char* TerrainName(TerrainTypes e) { CvTerrainInfo* i = (e != NO_TERRAIN) ? GC.getTerrainInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* FeatureName(FeatureTypes e) { CvFeatureInfo* i = (e != NO_FEATURE) ? GC.getFeatureInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* ResourceName(ResourceTypes e) { CvResourceInfo* i = (e != NO_RESOURCE) ? GC.getResourceInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* ImprovementName(ImprovementTypes e) { CvImprovementEntry* i = (e != NO_IMPROVEMENT) ? GC.getImprovementInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* RouteName(RouteTypes e) { CvRouteInfo* i = (e != NO_ROUTE) ? GC.getRouteInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* UnitTypeName(UnitTypes e) { CvUnitEntry* i = (e != NO_UNIT) ? GC.getUnitInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* UnitTypeKey(UnitTypes e) { CvUnitEntry* i = (e != NO_UNIT) ? GC.getUnitInfo(e) : NULL; return i ? i->GetType() : ""; }
	const char* BuildingName(BuildingTypes e) { CvBuildingEntry* i = (e != NO_BUILDING) ? GC.getBuildingInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* TechName(TechTypes e) { CvTechEntry* i = (e != NO_TECH) ? GC.getTechInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* BuildName(BuildTypes e) { CvBuildInfo* i = (e != NO_BUILD) ? GC.getBuildInfo(e) : NULL; return i ? i->GetDescription() : ""; }
	const char* EraName(EraTypes e) { CvEraInfo* i = (e != NO_ERA) ? GC.getEraInfo(e) : NULL; return i ? i->GetDescription() : ""; }

	std::string CivName(PlayerTypes e)
	{
		if(e == NO_PLAYER) return "";
		CvPlayer& p = GET_PLAYER(e);
		if(p.isBarbarian()) return "Barbarians";
		return p.getCivilizationShortDescription();
	}

	UnitTypes FindUnitType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumUnitInfos(); ++i)
		{
			CvUnitEntry* e = GC.getUnitInfo((UnitTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (UnitTypes)i;
		}
		return NO_UNIT;
	}
	BuildingTypes FindBuildingType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumBuildingInfos(); ++i)
		{
			CvBuildingEntry* e = GC.getBuildingInfo((BuildingTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (BuildingTypes)i;
		}
		return NO_BUILDING;
	}
	TechTypes FindTechType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumTechInfos(); ++i)
		{
			CvTechEntry* e = GC.getTechInfo((TechTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (TechTypes)i;
		}
		return NO_TECH;
	}
	BuildTypes FindBuildType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumBuildInfos(); ++i)
		{
			CvBuildInfo* e = GC.getBuildInfo((BuildTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (BuildTypes)i;
		}
		return NO_BUILD;
	}

	std::string BlockingName(EndTurnBlockingTypes e)
	{
		switch(e)
		{
		case NO_ENDTURN_BLOCKING_TYPE: return "none";
		case ENDTURN_BLOCKING_POLICY: return "policy: you have enough culture to adopt a social policy";
		case ENDTURN_BLOCKING_RESEARCH: return "research: choose a technology to research";
		case ENDTURN_BLOCKING_PRODUCTION: return "production: a city needs a production choice";
		case ENDTURN_BLOCKING_UNITS: return "units: a unit still awaits orders (give it an order, or end_turn with force=true to let idle units skip this turn)";
		case ENDTURN_BLOCKING_UNIT_PROMOTION: return "unit_promotion: a unit can be promoted";
		case ENDTURN_BLOCKING_DIPLO_VOTE: return "diplo_vote: a World Congress vote is pending";
		case ENDTURN_BLOCKING_MINOR_QUEST: return "minor_quest: a city-state quest needs a response";
		case ENDTURN_BLOCKING_FREE_TECH: return "free_tech: choose a free technology";
		case ENDTURN_BLOCKING_STACKED_UNITS: return "stacked_units: two units share a tile";
		case ENDTURN_BLOCKING_CITY_RANGE_ATTACK: return "city_range_attack: a city can attack";
		case ENDTURN_BLOCKING_FREE_POLICY: return "free_policy: choose a free policy";
		case ENDTURN_BLOCKING_FREE_ITEMS: return "free_items: choose free items";
		case ENDTURN_BLOCKING_FOUND_PANTHEON: return "found_pantheon: choose a pantheon belief";
		case ENDTURN_BLOCKING_FOUND_RELIGION: return "found_religion: found a religion";
		case ENDTURN_BLOCKING_ENHANCE_RELIGION: return "enhance_religion: enhance your religion";
		case ENDTURN_BLOCKING_STEAL_TECH: return "steal_tech: choose a technology to steal";
		case ENDTURN_BLOCKING_MAYA_LONG_COUNT: return "maya_long_count: choose a great person";
		case ENDTURN_BLOCKING_FAITH_GREAT_PERSON: return "faith_great_person: choose a great person";
		case ENDTURN_BLOCKING_ADD_REFORMATION_BELIEF: return "reformation_belief: choose a reformation belief";
		case ENDTURN_BLOCKING_LEAGUE_CALL_FOR_PROPOSALS: return "league_proposals: the World Congress wants proposals";
		case ENDTURN_BLOCKING_CHOOSE_ARCHAEOLOGY: return "archaeology: choose what to do with an artifact";
		case ENDTURN_BLOCKING_LEAGUE_CALL_FOR_VOTES: return "league_votes: the World Congress wants votes";
		case ENDTURN_BLOCKING_CHOOSE_IDEOLOGY: return "ideology: choose an ideology";
		default: return ClaudeFormat("other (blocking type %d)", (int)e);
		}
	}

	// ------------------------------------------------------------ unit actions

	bool CanFound(CvUnit* u, CvPlot* p) { return u->canFound(p); }
	bool CanFortify(CvUnit* u, CvPlot* p) { return u->canFortify(p); }
	bool CanSleep(CvUnit* u, CvPlot* p) { return u->canSleep(p); }
	bool CanHold(CvUnit* u, CvPlot* p) { return u->canHold(p); }
	bool CanSentry(CvUnit* u, CvPlot* p) { return u->canSentry(p); }
	bool CanHeal(CvUnit* u, CvPlot* p) { return u->canHeal(p); }
	bool CanPillage(CvUnit* u, CvPlot* p) { return u->canPillage(p); }

	struct UnitActionDef
	{
		const char* name;
		bool (*can)(CvUnit*, CvPlot*);
		const MissionTypes (*mission)();
		const char* refusal;
	};

	const UnitActionDef UNIT_ACTIONS[] =
	{
		{ "found_city", CanFound,   CvTypes::getMISSION_FOUND,   "cannot found a city here (not a settler, too close to another city, or bad terrain)" },
		{ "fortify",    CanFortify, CvTypes::getMISSION_FORTIFY, "unit cannot fortify (non-combat units sleep instead)" },
		{ "sleep",      CanSleep,   CvTypes::getMISSION_SLEEP,   "unit cannot sleep here" },
		{ "skip",       CanHold,    CvTypes::getMISSION_SKIP,    "unit cannot skip its turn" },
		{ "alert",      CanSentry,  CvTypes::getMISSION_ALERT,   "unit cannot go on alert here" },
		{ "heal",       CanHeal,    CvTypes::getMISSION_HEAL,    "unit cannot heal here (full health, or healing not allowed on this tile)" },
		{ "pillage",    CanPillage, CvTypes::getMISSION_PILLAGE, "nothing to pillage here or unit cannot pillage" },
	};
	const size_t NUM_UNIT_ACTIONS = sizeof(UNIT_ACTIONS) / sizeof(UNIT_ACTIONS[0]);

	std::string UnitActionNames()
	{
		std::string s;
		for(size_t i = 0; i < NUM_UNIT_ACTIONS; ++i)
		{
			if(i) s += ", ";
			s += UNIT_ACTIONS[i].name;
		}
		return s;
	}

	// ------------------------------------------------------------ JSON views

	std::string UnitActivity(CvUnit* u)
	{
		if(u->GetLengthMissionQueue() > 0)
		{
			const MissionData* m = u->GetHeadMissionData();
			if(m)
			{
				if(m->eMissionType == CvTypes::getMISSION_MOVE_TO()) return ClaudeFormat("moving to (%d,%d); continues when the turn ends", m->iData1, m->iData2);
				if(m->eMissionType == CvTypes::getMISSION_ROUTE_TO()) return ClaudeFormat("building a route to (%d,%d)", m->iData1, m->iData2);
				if(m->eMissionType == CvTypes::getMISSION_BUILD()) return ClaudeFormat("working: %s", BuildName((BuildTypes)m->iData1));
			}
			return "on a mission";
		}
		switch(u->GetActivityType())
		{
		case ACTIVITY_SLEEP: return "sleeping";
		case ACTIVITY_HOLD: return "skipped this turn";
		case ACTIVITY_HEAL: return "healing";
		case ACTIVITY_SENTRY: return "on alert";
		case ACTIVITY_MISSION: return "on a mission";
		default: break;
		}
		if(u->getFortifyTurns() > 0) return "fortified";
		return u->ReadyToMove() ? "awaiting orders" : "done for this turn";
	}

	JVal UnitJson(CvUnit* u)
	{
		JObj o;
		o["id"] = J(u->GetID());
		o["type"] = J(UnitTypeKey(u->getUnitType()));
		o["name"] = J(u->getName().c_str());
		o["x"] = J(u->getX());
		o["y"] = J(u->getY());
		o["moves_left"] = J(Moves(u->movesLeft()));
		o["max_moves"] = J(Moves(u->maxMoves()));
		o["hp"] = J(u->GetCurrHitPoints());
		o["max_hp"] = J(u->GetMaxHitPoints());
		int iStrength = u->GetBaseCombatStrength();
		o["strength"] = J(iStrength);
		if(u->isRanged())
		{
			o["ranged_strength"] = J(u->GetBaseRangedCombatStrength());
			o["range"] = J(u->GetRange());
		}
		o["is_combat"] = J(iStrength > 0);
		o["is_worker"] = J(u->IsWork());
		o["can_found_city_here"] = J(u->canFound(u->plot()));
		o["embarked"] = J(u->isEmbarked());
		o["level"] = J(u->getLevel());
		o["xp"] = J(u->getExperience());
		o["activity"] = J(UnitActivity(u));
		o["needs_orders"] = J(u->ReadyToMove());
		return JVal(o);
	}

	std::string OrderName(const OrderData* od)
	{
		if(!od) return "";
		switch(od->eOrderType)
		{
		case ORDER_TRAIN: return UnitTypeName((UnitTypes)od->iData1);
		case ORDER_CONSTRUCT: return BuildingName((BuildingTypes)od->iData1);
		case ORDER_CREATE: return "project";
		case ORDER_MAINTAIN: return "process";
		default: return "other";
		}
	}

	JVal CityJson(CvCity* c)
	{
		JObj o;
		o["id"] = J(c->GetID());
		o["name"] = J(c->getName().c_str());
		o["x"] = J(c->getX());
		o["y"] = J(c->getY());
		o["is_capital"] = J(c->isCapital());
		o["population"] = J(c->getPopulation());
		o["food_surplus_per_turn"] = J(c->foodDifference());
		o["turns_to_grow"] = J(c->getFoodTurnsLeft());

		JObj prod;
		if(c->getOrderQueueLength() == 0)
		{
			prod["name"] = J("nothing - choose with set_production");
			prod["progress"] = J(c->getProduction());
		}
		else
		{
			prod["name"] = J(c->getProductionName());
			prod["turns_left"] = J(c->getProductionTurnsLeft());
			prod["progress"] = J(c->getProduction());
			prod["cost"] = J(c->getProductionNeeded());
		}
		o["production"] = JVal(prod);

		JArr queue;
		for(int i = 1; i < c->getOrderQueueLength(); ++i)
			queue.push_back(J(OrderName(c->getOrderFromQueue(i))));
		o["production_queue_after_current"] = JVal(queue);

		JObj y;
		y["food"] = J(c->getYieldRate(YIELD_FOOD, false));
		y["production"] = J(c->getYieldRate(YIELD_PRODUCTION, false));
		y["gold"] = J(c->getYieldRate(YIELD_GOLD, false));
		y["science"] = J(c->getYieldRate(YIELD_SCIENCE, false));
		y["culture"] = J(c->getJONSCulturePerTurn());
		y["faith"] = J(c->GetFaithPerTurn());
		o["yields_per_turn"] = JVal(y);

		o["hp"] = J(c->GetMaxHitPoints() - c->getDamage());
		o["max_hp"] = J(c->GetMaxHitPoints());
		o["defense_strength"] = J(c->getStrengthValue() / 100);
		return JVal(o);
	}

	JVal PlotJson(CvPlot* p, TeamTypes eTeam, PlayerTypes eMe, int cx, int cy)
	{
		JObj o;
		o["x"] = J(p->getX());
		o["y"] = J(p->getY());
		o["dist"] = J(plotDistance(cx, cy, p->getX(), p->getY()));
		bool bVisible = p->isVisible(eTeam);
		o["visible_now"] = J(bVisible);
		o["terrain"] = J(TerrainName(p->getTerrainType()));
		if(p->isHills()) o["hills"] = J(true);
		if(p->isMountain()) o["mountain"] = J(true);
		if(p->isWater()) o["water"] = J(true);
		if(p->isLake()) o["lake"] = J(true);
		if(!p->isWater() && p->isCoastalLand()) o["coastal"] = J(true);
		if(p->isRiver()) o["river"] = J(true);
		if(p->getFeatureType() != NO_FEATURE) o["feature"] = J(FeatureName(p->getFeatureType()));
		if(p->IsNaturalWonder()) o["natural_wonder"] = J(true);
		ResourceTypes eRes = p->getResourceType(eTeam);
		if(eRes != NO_RESOURCE)
		{
			o["resource"] = J(ResourceName(eRes));
			if(p->getNumResource() > 1) o["resource_amount"] = J(p->getNumResource());
		}
		ImprovementTypes eImp = p->getRevealedImprovementType(eTeam);
		if(eImp != NO_IMPROVEMENT)
		{
			o["improvement"] = J(ImprovementName(eImp));
			if(p->IsImprovementPillaged()) o["pillaged"] = J(true);
		}
		RouteTypes eRoute = p->getRevealedRouteType(eTeam);
		if(eRoute != NO_ROUTE) o["route"] = J(RouteName(eRoute));
		PlayerTypes eOwner = p->getRevealedOwner(eTeam);
		if(eOwner != NO_PLAYER) o["owner"] = J(CivName(eOwner));
		if(p->isCity())
		{
			CvCity* c = p->getPlotCity();
			if(c)
			{
				o["city"] = J(c->getName().c_str());
				o["city_owner"] = J(CivName(c->getOwner()));
			}
		}
		if(p->isGoody(eTeam)) o["ancient_ruins"] = J(true);

		JObj y;
		y["food"] = J(p->calculateYield(YIELD_FOOD));
		y["production"] = J(p->calculateYield(YIELD_PRODUCTION));
		y["gold"] = J(p->calculateYield(YIELD_GOLD));
		o["yields"] = JVal(y);

		if(bVisible && p->getNumUnits() > 0)
		{
			JArr units;
			for(int i = 0; i < p->getNumUnits(); ++i)
			{
				CvUnit* u = p->getUnitByIndex(i);
				if(!u) continue;
				JObj uo;
				uo["owner"] = J(CivName(u->getOwner()));
				uo["type"] = J(UnitTypeName(u->getUnitType()));
				uo["hp"] = J(u->GetCurrHitPoints());
				if(u->getOwner() == eMe) uo["id"] = J(u->GetID());
				units.push_back(JVal(uo));
			}
			o["units"] = JVal(units);
		}
		return JVal(o);
	}

	JVal ResearchJson(CvPlayer* me)
	{
		JObj r;
		TechTypes eTech = me->GetPlayerTechs()->GetCurrentResearch();
		if(eTech == NO_TECH)
		{
			r["current"] = J("none - choose one with set_research");
			return JVal(r);
		}
		CvTeamTechs* pTeamTechs = GET_TEAM(me->getTeam()).GetTeamTechs();
		r["current"] = J(TechName(eTech));
		r["type"] = J(GC.getTechInfo(eTech)->GetType());
		r["progress"] = J(pTeamTechs->GetResearchProgress(eTech));
		r["cost"] = J(pTeamTechs->GetResearchCost(eTech));
		r["turns_left"] = J(me->GetPlayerTechs()->GetResearchTurnsLeft(eTech, true));
		return JVal(r);
	}

	JArr CitiesJson(CvPlayer* me)
	{
		JArr cities;
		int iLoop = 0;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
			cities.push_back(CityJson(c));
		return cities;
	}

	JArr UnitsJson(CvPlayer* me)
	{
		JArr units;
		int iLoop = 0;
		for(CvUnit* u = me->firstUnit(&iLoop); u; u = me->nextUnit(&iLoop))
			units.push_back(UnitJson(u));
		return units;
	}

	// ------------------------------------------------------------------ tools

	JVal ToolStatus(const JObj& args, std::string& err)
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
		o["ai_processing"] = J(!(gDLL->allAICivsProcessedThisTurn() && g.allUnitAIProcessed()));
		o["end_turn_blocking"] = J(BlockingName(me->GetEndTurnBlockingType()));
		o["units_needing_orders"] = J(me->GetCountReadyUnits());
		return JVal(o);
	}

	JVal ToolOverview(const JObj& args, std::string& err)
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
		o["end_turn_blocking"] = J(BlockingName(me->GetEndTurnBlockingType()));
		o["cities"] = JVal(CitiesJson(me));
		o["units"] = JVal(UnitsJson(me));
		return JVal(o);
	}

	JVal ToolUnits(const JObj& args, std::string& err)
	{
		CvPlayer* me = Me(err);
		if(!me) return JVal();
		JObj o;
		o["units"] = JVal(UnitsJson(me));
		return JVal(o);
	}

	JVal ToolUnitOptions(const JObj& args, std::string& err)
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
		for(size_t i = 0; i < NUM_UNIT_ACTIONS; ++i)
			if(UNIT_ACTIONS[i].can(u, p)) actions.push_back(J(UNIT_ACTIONS[i].name));
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

		if(u->CanUpgradeRightNow(false))
			o["upgrade_available_to"] = J(UnitTypeName(u->GetUpgradeUnitType()));

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

	JVal ToolCities(const JObj& args, std::string& err)
	{
		CvPlayer* me = Me(err);
		if(!me) return JVal();
		JObj o;
		o["cities"] = JVal(CitiesJson(me));
		return JVal(o);
	}

	JVal ToolCityOptions(const JObj& args, std::string& err)
	{
		CvPlayer* me = Me(err);
		if(!me) return JVal();
		int iCity = 0;
		if(!RequireInt(args, "city_id", iCity, err)) return JVal();
		CvCity* c = me->getCity(iCity);
		if(!c) { err = "no city with that id"; return JVal(); }
		JObj o;
		o["city"] = CityJson(c);
		JArr units;
		for(int i = 0; i < GC.getNumUnitInfos(); ++i)
		{
			UnitTypes e = (UnitTypes)i;
			if(!c->canTrain(e)) continue;
			CvUnitEntry* info = GC.getUnitInfo(e);
			JObj u;
			u["type"] = J(info->GetType());
			u["name"] = J(info->GetDescription());
			u["cost"] = J(c->getProductionNeeded(e));
			u["turns"] = J(c->getProductionTurnsLeft(e, 0));
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
			if(!c->canConstruct(e)) continue;
			CvBuildingEntry* info = GC.getBuildingInfo(e);
			JObj b;
			b["type"] = J(info->GetType());
			b["name"] = J(info->GetDescription());
			b["cost"] = J(c->getProductionNeeded(e));
			b["turns"] = J(c->getProductionTurnsLeft(e, 0));
			buildings.push_back(JVal(b));
		}
		o["buildings"] = JVal(buildings);
		return JVal(o);
	}

	JVal ToolMap(const JObj& args, std::string& err)
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

	JVal ToolResearchOptions(const JObj& args, std::string& err)
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
			if(info->GetHelp() && *info->GetHelp()) t["help"] = J(GetLocalizedText(info->GetHelp()).c_str());
			techs.push_back(JVal(t));
		}
		o["available"] = JVal(techs);
		return JVal(o);
	}

	JVal ToolMoveUnit(const JObj& args, std::string& err)
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
		if(!u->canMove()) { err = "unit has no moves left this turn; the order can be given next turn"; return JVal(); }
		int iTurns = 0;
		if(!u->GeneratePath(dest, 0, false, &iTurns))
		{
			err = "no path to that tile";
			if(dest->isWater() && u->getDomainType() == DOMAIN_LAND) err += " (water: this unit cannot embark yet or no sea route)";
			if(dest->isVisibleEnemyUnit(u)) err += " (an enemy unit is there; attacking is not supported yet)";
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
		if(!bArrived) o["note"] = J("the unit continues toward the destination when you end the turn; do not re-issue the order");
		o["estimated_turns_total"] = J(iTurns);
		o["moves_left"] = J(Moves(u->movesLeft()));
		return JVal(o);
	}

	JVal ToolUnitAction(const JObj& args, std::string& err)
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

		for(size_t i = 0; i < NUM_UNIT_ACTIONS; ++i)
		{
			const UnitActionDef& def = UNIT_ACTIONS[i];
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

	JVal ToolSetProduction(const JObj& args, std::string& err)
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

	JVal ToolSetResearch(const JObj& args, std::string& err)
	{
		CvPlayer* me = MeActing(err);
		if(!me) return JVal();
		std::string key = ArgStr(args, "tech");
		TechTypes eTech = FindTechType(key);
		if(eTech == NO_TECH) { err = "unknown tech '" + key + "'; use a type from research_options such as TECH_POTTERY"; return JVal(); }
		if(!me->GetPlayerTechs()->CanResearch(eTech)) { err = "cannot research " + key + " now (prerequisites missing or already known)"; return JVal(); }
		me->clearResearchQueue();
		me->pushResearch(eTech, true);
		JObj o;
		o["ok"] = J(true);
		o["research"] = ResearchJson(me);
		return JVal(o);
	}

	JVal ToolEndTurn(const JObj& args, std::string& err)
	{
		CvPlayer* me = MeActing(err);
		if(!me) return JVal();
		CvGame& g = GC.getGame();
		bool bForce = ArgBool(args, "force", false);
		EndTurnBlockingTypes eBlock = me->GetEndTurnBlockingType();
		bool bIdleUnits = (eBlock == ENDTURN_BLOCKING_UNITS);
		if(eBlock != NO_ENDTURN_BLOCKING_TYPE && !(bIdleUnits && bForce))
		{
			err = "end of turn blocked - " + BlockingName(eBlock);
			return JVal();
		}
		if(!(gDLL->allAICivsProcessedThisTurn() && g.allUnitAIProcessed()))
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
		o["next"] = J("call wait_for_my_turn; the turn comes back to you early if a unit finishes a queued move with moves to spare");
		return JVal(o);
	}

	JVal ToolLog(const JObj& args, std::string& err)
	{
		std::string text = ArgStr(args, "text");
		if(text.empty()) { err = "text is required"; return JVal(); }
		ClaudeBridge::Get().LogLine(text);
		JObj o;
		o["ok"] = J(true);
		return JVal(o);
	}

	const char* SCHEMA_EMPTY = "{\"type\":\"object\",\"properties\":{}}";

	const ClaudeToolDef TOOLS[] =
	{
		{ "status",
		  "Current turn, who you are, whether it is your turn, and what blocks ending it. Cheap; call it freely.",
		  SCHEMA_EMPTY, ToolStatus, false, false },
		{ "wait_for_my_turn",
		  "Blocks until it is your turn (about half a minute at most; pass timeout_seconds to shorten), then returns status. Call it again if is_my_turn is still false.",
		  "{\"type\":\"object\",\"properties\":{\"timeout_seconds\":{\"type\":\"number\"}}}", ToolStatus, false, true },
		{ "overview",
		  "Your empire at a glance: treasury, yields, happiness, research, every city and every unit with its activity. Start each turn with this.",
		  SCHEMA_EMPTY, ToolOverview, false, false },
		{ "units",
		  "All your units with position, moves left, health, strength and current activity. needs_orders=true means the unit is idle this turn.",
		  SCHEMA_EMPTY, ToolUnits, false, false },
		{ "unit_options",
		  "What one unit can do right now: actions for unit_action, tile improvements it can build here, upgrades, and which adjacent tiles it can enter.",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"}},\"required\":[\"unit_id\"]}", ToolUnitOptions, false, false },
		{ "cities",
		  "All your cities: population, growth, production and per-turn yields.",
		  SCHEMA_EMPTY, ToolCities, false, false },
		{ "city_options",
		  "Everything one city can build right now (units and buildings) with cost and turns. Use the type strings with set_production.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"}},\"required\":[\"city_id\"]}", ToolCityOptions, false, false },
		{ "map",
		  "Explored tiles within radius (default 3, max 8) of a point (default: your capital). Hex grid with offset rows: odd rows sit half a tile east of even rows, so use the dist field rather than computing distances. Unexplored tiles are counted, not listed. Only visible_now tiles show units.",
		  "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"radius\":{\"type\":\"integer\"}}}", ToolMap, false, false },
		{ "research_options",
		  "Technologies you can research now, with cost, turns and what they give.",
		  SCHEMA_EMPTY, ToolResearchOptions, false, false },
		{ "move_unit",
		  "Order a unit to move to a tile. Multi-turn moves continue automatically when you end each turn; do not re-issue them. Fails with a reason if there is no path.",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"unit_id\",\"x\",\"y\"]}", ToolMoveUnit, true, false },
		{ "unit_action",
		  "Give a unit an order on its current tile. action is one of the names unit_options lists under actions, or build (with build=BUILD_... from builds_here).",
		  "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\"},\"build\":{\"type\":\"string\"}},\"required\":[\"unit_id\",\"action\"]}", ToolUnitAction, true, false },
		{ "set_production",
		  "Set what a city builds. item is a type string from city_options (UNIT_... or BUILDING_...). Replaces the current item unless append=true, which adds it to the end of the queue.",
		  "{\"type\":\"object\",\"properties\":{\"city_id\":{\"type\":\"integer\"},\"item\":{\"type\":\"string\"},\"append\":{\"type\":\"boolean\"}},\"required\":[\"city_id\",\"item\"]}", ToolSetProduction, true, false },
		{ "set_research",
		  "Choose the technology to research (type string from research_options, e.g. TECH_POTTERY).",
		  "{\"type\":\"object\",\"properties\":{\"tech\":{\"type\":\"string\"}},\"required\":[\"tech\"]}", ToolSetResearch, true, false },
		{ "end_turn",
		  "End your turn. Refused while a decision is pending (research, production, policy...). If units still await orders it is refused too, unless force=true, which makes them skip this turn.",
		  "{\"type\":\"object\",\"properties\":{\"force\":{\"type\":\"boolean\"}}}", ToolEndTurn, true, false },
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

const ClaudeToolDef* ClaudeFindTool(const std::string& name)
{
	for(size_t i = 0; i < sizeof(TOOLS) / sizeof(TOOLS[0]); ++i)
		if(name == TOOLS[i].name) return &TOOLS[i];
	return NULL;
}
