-- A Declaration of Friendship with anyone on a team blocks war on that team, and a denounce
-- gives its target a few turns' notice before war. The game asks this before any war
-- declaration (the war button, attacking a unit, the AI's own planning).
print("Loading Reduced Aggression")

local TURNS_AFTER_DENOUNCE_STANDARD_SPEED = 5

local function TurnsAfterDenounce()
	local speed = GameInfo.GameSpeeds[Game.GetGameSpeedType()]
	return math.max(1, math.floor(TURNS_AFTER_DENOUNCE_STANDARD_SPEED * speed.OpinionDurationPercent / 100))
end

local function MajorsOnTeam(iTeam)
	local members = {}
	for iPlayer = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local pPlayer = Players[iPlayer]
		if pPlayer:IsAlive() and pPlayer:GetTeam() == iTeam then
			table.insert(members, pPlayer)
		end
	end
	return members
end

local function CanDeclareWar(iTeam, iOtherTeam)
	local iTurnsAfterDenounce = TurnsAfterDenounce()
	local targets = MajorsOnTeam(iOtherTeam)
	for _, pPlayer in ipairs(MajorsOnTeam(iTeam)) do
		for _, pTarget in ipairs(targets) do
			local iTarget = pTarget:GetID()
			if pPlayer:IsDoF(iTarget) then
				return false
			end
			local iTurnsSinceDenounce = pPlayer:GetDenouncedPlayerCounter(iTarget)
			if iTurnsSinceDenounce >= 0 and iTurnsSinceDenounce < iTurnsAfterDenounce then
				return false
			end
		end
	end
	return true
end

GameEvents.CanDeclareWar.Add(CanDeclareWar)
