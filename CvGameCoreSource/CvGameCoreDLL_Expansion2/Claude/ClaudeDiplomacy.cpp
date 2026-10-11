#include "CvGameCoreDLLPCH.h"
#include "ClaudeDiplomacy.h"
#include "ClaudeSeat.h"
#include "ClaudeBridge.h"
#include "ClaudeToolSupport.h"
#include "ClaudeToolHandlers.h"
#include "../CvGame.h"
#include "../CvPlayer.h"
#include "../CvTeam.h"
#include "../CvCity.h"
#include "../CvDealClasses.h"
#include "../CvDealAI.h"
#include "../CvDiplomacyAI.h"
#include "../CvDiplomacyRequests.h"
#include "../CvNotifications.h"
#include "ClaudePopups.h"
#include "../CvGlobals.h"

using namespace ClaudeTool;

// ------------------------------------------------------------- request store

namespace
{
	struct DiploRequest
	{
		int id;
		int turn;
		PlayerTypes from;
		DiploUIStateTypes state;
		std::string message;
		int extra; // the game's extra data: the city-state or third civilization the request is about
		bool hasDeal;
		CvDeal deal;
	};
	std::vector<DiploRequest> s_requests;
	int s_seq = 0;

	bool IsOffScreenSeat(PlayerTypes e)
	{
		return e != NO_PLAYER && e == ClaudeSeat() && !ClaudeSeatOnScreen();
	}

	// Testing only: a leader screen aimed at the idle human would block the game until someone clicks it.
	bool DropForIdleHuman(PlayerTypes eTo)
	{
		return ClaudeBridge::Get().TestingMode() && !ClaudeSeatOnScreen() && eTo == GC.getGame().getActivePlayer();
	}

	const char* StateName(DiploUIStateTypes e)
	{
		switch(e)
		{
		case DIPLO_UI_STATE_DEFAULT_ROOT: return "greeting";
		case DIPLO_UI_STATE_WAR_DECLARED_BY_HUMAN: return "reaction_to_your_war";
		case DIPLO_UI_STATE_PEACE_MADE_BY_HUMAN: return "reaction_to_peace";
		case DIPLO_UI_STATE_TRADE: return "trade";
		case DIPLO_UI_STATE_TRADE_AI_MAKES_OFFER: return "deal_offer";
		case DIPLO_UI_STATE_TRADE_AI_ACCEPTS_OFFER: return "deal_accepted";
		case DIPLO_UI_STATE_TRADE_AI_REJECTS_OFFER: return "deal_rejected";
		case DIPLO_UI_STATE_TRADE_AI_MAKES_DEMAND: return "demand";
		case DIPLO_UI_STATE_TRADE_AI_MAKES_REQUEST: return "request_for_help";
		case DIPLO_UI_STATE_TRADE_HUMAN_OFFERS_CONCESSIONS: return "concessions";
		case DIPLO_UI_STATE_HUMAN_DEMAND: return "your_demand";
		case DIPLO_UI_STATE_AI_DECLARED_WAR: return "war_declared_on_you";
		case DIPLO_UI_STATE_DISCUSS_HUMAN_INVOKED: return "discussion";
		case DIPLO_UI_STATE_DISCUSS_AGGRESSIVE_MILITARY_WARNING: return "military_warning";
		case DIPLO_UI_STATE_DISCUSS_I_ATTACKED_YOUR_MINOR_CIV: return "they_attacked_your_city_state";
		case DIPLO_UI_STATE_DISCUSS_I_BULLIED_YOUR_MINOR_CIV: return "they_bullied_your_city_state";
		case DIPLO_UI_STATE_DISCUSS_YOU_ATTACKED_MINOR_CIV: return "you_attacked_their_city_state";
		case DIPLO_UI_STATE_DISCUSS_YOU_KILLED_MINOR_CIV: return "you_destroyed_their_city_state";
		case DIPLO_UI_STATE_DISCUSS_YOU_BULLIED_MINOR_CIV: return "you_bullied_their_city_state";
		case DIPLO_UI_STATE_DISCUSS_PROTECT_MINOR_CIV: return "protect_city_state";
		case DIPLO_UI_STATE_DISCUSS_YOU_EXPANSION_SERIOUS_WARNING: return "expansion_serious_warning";
		case DIPLO_UI_STATE_DISCUSS_YOU_EXPANSION_WARNING: return "expansion_warning";
		case DIPLO_UI_STATE_DISCUSS_YOU_PLOT_BUYING_SERIOUS_WARNING: return "land_buying_serious_warning";
		case DIPLO_UI_STATE_DISCUSS_YOU_PLOT_BUYING_WARNING: return "land_buying_warning";
		case DIPLO_UI_STATE_DISCUSS_WORK_WITH_US: return "friendship_offer";
		case DIPLO_UI_STATE_DISCUSS_WORK_AGAINST_SOMEONE: return "work_against_someone";
		case DIPLO_UI_STATE_DISCUSS_COOP_WAR: return "joint_war_proposal";
		case DIPLO_UI_STATE_DISCUSS_COOP_WAR_TIME: return "joint_war_now";
		case DIPLO_UI_STATE_DISCUSS_PLAN_RESEARCH_AGREEMENT: return "research_agreement_plan";
		case DIPLO_UI_STATE_DISCUSS_AI_REQUEST_DENOUNCE: return "denounce_request";
		case DIPLO_UI_STATE_CAUGHT_YOUR_SPY: return "your_spy_caught";
		case DIPLO_UI_STATE_KILLED_YOUR_SPY: return "your_spy_killed";
		case DIPLO_UI_STATE_KILLED_MY_SPY: return "you_killed_their_spy";
		case DIPLO_UI_STATE_CONFRONT_YOU_KILLED_MY_SPY: return "confront_spy_killing";
		case DIPLO_UI_STATE_STOP_CONVERSIONS: return "stop_converting";
		case DIPLO_UI_STATE_STOP_DIGGING: return "stop_digging";
		default: return "statement";
		}
	}

	// A yes/no style answer: the game's button 1 and button 2 for this state.
	struct TwoWay { const char* first; const char* second; FromUIDiploEventTypes event; bool passesExtra; };

	bool TwoWayFor(DiploUIStateTypes e, TwoWay& out)
	{
		static const struct { DiploUIStateTypes state; TwoWay answer; } TABLE[] =
		{
			{ DIPLO_UI_STATE_DISCUSS_AGGRESSIVE_MILITARY_WARNING, { "promise_no_harm", "refuse_and_declare_war", FROM_UI_DIPLO_EVENT_AGGRESSIVE_MILITARY_WARNING_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_I_ATTACKED_YOUR_MINOR_CIV, { "forgive", "warn", FROM_UI_DIPLO_EVENT_I_ATTACKED_YOUR_MINOR_CIV_RESPONSE, true } },
			{ DIPLO_UI_STATE_DISCUSS_I_BULLIED_YOUR_MINOR_CIV, { "forgive", "warn", FROM_UI_DIPLO_EVENT_I_BULLIED_YOUR_MINOR_CIV_RESPONSE, true } },
			{ DIPLO_UI_STATE_DISCUSS_YOU_ATTACKED_MINOR_CIV, { "none_of_your_business", "promise_to_withdraw", FROM_UI_DIPLO_EVENT_ATTACKED_MINOR_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_YOU_BULLIED_MINOR_CIV, { "ignore", "promise_to_stop", FROM_UI_DIPLO_EVENT_BULLIED_MINOR_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_YOU_EXPANSION_WARNING, { "ignore", "promise_no_settling", FROM_UI_DIPLO_EVENT_EXPANSION_WARNING_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_YOU_PLOT_BUYING_WARNING, { "ignore", "promise_to_stop", FROM_UI_DIPLO_EVENT_PLOT_BUYING_WARNING_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_WORK_WITH_US, { "accept", "decline", FROM_UI_DIPLO_EVENT_WORK_WITH_US_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_COOP_WAR_TIME, { "accept", "decline", FROM_UI_DIPLO_EVENT_COOP_WAR_NOW_RESPONSE, true } },
			{ DIPLO_UI_STATE_DISCUSS_PLAN_RESEARCH_AGREEMENT, { "accept", "decline", FROM_UI_DIPLO_EVENT_PLAN_RA_RESPONSE, false } },
			{ DIPLO_UI_STATE_DISCUSS_AI_REQUEST_DENOUNCE, { "accept", "decline", FROM_UI_DIPLO_EVENT_AI_REQUEST_DENOUNCE_RESPONSE, true } },
			{ DIPLO_UI_STATE_CAUGHT_YOUR_SPY, { "ignore", "promise_to_stop", FROM_UI_DIPLO_EVENT_CAUGHT_YOUR_SPY_RESPONSE, false } },
			{ DIPLO_UI_STATE_KILLED_MY_SPY, { "no_apology", "forgive", FROM_UI_DIPLO_EVENT_KILLED_MY_SPY_RESPONSE, false } },
			{ DIPLO_UI_STATE_STOP_CONVERSIONS, { "ignore", "promise_to_stop", FROM_UI_DIPLO_EVENT_STOP_CONVERSIONS, false } },
			{ DIPLO_UI_STATE_STOP_DIGGING, { "ignore", "promise_to_stop", FROM_UI_DIPLO_EVENT_STOP_DIGGING, false } },
		};
		for(size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i)
			if(TABLE[i].state == e) { out = TABLE[i].answer; return true; }
		return false;
	}

	bool IsDealState(DiploUIStateTypes e)
	{
		return e == DIPLO_UI_STATE_TRADE_AI_MAKES_OFFER || e == DIPLO_UI_STATE_TRADE_AI_MAKES_DEMAND || e == DIPLO_UI_STATE_TRADE_AI_MAKES_REQUEST;
	}

	// One-button acknowledgements that still tell the game something.
	bool AcknowledgeEvent(DiploUIStateTypes e, FromUIDiploEventTypes& ev)
	{
		switch(e)
		{
		case DIPLO_UI_STATE_DISCUSS_YOU_KILLED_MINOR_CIV: ev = FROM_UI_DIPLO_EVENT_KILLED_MINOR_RESPONSE; return true;
		case DIPLO_UI_STATE_DISCUSS_YOU_EXPANSION_SERIOUS_WARNING: ev = FROM_UI_DIPLO_EVENT_EXPANSION_SERIOUS_WARNING_RESPONSE; return true;
		case DIPLO_UI_STATE_DISCUSS_YOU_PLOT_BUYING_SERIOUS_WARNING: ev = FROM_UI_DIPLO_EVENT_PLOT_BUYING_SERIOUS_WARNING_RESPONSE; return true;
		default: return false;
		}
	}

	// Whether the request's extra value names the civilization it is about.
	bool ExtraIsPlayer(DiploUIStateTypes e)
	{
		TwoWay tw;
		return e == DIPLO_UI_STATE_DISCUSS_COOP_WAR || (TwoWayFor(e, tw) && tw.passesExtra);
	}

	std::string Choices(const DiploRequest& r)
	{
		TwoWay tw;
		if(IsDealState(r.state)) return "accept, decline";
		if(r.state == DIPLO_UI_STATE_DISCUSS_COOP_WAR) return "accept, later, decline";
		if(TwoWayFor(r.state, tw)) return std::string(tw.first) + ", " + tw.second;
		return "acknowledge";
	}

	PlayerTypes AnyPlayerOnTeam(TeamTypes eTeam)
	{
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
			if(GET_PLAYER((PlayerTypes)i).isEverAlive() && GET_PLAYER((PlayerTypes)i).getTeam() == eTeam) return (PlayerTypes)i;
		return NO_PLAYER;
	}

	JVal DealJson(const CvDeal& d)
	{
		JObj o;
		o["from"] = J(CivName(d.m_eFromPlayer));
		o["to"] = J(CivName(d.m_eToPlayer));
		if(d.GetPeaceTreatyType() != NO_PEACE_TREATY_TYPE) o["peace_treaty"] = J(true);
		if(d.GetSurrenderingPlayer() != NO_PLAYER) o["surrendering"] = J(CivName(d.GetSurrenderingPlayer()));
		JArr items;
		CvDeal& deal = const_cast<CvDeal&>(d); // the item list only has non-const iteration
		for(TradedItemList::iterator it = deal.m_TradedItems.begin(); it != deal.m_TradedItems.end(); ++it)
		{
			JObj i;
			i["given_by"] = J(CivName(it->m_eFromPlayer));
			switch(it->m_eItemType)
			{
			case TRADE_ITEM_GOLD: i["item"] = J("gold"); i["amount"] = J(it->m_iData1); break;
			case TRADE_ITEM_GOLD_PER_TURN: i["item"] = J("gold_per_turn"); i["amount"] = J(it->m_iData1); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_MAPS: i["item"] = J("maps"); break;
			case TRADE_ITEM_RESOURCES: i["item"] = J("resource"); i["resource"] = J(ResourceName((ResourceTypes)it->m_iData1)); i["amount"] = J(it->m_iData2); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_CITIES:
			{
				i["item"] = J("city");
				CvPlot* p = GC.getMap().plot(it->m_iData1, it->m_iData2);
				CvCity* c = p ? p->getPlotCity() : NULL;
				i["city"] = J(c ? c->getName().c_str() : "?");
				if(c) i["city_id"] = J(c->GetID());
				break;
			}
			case TRADE_ITEM_UNITS: i["item"] = J("unit"); i["unit_id"] = J(it->m_iData1); break;
			case TRADE_ITEM_OPEN_BORDERS: i["item"] = J("open_borders"); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_DEFENSIVE_PACT: i["item"] = J("defensive_pact"); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_RESEARCH_AGREEMENT: i["item"] = J("research_agreement"); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_PEACE_TREATY: i["item"] = J("peace_treaty"); i["turns"] = J(it->m_iDuration); break;
			case TRADE_ITEM_THIRD_PARTY_PEACE: i["item"] = J("third_party_peace"); i["with"] = J(CivName(AnyPlayerOnTeam((TeamTypes)it->m_iData1))); break;
			case TRADE_ITEM_THIRD_PARTY_WAR: i["item"] = J("third_party_war"); i["against"] = J(CivName(AnyPlayerOnTeam((TeamTypes)it->m_iData1))); break;
			case TRADE_ITEM_ALLOW_EMBASSY: i["item"] = J("embassy"); break;
			case TRADE_ITEM_DECLARATION_OF_FRIENDSHIP: i["item"] = J("declaration_of_friendship"); break;
			case TRADE_ITEM_VOTE_COMMITMENT: i["item"] = J("vote_commitment"); i["resolution_id"] = J(it->m_iData1); i["choice"] = J(it->m_iData2); i["votes"] = J(it->m_iData3); break;
			default: i["item"] = J("other"); break;
			}
			items.push_back(JVal(i));
		}
		o["items"] = JVal(items);
		return JVal(o);
	}

	JVal RequestJson(const DiploRequest& r)
	{
		JObj o;
		o["id"] = J(r.id);
		o["turn"] = J(r.turn);
		o["from"] = J(CivName(r.from));
		o["kind"] = J(StateName(r.state));
		o["message"] = J(r.message);
		if(ExtraIsPlayer(r.state) && r.extra >= 0 && r.extra < MAX_CIV_PLAYERS) o["about"] = J(CivName((PlayerTypes)r.extra));
		if(r.hasDeal) o["deal"] = DealJson(r.deal);
		o["choices"] = J(Choices(r));
		return JVal(o);
	}

	DiploRequest* FindRequest(int id)
	{
		for(size_t i = 0; i < s_requests.size(); ++i)
			if(s_requests[i].id == id) return &s_requests[i];
		return NULL;
	}

	void EraseRequest(int id)
	{
		for(size_t i = 0; i < s_requests.size(); ++i)
			if(s_requests[i].id == id) { s_requests.erase(s_requests.begin() + i); return; }
	}

	void Record(PlayerTypes eFrom, DiploUIStateTypes eState, const char* szMessage, int iExtra, const CvDeal* pDeal)
	{
		DiploRequest r;
		r.id = ++s_seq;
		r.turn = GC.getGame().getGameTurn();
		r.from = eFrom;
		r.state = eState;
		r.message = szMessage ? szMessage : "";
		r.extra = iExtra;
		r.hasDeal = (pDeal != NULL);
		if(pDeal) r.deal = *pDeal;
		s_requests.push_back(r);
		ClaudeBridge::Get().LogLine(ClaudeFormat("[diplomacy #%d] %s (%s): ", r.id, CivName(eFrom).c_str(), StateName(eState)) + r.message);
	}

	// ------------------------------------------------------------- deal building

	int StandardDuration() { return GC.getGame().GetDealDuration(); }

	// The same path two humans use in multiplayer: the deal waits in the game's proposed
	// deals and the other player gets a Diplomacy notification that opens their deal screen.
	bool OfferDealToHuman(CvPlayer* me, CvPlayer* other, CvDeal& deal, JObj& out, std::string& err)
	{
		CvGameDeals* pDeals = GC.getGame().GetGameDeals();
		if(pDeals->ProposedDealExists(me->GetID(), other->GetID())) { err = "they already have an offer from you waiting; wait for their answer"; return false; }
		pDeals->AddProposedDeal(deal);
		CvNotifications* pN = other->GetNotifications();
		if(pN)
		{
			std::string summary = CivName(me->GetID()) + " proposes a deal";
			pN->Add(NOTIFICATION_PLAYER_DEAL_RECEIVED, (summary + ": open it from the Diplomacy notification to accept or decline").c_str(), summary.c_str(), me->GetID(), -1, -1);
		}
		out["ok"] = J(true);
		out["proposed_to_human"] = J(CivName(other->GetID()));
		out["next"] = J("their answer arrives as a game message; the deal stays open until they decide");
		return true;
	}

	bool AddItems(CvDeal& deal, PlayerTypes eGiver, PlayerTypes eReceiver, const JArr& items, std::string& err)
	{
		for(size_t n = 0; n < items.size(); ++n)
		{
			if(!items[n].is<JObj>()) { err = "each item must be an object with an item name"; return false; }
			const JObj& it = items[n].get<JObj>();
			std::string kind = ArgStr(it, "item");
			int amount = ArgInt(it, "amount", 0);
			int dur = StandardDuration();
			std::string what = kind + " given by " + CivName(eGiver);
			bool ok = false;
			if(kind == "gold")
			{
				if(amount <= 0) { err = "gold needs an amount"; return false; }
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_GOLD, amount);
				if(ok) deal.AddGoldTrade(eGiver, amount);
			}
			else if(kind == "gold_per_turn")
			{
				if(amount <= 0) { err = "gold_per_turn needs an amount"; return false; }
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_GOLD_PER_TURN, amount, dur);
				if(ok) deal.AddGoldPerTurnTrade(eGiver, amount, dur);
			}
			else if(kind == "resource")
			{
				ResourceTypes eRes = FindResourceType(ArgStr(it, "resource"));
				if(eRes == NO_RESOURCE) { err = "unknown resource '" + ArgStr(it, "resource") + "'"; return false; }
				if(amount <= 0) amount = 1;
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_RESOURCES, eRes, amount, dur);
				if(ok) deal.AddResourceTrade(eGiver, eRes, amount, dur);
				what = std::string(ResourceName(eRes)) + " given by " + CivName(eGiver);
			}
			else if(kind == "open_borders")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_OPEN_BORDERS, -1, -1, dur);
				if(ok) deal.AddOpenBorders(eGiver, dur);
			}
			else if(kind == "embassy")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_ALLOW_EMBASSY);
				if(ok) deal.AddAllowEmbassy(eGiver);
			}
			else if(kind == "defensive_pact")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_DEFENSIVE_PACT, -1, -1, dur);
				if(ok) deal.AddDefensivePact(eGiver, dur);
			}
			else if(kind == "research_agreement")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_RESEARCH_AGREEMENT, -1, -1, dur);
				if(ok) deal.AddResearchAgreement(eGiver, dur);
			}
			else if(kind == "maps")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_MAPS);
				if(ok) deal.AddMapTrade(eGiver);
			}
			else if(kind == "declaration_of_friendship")
			{
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_DECLARATION_OF_FRIENDSHIP);
				if(ok) deal.AddDeclarationOfFriendship(eGiver);
			}
			else if(kind == "city")
			{
				CvCity* c = GET_PLAYER(eGiver).getCity(ArgInt(it, "city_id", -1));
				if(!c) { err = "city_id does not name a city of " + CivName(eGiver); return false; }
				ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_CITIES, c->getX(), c->getY());
				if(ok) deal.AddCityTrade(eGiver, c->GetID());
				what = c->getName() + " given by " + CivName(eGiver);
			}
			else if(kind == "third_party_war" || kind == "third_party_peace")
			{
				PlayerTypes eThird = FindPlayer(ArgStr(it, "civ"));
				if(eThird == NO_PLAYER) { err = kind + " needs civ: the third civilization"; return false; }
				TeamTypes eTeam = GET_PLAYER(eThird).getTeam();
				if(kind == "third_party_war")
				{
					ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_THIRD_PARTY_WAR, eTeam);
					if(ok) deal.AddThirdPartyWar(eGiver, eTeam);
				}
				else
				{
					ok = deal.IsPossibleToTradeItem(eGiver, eReceiver, TRADE_ITEM_THIRD_PARTY_PEACE, eTeam, -1, dur);
					if(ok) deal.AddThirdPartyPeace(eGiver, eTeam, dur);
				}
			}
			else
			{
				err = "unknown item '" + kind + "'; use gold, gold_per_turn, resource, open_borders, embassy, defensive_pact, research_agreement, maps, declaration_of_friendship, city, third_party_war or third_party_peace";
				return false;
			}
			if(!ok) { err = "the game does not allow " + what + " in this deal right now"; return false; }
		}
		return true;
	}

	// The AI's verdict on a deal Claude proposes, and what would make it acceptable.
	JVal JudgeDeal(CvPlayer* me, CvPlayer& other, CvDeal& deal, bool bExecute, std::string& err)
	{
		CvDealAI* ai = other.GetDealAI();
		int iToThem = 0, iTheyGive = 0, iWeGive = 0, iOver = 0, iUnder = 0;
		bool bCantMatch = false;
		bool bOk = ai->IsDealWithHumanAcceptable(&deal, me->GetID(), iToThem, iTheyGive, iWeGive, iOver, iUnder, bCantMatch);
		if(!bOk && !deal.IsPeaceTreatyTrade(me->GetID()) && iWeGive > iTheyGive) bOk = true; // a gift is always taken
		JObj o;
		o["acceptable"] = J(bOk);
		o["value_to_them"] = J(iToThem);
		if(!bOk)
		{
			CvDeal copy(deal);
			bool bGood = false, bCant = false;
			ai->DoEqualizeDealWithHuman(&copy, me->GetID(), false, false, bGood, bCant);
			if(!bCant) o["what_would_work"] = DealJson(copy);
			else o["what_would_work"] = J("nothing you have would balance this");
		}
		if(!bExecute) { o["deal"] = DealJson(deal); return JVal(o); }
		if(!bOk)
		{
			err = (iToThem > -75 && iTheyGive < iWeGive * 5) ? "they refuse: not good enough for them" : "they refuse: insulting";
			return JVal(o);
		}
		ai->DoAcceptedDeal(me->GetID(), deal, iToThem, iTheyGive, iWeGive);
		o["ok"] = J(true);
		o["deal"] = DealJson(deal);
		return JVal(o);
	}

	CvPlayer* OtherMajor(CvPlayer* me, const JObj& args, std::string& err)
	{
		std::string key = ArgStr(args, "civ");
		PlayerTypes e = FindPlayer(key);
		if(e == NO_PLAYER || e == me->GetID() || e >= MAX_MAJOR_CIVS) { err = "unknown civilization '" + key + "'; use a name from civs"; return NULL; }
		CvPlayer& other = GET_PLAYER(e);
		if(!other.isAlive()) { err = CivName(e) + " is no longer in the game"; return NULL; }
		if(!GET_TEAM(me->getTeam()).isHasMet(other.getTeam())) { err = "you have not met " + CivName(e); return NULL; }
		return &other;
	}

	JVal RelationJson(CvPlayer* me, CvPlayer& other)
	{
		JObj o;
		CvTeam& mine = GET_TEAM(me->getTeam());
		CvTeam& theirs = GET_TEAM(other.getTeam());
		o["at_war"] = J(mine.isAtWar(other.getTeam()));
		o["same_team"] = J(other.getTeam() == me->getTeam());
		o["our_embassy_with_them"] = J(mine.HasEmbassyAtTeam(other.getTeam()));
		o["their_embassy_with_us"] = J(theirs.HasEmbassyAtTeam(me->getTeam()));
		o["open_borders_for_us"] = J(theirs.IsAllowsOpenBordersToTeam(me->getTeam()));
		o["open_borders_for_them"] = J(mine.IsAllowsOpenBordersToTeam(other.getTeam()));
		o["defensive_pact"] = J(mine.IsHasDefensivePact(other.getTeam()));
		o["research_agreement"] = J(mine.IsHasResearchAgreement(other.getTeam()));
		if(!other.isHuman())
		{
			CvDiplomacyAI* d = other.GetDiplomacyAI();
			o["declared_friendship"] = J(d->IsDoFAccepted(me->GetID()));
			o["denounced_you"] = J(d->IsDenouncedPlayer(me->GetID()));
			if(mine.isAtWar(other.getTeam()))
				o["willing_to_talk_peace"] = J(mine.canChangeWarPeace(other.getTeam()) && d->IsWillingToMakePeaceWithHuman(me->GetID())
				                              && d->GetTreatyWillingToOffer(me->GetID()) >= PEACE_TREATY_WHITE_PEACE && d->GetTreatyWillingToAccept(me->GetID()) >= PEACE_TREATY_WHITE_PEACE);
		}
		return JVal(o);
	}
}

// ------------------------------------------------------------- hooks

void ClaudeNoteDealResolved(PlayerTypes eFrom, PlayerTypes eTo, PlayerTypes eActBy, bool bAccepted, bool bStillValid)
{
	PlayerTypes eSeat = ClaudeSeat();
	if(eSeat == NO_PLAYER || ClaudeSeatOnScreen() || (eFrom != eSeat && eTo != eSeat)) return;
	if(eActBy == eSeat) return; // the tool that acted already reported the result
	PlayerTypes eOther = (eFrom == eSeat) ? eTo : eFrom;
	std::string what = (eFrom == eSeat) ? "your offer" : "their offer";
	std::string text;
	if(!bStillValid) text = CivName(eOther) + ": " + what + " expired; something in it is no longer available";
	else if(bAccepted) text = CivName(eOther) + " accepted " + what + "; the deal is in force";
	else text = CivName(eOther) + " declined " + what;
	ClaudeBridge::Get().NotePopup(CLAUDE_POPUP_TEXT, 0, 0, 0, false, text, false);
}

bool ClaudeInterceptRequest(PlayerTypes eFrom, PlayerTypes eTo, DiploUIStateTypes eState, const char* szMessage, int iExtraGameData)
{
	if(DropForIdleHuman(eTo)) return true;
	if(!IsOffScreenSeat(eTo)) return false;
	Record(eFrom, eState, szMessage, iExtraGameData, NULL);
	return true;
}

bool ClaudeInterceptDealRequest(PlayerTypes eFrom, PlayerTypes eTo, const CvDeal* pDeal, DiploUIStateTypes eState, const char* szMessage)
{
	if(DropForIdleHuman(eTo)) return true;
	if(!IsOffScreenSeat(eTo)) return false;
	Record(eFrom, eState, szMessage, -1, pDeal);
	return true;
}

namespace
{
	// Humans with a deal waiting between them and the seat (the game keeps one per direction).
	// list NULL just counts the ones waiting for the seat's answer.
	int ProposedDealsWithSeat(JArr* list)
	{
		int n = 0;
		PlayerTypes eSeat = ClaudeSeat();
		if(eSeat == NO_PLAYER) return 0;
		CvGameDeals* pDeals = GC.getGame().GetGameDeals();
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(e == eSeat || !GET_PLAYER(e).isAlive() || !GET_PLAYER(e).isHuman()) continue;
			if(pDeals->ProposedDealExists(e, eSeat))
			{
				++n;
				if(list)
				{
					JObj o;
					o["from"] = J(CivName(e));
					o["kind"] = J("human_deal_offer");
					o["deal"] = DealJson(*pDeals->GetProposedDeal(e, eSeat));
					o["choices"] = J("answer_deal with civ and accept true or false");
					list->push_back(JVal(o));
				}
			}
			if(list && pDeals->ProposedDealExists(eSeat, e))
			{
				JObj o;
				o["to"] = J(CivName(e));
				o["kind"] = J("your_offer_awaiting_their_answer");
				o["deal"] = DealJson(*pDeals->GetProposedDeal(eSeat, e));
				o["choices"] = J("wait, or answer_deal with civ and withdraw true");
				list->push_back(JVal(o));
			}
		}
		return n;
	}
}

picojson::value ClaudeDiploRequestsJson()
{
	JArr list;
	for(size_t i = 0; i < s_requests.size(); ++i) list.push_back(RequestJson(s_requests[i]));
	ProposedDealsWithSeat(&list);
	return JVal(list);
}

int ClaudeCountDiploRequests()
{
	return (int)s_requests.size() + ProposedDealsWithSeat(NULL);
}

CLAUDE_TOOL(ToolAnswerDeal)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvPlayer* other = OtherMajor(me, args, err);
	if(!other) return JVal();
	if(!other->isHuman()) { err = "only a human proposes deals this way; computer leaders' offers are answered with respond_to_request"; return JVal(); }
	CvGameDeals* pDeals = GC.getGame().GetGameDeals();
	JObj o;
	if(ArgBool(args, "withdraw", false))
	{
		if(!pDeals->ProposedDealExists(me->GetID(), other->GetID())) { err = "you have no offer waiting with them"; return JVal(); }
		o["deal"] = DealJson(*pDeals->GetProposedDeal(me->GetID(), other->GetID()));
		pDeals->DoPlayerDealFinalized(me->GetID(), other->GetID(), me->GetID(), false);
		o["ok"] = J(true);
		o["answered"] = J("withdrawn");
		return JVal(o);
	}
	if(!pDeals->ProposedDealExists(other->GetID(), me->GetID())) { err = "they have no deal waiting for your answer"; return JVal(); }
	bool bAccept = ArgBool(args, "accept", false);
	o["deal"] = DealJson(*pDeals->GetProposedDeal(other->GetID(), me->GetID()));
	if(!pDeals->DoPlayerDealFinalized(other->GetID(), me->GetID(), me->GetID(), bAccept)) { err = "the deal expired: something in it is no longer available"; return JVal(); }
	o["ok"] = J(true);
	o["answered"] = J(bAccept ? "accepted" : "declined");
	return JVal(o);
}

void ClaudeResetDiploRequests()
{
	s_requests.clear();
	s_seq = 0;
}

// ------------------------------------------------------------- tools

CLAUDE_TOOL(ToolDiplomacy)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	JObj o;
	o["requests"] = ClaudeDiploRequestsJson();
	JArr rel;
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
	{
		CvPlayer& other = GET_PLAYER((PlayerTypes)i);
		if(!other.isAlive() || other.GetID() == me->GetID() || !GET_TEAM(me->getTeam()).isHasMet(other.getTeam())) continue;
		JObj r;
		r["civilization"] = J(CivName(other.GetID()));
		r["human"] = J(other.isHuman());
		r["relations"] = RelationJson(me, other);
		rel.push_back(JVal(r));
	}
	o["civilizations"] = JVal(rel);
	JArr deals;
	CvGameDeals* pDeals = GC.getGame().GetGameDeals();
	int iTurn = GC.getGame().getGameTurn();
	for(uint i = 0; i < pDeals->GetNumCurrentDeals(me->GetID()); ++i)
	{
		CvDeal* d = pDeals->GetCurrentDeal(me->GetID(), i);
		if(!d) continue;
		JObj e;
		e["deal"] = DealJson(*d);
		if(d->m_iFinalTurn > 0) e["turns_left"] = J(d->m_iFinalTurn - iTurn);
		deals.push_back(JVal(e));
	}
	o["current_deals"] = JVal(deals);
	return JVal(o);
}

CLAUDE_TOOL(ToolRespondToRequest)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	int id = 0;
	if(!RequireInt(args, "id", id, err)) return JVal();
	std::string choice = ArgStr(args, "choice");
	DiploRequest* r = FindRequest(id);
	if(!r) { err = ClaudeFormat("no open request #%d; see diplomacy.requests", id); return JVal(); }
	if(!GET_PLAYER(r->from).isAlive()) { err = CivName(r->from) + " is gone; the request no longer matters"; EraseRequest(r->id); return JVal(); }
	CvDiplomacyAI* theirAI = GET_PLAYER(r->from).GetDiplomacyAI();
	JObj o;
	if(r->hasDeal)
	{
		if(choice == "accept")
		{
			CvGameDeals* pDeals = GC.getGame().GetGameDeals();
			pDeals->AddProposedDeal(r->deal);
			if(!pDeals->FinalizeDeal(r->deal.m_eFromPlayer, r->deal.m_eToPlayer, true)) { err = "the deal could not be completed; something in it is no longer available"; EraseRequest(r->id); return JVal(); }
			o["deal"] = DealJson(r->deal);
		}
		else if(choice == "decline")
		{
			if(r->state == DIPLO_UI_STATE_TRADE_AI_MAKES_DEMAND)
				theirAI->DoFromUIDiploEvent(me->GetID(), FROM_UI_DIPLO_EVENT_DEMAND_HUMAN_REFUSAL, 0, -1);
			else if(r->state == DIPLO_UI_STATE_TRADE_AI_MAKES_REQUEST)
			{
				int a = 0, b = 0;
				int value = GET_PLAYER(r->from).GetDealAI()->GetDealValue(&r->deal, a, b, false);
				theirAI->DoFromUIDiploEvent(me->GetID(), FROM_UI_DIPLO_EVENT_REQUEST_HUMAN_REFUSAL, value, -1);
			}
		}
		else { err = "choice must be accept or decline"; return JVal(); }
	}
	else
	{
		TwoWay tw;
		FromUIDiploEventTypes ev = NO_FROM_UI_DIPLO_EVENT;
		int a1 = 0, a2 = -1;
		if(r->state == DIPLO_UI_STATE_DISCUSS_COOP_WAR)
		{
			ev = FROM_UI_DIPLO_EVENT_COOP_WAR_RESPONSE;
			a2 = r->extra;
			if(choice == "accept") a1 = 4; else if(choice == "later") a1 = 3; else if(choice == "decline") a1 = 2;
			else { err = "choice must be accept, later or decline"; return JVal(); }
		}
		else if(TwoWayFor(r->state, tw))
		{
			ev = tw.event;
			if(tw.passesExtra) a2 = r->extra;
			if(choice == tw.first) a1 = 1; else if(choice == tw.second) a1 = 2;
			else { err = std::string("choice must be ") + tw.first + " or " + tw.second; return JVal(); }
		}
		else if(choice != "acknowledge") { err = "this request only takes acknowledge"; return JVal(); }
		else if(!AcknowledgeEvent(r->state, ev))
			ClaudeBridge::Get().LogLine(ClaudeFormat("[diplomacy #%d] no reply event for state %d; nothing sent to the game", r->id, (int)r->state));
		if(ev != NO_FROM_UI_DIPLO_EVENT) theirAI->DoFromUIDiploEvent(me->GetID(), ev, a1, a2);
	}
	o["ok"] = J(true);
	o["answered"] = J(choice);
	o["relations"] = RelationJson(me, GET_PLAYER(r->from));
	EraseRequest(r->id);
	return JVal(o);
}

CLAUDE_TOOL(ToolProposeDeal)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvPlayer* other = OtherMajor(me, args, err);
	if(!other) return JVal();
	if(GET_TEAM(me->getTeam()).isAtWar(other->getTeam())) { err = "you are at war with them; use make_peace first"; return JVal(); }
	CvDeal deal(me->GetID(), other->GetID());
	JObj::const_iterator g = args.find("give"), t = args.find("get");
	if(g != args.end() && g->second.is<JArr>() && !AddItems(deal, me->GetID(), other->GetID(), g->second.get<JArr>(), err)) return JVal();
	if(t != args.end() && t->second.is<JArr>() && !AddItems(deal, other->GetID(), me->GetID(), t->second.get<JArr>(), err)) return JVal();
	if(deal.GetNumItems() == 0) { err = "the deal is empty; list items in give and/or get"; return JVal(); }
	bool bPreview = ArgBool(args, "preview_only", false);
	if(other->isHuman())
	{
		JObj o;
		o["deal"] = DealJson(deal);
		if(bPreview) { o["note"] = J("a human decides on their own trade screen"); return JVal(o); }
		if(!OfferDealToHuman(me, other, deal, o, err)) return JVal();
		return JVal(o);
	}
	return JudgeDeal(me, *other, deal, !bPreview, err);
}

CLAUDE_TOOL(ToolMakePeace)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvPlayer* other = OtherMajor(me, args, err);
	if(!other) return JVal();
	CvTeam& mine = GET_TEAM(me->getTeam());
	if(!mine.isAtWar(other->getTeam())) { err = "you are not at war with them"; return JVal(); }
	if(!mine.canChangeWarPeace(other->getTeam())) { err = "peace is not possible yet (locked into war)"; return JVal(); }
	int dur = GC.getGame().getGameSpeedInfo().getPeaceDealDuration();
	CvDeal deal(me->GetID(), other->GetID());
	bool bPreview = ArgBool(args, "preview_only", false);
	if(other->isHuman())
	{
		deal.SetPeaceTreatyType(PEACE_TREATY_WHITE_PEACE);
		deal.AddPeaceTreaty(other->GetID(), dur);
		deal.AddPeaceTreaty(me->GetID(), dur);
		JObj o;
		o["deal"] = DealJson(deal);
		if(!bPreview && !OfferDealToHuman(me, other, deal, o, err)) return JVal();
		return JVal(o);
	}
	CvDiplomacyAI* d = other->GetDiplomacyAI();
	CvDealAI* ai = other->GetDealAI();
	PeaceTreatyTypes eOffer = d->GetTreatyWillingToOffer(me->GetID());
	PeaceTreatyTypes eAccept = d->GetTreatyWillingToAccept(me->GetID());
	if(!d->IsWillingToMakePeaceWithHuman(me->GetID()) || eOffer < PEACE_TREATY_WHITE_PEACE || eAccept < PEACE_TREATY_WHITE_PEACE)
	{
		err = "they are not willing to make peace yet";
		return JVal();
	}
	// DoEqualizeDealWithHuman rebuilds a peace deal from scratch; it only needs the
	// treaty items to recognise one, and the price it reads from the cache seeded here.
	CvDeal valuation(me->GetID(), other->GetID());
	int iTheyGive = 0, iWeGive = 0;
	if(eOffer > PEACE_TREATY_WHITE_PEACE)
	{
		ai->DoAddItemsToDealForPeaceTreaty(me->GetID(), &valuation, eOffer, true);
		ai->GetDealValue(&valuation, iTheyGive, iWeGive, false);
		ai->SetCachedValueOfPeaceWithHuman(-iTheyGive);
	}
	else if(eAccept > PEACE_TREATY_WHITE_PEACE)
	{
		ai->DoAddItemsToDealForPeaceTreaty(me->GetID(), &valuation, eAccept, false);
		ai->GetDealValue(&valuation, iTheyGive, iWeGive, false);
		ai->SetCachedValueOfPeaceWithHuman(iWeGive);
	}
	else ai->SetCachedValueOfPeaceWithHuman(0);
	deal.AddPeaceTreaty(other->GetID(), dur);
	deal.AddPeaceTreaty(me->GetID(), dur);
	bool bGood = false, bCant = false;
	ai->DoEqualizeDealWithHuman(&deal, me->GetID(), false, false, bGood, bCant);
	return JudgeDeal(me, *other, deal, !bPreview, err);
}

CLAUDE_TOOL(ToolDemand)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvPlayer* other = OtherMajor(me, args, err);
	if(!other) return JVal();
	if(other->isHuman()) { err = "demands only work on the game's AI; propose a deal to a human"; return JVal(); }
	if(GET_TEAM(me->getTeam()).isAtWar(other->getTeam())) { err = "you are at war with them"; return JVal(); }
	CvDeal deal(me->GetID(), other->GetID());
	JObj::const_iterator t = args.find("get");
	if(t == args.end() || !t->second.is<JArr>() || !AddItems(deal, other->GetID(), me->GetID(), t->second.get<JArr>(), err)) { if(err.empty()) err = "get must list what you demand"; return JVal(); }
	if(deal.GetNumItems() == 0) { err = "the demand is empty"; return JVal(); }
	deal.SetDemandingPlayer(me->GetID());
	DemandResponseTypes r = other->GetDealAI()->DoHumanDemand(&deal, me->GetID());
	JObj o;
	o["ok"] = J(true);
	switch(r)
	{
	case DEMAND_RESPONSE_ACCEPT: o["response"] = J("they gave in"); break;
	case DEMAND_RESPONSE_REFUSE_WEAK: o["response"] = J("refused: they do not fear you"); break;
	case DEMAND_RESPONSE_REFUSE_HOSTILE: o["response"] = J("refused: they are hostile to you"); break;
	case DEMAND_RESPONSE_REFUSE_TOO_MUCH: o["response"] = J("refused: too much"); break;
	case DEMAND_RESPONSE_REFUSE_TOO_SOON: o["response"] = J("refused: you demanded too recently"); break;
	default: o["response"] = J("refused"); break;
	}
	o["deal"] = DealJson(deal);
	return JVal(o);
}

CLAUDE_TOOL(ToolDiscuss)
{
	CvPlayer* me = MeActing(err);
	if(!me) return JVal();
	CvPlayer* other = OtherMajor(me, args, err);
	if(!other) return JVal();
	std::string topic = ArgStr(args, "topic");
	if(other->isHuman())
	{
		if(topic == "denounce") me->GetDiplomacyAI()->DoDenouncePlayer(other->GetID());
		else { err = "with a human only denounce works here; friendship is a deal item (declaration_of_friendship) and the rest is talk"; return JVal(); }
	}
	else
	{
		FromUIDiploEventTypes ev;
		int a1 = 0;
		if(topic == "ask_friendship") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_WORK_WITH_US;
		else if(topic == "end_friendship") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_END_WORK_WITH_US;
		else if(topic == "denounce") ev = FROM_UI_DIPLO_EVENT_DENOUNCE;
		else if(topic == "stop_settling") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_DONT_SETTLE;
		else if(topic == "stop_spying") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_STOP_SPYING;
		else if(topic == "stop_converting") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_STOP_SPREADING_RELIGION;
		else if(topic == "stop_digging") ev = FROM_UI_DIPLO_EVENT_HUMAN_DISCUSSION_STOP_DIGGING;
		else if(topic == "joint_war")
		{
			PlayerTypes eTarget = FindPlayer(ArgStr(args, "target"));
			if(eTarget == NO_PLAYER || eTarget == me->GetID() || eTarget == other->GetID()) { err = "joint_war needs target: the civilization to fight"; return JVal(); }
			ev = FROM_UI_DIPLO_EVENT_COOP_WAR_OFFER;
			a1 = eTarget;
		}
		else { err = "unknown topic '" + topic + "'; use ask_friendship, end_friendship, denounce, stop_settling, stop_spying, stop_converting, stop_digging or joint_war"; return JVal(); }
		other->GetDiplomacyAI()->DoFromUIDiploEvent(me->GetID(), ev, a1, -1);
	}
	JObj o;
	o["ok"] = J(true);
	o["relations"] = RelationJson(me, *other);
	if(!other->isHuman())
	{
		CvDiplomacyAI* d = other->GetDiplomacyAI();
		if(topic == "stop_settling") o["they_agreed"] = J(d->IsPlayerNoSettleRequestAccepted(me->GetID()));
		if(topic == "stop_spying") o["they_agreed"] = J(d->IsPlayerStopSpyingRequestAccepted(me->GetID()));
		if(topic == "ask_friendship") o["they_agreed"] = J(d->IsDoFAccepted(me->GetID()));
	}
	return JVal(o);
}
