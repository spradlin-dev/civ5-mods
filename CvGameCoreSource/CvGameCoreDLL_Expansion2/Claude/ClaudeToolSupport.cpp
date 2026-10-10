#include "CvGameCoreDLLPCH.h"
#include <stdarg.h>
#include "ClaudeToolSupport.h"
#include "ClaudeBridge.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvTeam.h"
#include "../CvCity.h"
#include "../CvCityCitizens.h"
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
#include "../CvPromotionClasses.h"
#include "../CvPolicyClasses.h"
#include "../CvNotifications.h"
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

namespace ClaudeTool
{
	JVal J(int v) { return JVal((double)v); }
	JVal J(double v) { return JVal(v); }
	JVal J(bool v) { return JVal(v); }
	JVal J(const char* s) { return JVal(std::string(s ? s : "")); }
	JVal J(const std::string& s) { return JVal(s); }

	bool IsInt(const JVal& v, int& out)
	{
		if(!v.is<double>()) return false;
		double d = v.get<double>();
		if(d < -2147483648.0 || d >= 2147483648.0 || d != floor(d)) return false;
		out = (int)d;
		return true;
	}
	namespace
	{
		bool ReadInt(const JObj& a, const char* k, int& out)
		{
			JObj::const_iterator it = a.find(k);
			return it != a.end() && IsInt(it->second, out);
		}
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

	CvPlayer* MeActing(std::string& err)
	{
		CvPlayer* me = Me(err);
		if(!me) return NULL;
		if(!ClaudeIsPlayerTurn(*me))
		{
			err = "it is not your turn right now (the turn is ending or other civilizations are moving); call wait_for_my_turn";
			return NULL;
		}
		int iUnread = ClaudeBridge::Get().CountUnread();
		if(iUnread > 0)
		{
			err = ClaudeFormat("%d unread game message(s) are waiting: read them in briefing.game_messages, then call acknowledge_messages", iUnread);
			return NULL;
		}
		return me;
	}

	NetMessageScope::NetMessageScope() { CvUnit::dispatchingNetMessage(true); }
	NetMessageScope::~NetMessageScope() { CvUnit::dispatchingNetMessage(false); }

	double Moves(int iMoves) { return iMoves / (double)GC.getMOVE_DENOMINATOR(); }

	bool AiProcessing()
	{
		return !(gDLL->allAICivsProcessedThisTurn() && GC.getGame().allUnitAIProcessed());
	}

	bool IsEnemyOf(CvPlayer* me, PlayerTypes eOther)
	{
		if(eOther == NO_PLAYER || eOther == me->GetID()) return false;
		CvPlayer& other = GET_PLAYER(eOther);
		return other.isBarbarian() || GET_TEAM(me->getTeam()).isAtWar(other.getTeam());
	}

	bool PolicyChoiceAvailable(CvPlayer* me)
	{
		if(me->GetNumFreePolicies() <= 0 && me->getJONSCulture() < me->getNextPolicyCost()) return false;
		CvPlayerPolicies* pPolicies = me->GetPlayerPolicies();
		for(int i = 0; i < GC.getNumPolicyBranchInfos(); ++i)
			if(!pPolicies->IsPolicyBranchUnlocked((PolicyBranchTypes)i) && pPolicies->CanUnlockPolicyBranch((PolicyBranchTypes)i)) return true;
		for(int i = 0; i < GC.getNumPolicyInfos(); ++i)
			if(!pPolicies->HasPolicy((PolicyTypes)i) && me->canAdoptPolicy((PolicyTypes)i)) return true;
		return false;
	}

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
	const char* PromotionName(PromotionTypes e) { CvPromotionEntry* i = (e != NO_PROMOTION) ? GC.getPromotionInfo(e) : NULL; return i ? i->GetDescription() : ""; }

	std::string CivName(PlayerTypes e)
	{
		if(e == NO_PLAYER) return "";
		CvPlayer& p = GET_PLAYER(e);
		if(p.isBarbarian()) return "Barbarians";
		return p.getCivilizationShortDescription();
	}

	std::string Localized(const char* szKey)
	{
		if(!szKey || !*szKey) return "";
		return GetLocalizedText(szKey).c_str();
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
	PromotionTypes FindPromotionType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumPromotionInfos(); ++i)
		{
			CvPromotionEntry* e = GC.getPromotionInfo((PromotionTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (PromotionTypes)i;
		}
		return NO_PROMOTION;
	}
	PolicyTypes FindPolicyType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumPolicyInfos(); ++i)
		{
			CvPolicyEntry* e = GC.getPolicyInfo((PolicyTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (PolicyTypes)i;
		}
		return NO_POLICY;
	}
	PolicyBranchTypes FindPolicyBranchType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumPolicyBranchInfos(); ++i)
		{
			CvPolicyBranchEntry* e = GC.getPolicyBranchInfo((PolicyBranchTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (PolicyBranchTypes)i;
		}
		return NO_POLICY_BRANCH_TYPE;
	}
	ImprovementTypes FindImprovementType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumImprovementInfos(); ++i)
		{
			CvImprovementEntry* e = GC.getImprovementInfo((ImprovementTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (ImprovementTypes)i;
		}
		return NO_IMPROVEMENT;
	}
	ResourceTypes FindResourceType(const std::string& key)
	{
		for(int i = 0; i < GC.getNumResourceInfos(); ++i)
		{
			CvResourceInfo* e = GC.getResourceInfo((ResourceTypes)i);
			if(e && _stricmp(e->GetType(), key.c_str()) == 0) return (ResourceTypes)i;
		}
		return NO_RESOURCE;
	}
	PlayerTypes FindPlayer(const std::string& key)
	{
		if(key.empty()) return NO_PLAYER;
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isEverAlive()) continue;
			if(_stricmp(p.getCivilizationShortDescription(), key.c_str()) == 0) return (PlayerTypes)i;
			if(_stricmp(p.getName(), key.c_str()) == 0) return (PlayerTypes)i;
			if(key == ClaudeFormat("%d", i)) return (PlayerTypes)i;
		}
		return NO_PLAYER;
	}

	namespace
	{
		struct FocusDef
		{
			const char* name;
			CityAIFocusTypes focus;
		};
		const FocusDef FOCUSES[] =
		{
			{ "default", NO_CITY_AI_FOCUS_TYPE },
			{ "food", CITY_AI_FOCUS_TYPE_FOOD },
			{ "production", CITY_AI_FOCUS_TYPE_PRODUCTION },
			{ "gold", CITY_AI_FOCUS_TYPE_GOLD },
			{ "science", CITY_AI_FOCUS_TYPE_SCIENCE },
			{ "culture", CITY_AI_FOCUS_TYPE_CULTURE },
			{ "faith", CITY_AI_FOCUS_TYPE_FAITH },
			{ "great_people", CITY_AI_FOCUS_TYPE_GREAT_PEOPLE },
			{ "production_growth", CITY_AI_FOCUS_TYPE_PROD_GROWTH },
			{ "gold_growth", CITY_AI_FOCUS_TYPE_GOLD_GROWTH },
		};
		const size_t NUM_FOCUSES = sizeof(FOCUSES) / sizeof(FOCUSES[0]);
	}

	const char* FocusName(CityAIFocusTypes e)
	{
		for(size_t i = 0; i < NUM_FOCUSES; ++i)
			if(FOCUSES[i].focus == e) return FOCUSES[i].name;
		return "default";
	}
	bool FindFocus(const std::string& name, CityAIFocusTypes& out)
	{
		for(size_t i = 0; i < NUM_FOCUSES; ++i)
		{
			if(name == FOCUSES[i].name)
			{
				out = FOCUSES[i].focus;
				return true;
			}
		}
		return false;
	}
	std::string FocusNames()
	{
		std::string s;
		for(size_t i = 0; i < NUM_FOCUSES; ++i)
		{
			if(i) s += ", ";
			s += FOCUSES[i].name;
		}
		return s;
	}

	std::string BlockingName(EndTurnBlockingTypes e)
	{
		static const char* NO_TOOL = " (no tool for this yet; the human must handle it in the game)";
		switch(e)
		{
		case NO_ENDTURN_BLOCKING_TYPE: return "none";
		case ENDTURN_BLOCKING_POLICY: return "policy: you can adopt a social policy (policy_options / adopt_policy)";
		case ENDTURN_BLOCKING_RESEARCH: return "research: choose a technology to research (research_options / set_research)";
		case ENDTURN_BLOCKING_PRODUCTION: return "production: a city needs a production choice (city_options / set_production)";
		case ENDTURN_BLOCKING_UNITS: return "units: a unit still awaits orders (give it an order, or end_turn with force=true to let idle units skip this turn)";
		case ENDTURN_BLOCKING_UNIT_PROMOTION: return "unit_promotion: a unit can be promoted (unit_options / promote_unit)";
		case ENDTURN_BLOCKING_STACKED_UNITS: return "stacked_units: two units share a tile; move one";
		case ENDTURN_BLOCKING_CITY_RANGE_ATTACK: return "city_range_attack: a city can attack an enemy (city_attack)";
		case ENDTURN_BLOCKING_FREE_TECH: return "free_tech: you have a free technology to claim (set_research grants it)";
		case ENDTURN_BLOCKING_FREE_POLICY: return "free_policy: you have a free policy to adopt (adopt_policy)";
		case ENDTURN_BLOCKING_DIPLO_VOTE: return std::string("diplo_vote: a World Congress vote is pending") + NO_TOOL;
		case ENDTURN_BLOCKING_MINOR_QUEST: return std::string("minor_quest: a city-state quest needs a response") + NO_TOOL;
		case ENDTURN_BLOCKING_FREE_ITEMS: return std::string("free_items: choose free items") + NO_TOOL;
		case ENDTURN_BLOCKING_FOUND_PANTHEON: return std::string("found_pantheon: choose a pantheon belief") + NO_TOOL;
		case ENDTURN_BLOCKING_FOUND_RELIGION: return std::string("found_religion: found a religion") + NO_TOOL;
		case ENDTURN_BLOCKING_ENHANCE_RELIGION: return std::string("enhance_religion: enhance your religion") + NO_TOOL;
		case ENDTURN_BLOCKING_STEAL_TECH: return std::string("steal_tech: choose a technology to steal") + NO_TOOL;
		case ENDTURN_BLOCKING_MAYA_LONG_COUNT: return std::string("maya_long_count: choose a great person") + NO_TOOL;
		case ENDTURN_BLOCKING_FAITH_GREAT_PERSON: return std::string("faith_great_person: choose a great person") + NO_TOOL;
		case ENDTURN_BLOCKING_ADD_REFORMATION_BELIEF: return std::string("reformation_belief: choose a reformation belief") + NO_TOOL;
		case ENDTURN_BLOCKING_LEAGUE_CALL_FOR_PROPOSALS: return std::string("league_proposals: the World Congress wants proposals") + NO_TOOL;
		case ENDTURN_BLOCKING_CHOOSE_ARCHAEOLOGY: return std::string("archaeology: choose what to do with an artifact") + NO_TOOL;
		case ENDTURN_BLOCKING_LEAGUE_CALL_FOR_VOTES: return std::string("league_votes: the World Congress wants votes") + NO_TOOL;
		case ENDTURN_BLOCKING_CHOOSE_IDEOLOGY: return std::string("ideology: choose an ideology") + NO_TOOL;
		default: return ClaudeFormat("other (blocking type %d)", (int)e) + NO_TOOL;
		}
	}

	EndTurnBlockingTypes EndTurnBlocking(CvPlayer* me)
	{
		EndTurnBlockingTypes e = NO_ENDTURN_BLOCKING_TYPE;
		int iNotification = -1;
		if(me->GetNotifications()) me->GetNotifications()->GetEndTurnBlockedType(e, iNotification);
		if(e != NO_ENDTURN_BLOCKING_TYPE) return e;
		if(me->hasPromotableUnit() && !GC.getGame().isOption(GAMEOPTION_PROMOTION_SAVING)) return ENDTURN_BLOCKING_UNIT_PROMOTION;
		if(me->hasReadyUnit())
		{
			const CvUnit* u = me->GetFirstReadyUnit();
			return (u && !u->canHold(u->plot())) ? ENDTURN_BLOCKING_STACKED_UNITS : ENDTURN_BLOCKING_UNITS;
		}
		return NO_ENDTURN_BLOCKING_TYPE;
	}

	bool ResolveAttackTarget(CvPlayer* me, CvUnit* u, CvPlot* p, AttackTarget& out, std::string& why)
	{
		out.unit = NULL;
		out.city = NULL;
		out.ranged = u->isRanged();
		if(u->GetBaseCombatStrength() <= 0) { why = "that unit cannot attack"; return false; }
		if(!p->isVisible(me->getTeam())) { why = "target tile is not visible"; return false; }
		if(out.ranged)
		{
			if(!u->canRangeStrikeAt(p->getX(), p->getY())) { why = "cannot range-strike that tile (out of range, no line of sight, not at war, or this unit already attacked this turn)"; return false; }
		}
		else
		{
			if(plotDistance(u->getX(), u->getY(), p->getX(), p->getY()) != 1) { why = "melee attacks need the unit adjacent to the target; move next to it first"; return false; }
			if(!u->canMoveOrAttackInto(*p)) { why = "this unit cannot attack that tile from here (no moves left, already attacked, or terrain forbids it)"; return false; }
		}
		CvCity* c = p->getPlotCity();
		if(c && IsEnemyOf(me, c->getOwner()))
		{
			out.city = c;
			return true;
		}
		UnitHandle hDefender = p->getBestDefender(NO_PLAYER, me->GetID(), u, true, false, false, true);
		out.unit = hDefender.pointer();
		if(!out.unit) { why = "no enemy unit or city on that tile (you must be at war with its owner)"; return false; }
		return true;
	}

	namespace
	{
		bool CanFound(CvUnit* u, CvPlot* p) { return u->canFound(p); }
		bool CanFortify(CvUnit* u, CvPlot* p) { return u->canFortify(p); }
		bool CanSleep(CvUnit* u, CvPlot* p) { return u->canSleep(p); }
		bool CanHold(CvUnit* u, CvPlot* p) { return u->canHold(p); }
		bool CanSentry(CvUnit* u, CvPlot* p) { return u->canSentry(p); }
		bool CanHeal(CvUnit* u, CvPlot* p) { return u->canHeal(p); }
		bool CanPillage(CvUnit* u, CvPlot* p) { return u->canPillage(p); }

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
	}

	const UnitActionDef* UnitActions(size_t& count)
	{
		count = sizeof(UNIT_ACTIONS) / sizeof(UNIT_ACTIONS[0]);
		return UNIT_ACTIONS;
	}

	std::string UnitActionNames()
	{
		std::string s;
		size_t count = 0;
		const UnitActionDef* defs = UnitActions(count);
		for(size_t i = 0; i < count; ++i)
		{
			if(i) s += ", ";
			s += defs[i].name;
		}
		return s;
	}

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
		case ACTIVITY_SLEEP: return (u->IsFortifiedThisTurn() || u->getFortifyTurns() > 0) ? "fortified" : "sleeping";
		case ACTIVITY_HOLD: return "skipped this turn";
		case ACTIVITY_HEAL: return "healing";
		case ACTIVITY_SENTRY: return "on alert";
		case ACTIVITY_MISSION: return "on a mission";
		default: break;
		}
		return u->ReadyToMove() ? "awaiting orders" : "done for this turn";
	}

	JArr PromotionsAvailable(CvUnit* u)
	{
		JArr list;
		for(int i = 0; i < GC.getNumPromotionInfos(); ++i)
		{
			PromotionTypes e = (PromotionTypes)i;
			if(!u->canPromote(e, -1)) continue;
			CvPromotionEntry* info = GC.getPromotionInfo(e);
			JObj p;
			p["type"] = J(info->GetType());
			p["name"] = J(info->GetDescription());
			p["help"] = J(Localized(info->GetHelp()));
			list.push_back(JVal(p));
		}
		return list;
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
		if(u->isPromotionReady()) o["promotion_ready"] = J(true);
		if(u->CanUpgradeRightNow(false))
		{
			o["upgrade_available_to"] = J(UnitTypeName(u->GetUpgradeUnitType()));
			o["upgrade_cost"] = J(u->upgradePrice(u->GetUpgradeUnitType()));
		}
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
		o["focus"] = J(FocusName(c->GetCityCitizens()->GetFocusType()));

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
		if(c->CanRangeStrikeNow()) o["can_attack"] = J(true);
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
				if(!u || u->isInvisible(eTeam, false)) continue;
				JObj uo;
				uo["owner"] = J(CivName(u->getOwner()));
				uo["type"] = J(UnitTypeName(u->getUnitType()));
				uo["hp"] = J(u->GetCurrHitPoints());
				uo["strength"] = J(u->GetBaseCombatStrength());
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
		if(me->GetNumFreeTechs() > 0) r["free_techs_to_claim"] = J(me->GetNumFreeTechs());
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
}
