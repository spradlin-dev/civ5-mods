-- Local MCP Server DLL UI add-in: the log popup (Game.GetLLMLog) and popup capture.
-- Every game popup is reported to the DLL (Game.LLMNotePopup). It stays open
-- until the model acknowledges it; the DLL then names the dialog in Game.LLMDialogsToClose.
include("InstanceManager");

local MAX_LINES_SHOWN = 400;

-- The DLL tags every log entry with one of these categories (Claude/ClaudeLogText.h);
-- entries tagged "always" are shown whatever is filtered.
local CATEGORIES = {
	{ key = "reasoning", label = "Notes" },
	{ key = "turn", label = "Turns" },
	{ key = "movement", label = "Military moves" },
	{ key = "military", label = "Military orders" },
	{ key = "combat", label = "Combat" },
	{ key = "workers", label = "Workers" },
	{ key = "settlers", label = "Settlers" },
	{ key = "civilian", label = "Other civilians" },
	{ key = "city", label = "Cities" },
	{ key = "empire", label = "Research, policies" },
	{ key = "diplomacy", label = "Diplomacy" },
	{ key = "messages", label = "Game messages" },
	{ key = "notifications", label = "Notifications" },
};

local g_Lines = InstanceManager:new("LineInstance", "Text", Controls.LineStack);
local g_Filters = InstanceManager:new("FilterInstance", "Root", Controls.FilterStack);
local g_Settings = Modding.OpenUserData("LocalMCPServerDLL_Log", 1);
local g_Shown = {};
local g_LastSeq = -1;
local g_LastHidden = nil;
local g_SinceRefresh = 0;
local g_SinceIdentity = 0;
local g_IdentityLabel = nil;

-- A connected model names itself (set_identity), e.g. Claude / Opus / 5.5. The button
-- carries that name while exactly one model has; "AI" otherwise.
local function UpdateIdentity()
	local count, model, label = Game.GetLLMIdentity();
	if count ~= 1 then
		label = (count > 1) and string.format("%d AI players", count) or "";
	end
	if label == g_IdentityLabel then
		return;
	end
	g_IdentityLabel = label;
	local short = (count == 1 and model ~= "") and string.sub(model, 1, 12) or "AI";
	Controls.OpenButtonLabel:SetText(short .. " Log");
	Controls.OpenButton:SetToolTipString("Show what " .. short .. " is thinking");
	Controls.Title:SetText(((label ~= "") and label or "AI") .. " Log");
end

-- bToBottom: jump to the newest line; otherwise only follow it if the reader is already there.
-- Lines are prefixed with the writer's name and civilization once more than one model has written.
local function WriterPrefix(entries)
	local writers = {};
	local count = 0;
	for _, entry in ipairs(entries) do
		if entry.Who ~= "" and not writers[entry.Who] then
			writers[entry.Who] = true;
			count = count + 1;
		end
	end
	if count < 2 then
		return function() return ""; end;
	end
	return function(entry)
		if entry.Who == "" then
			return "";
		elseif entry.Civ ~= "" then
			return string.format("%s (%s): ", entry.Who, entry.Civ);
		end
		return string.format("%s: ", entry.Who);
	end;
end

local function Refresh(bToBottom)
	local seq, entries, hidden = Game.GetLLMLog();
	if seq == g_LastSeq and hidden == g_LastHidden then
		return;
	end
	g_LastSeq = seq;
	g_LastHidden = hidden;
	local bFollow = bToBottom or Controls.ScrollPanel:GetRatio() >= 1 or Controls.ScrollPanel:GetScrollValue() > 0.99;

	local lines = {};
	if hidden > 0 then
		if hidden == 1 then
			table.insert(lines, "One AI player is your opponent, so its log is hidden.");
		else
			table.insert(lines, string.format("%d AI players are your opponents, so their logs are hidden.", hidden));
		end
		table.insert(lines, "Their full thinking is in the windows they run in, and in AIThinking.log in the game's Logs folder.");
	end
	entries = entries or {};
	local prefix = WriterPrefix(entries);
	for _, entry in ipairs(entries) do
		if entry.Category == "always" or g_Shown[entry.Category] ~= false then
			local text = prefix(entry) .. entry.Text;
			repeat
				local before = text;
				text = text:gsub("%[NEWLINE%]%s*%[NEWLINE%]", "[NEWLINE]"):gsub("%s*%[NEWLINE%]%s*$", "");
			until text == before;
			for line in string.gmatch(text .. "\n", "(.-)\n") do
				table.insert(lines, line);
			end
		end
	end

	g_Lines:ResetInstances();
	if #lines == 0 then
		g_Lines:GetInstance().Text:SetText("(nothing logged yet)");
	else
		local first = math.max(1, #lines - MAX_LINES_SHOWN + 1);
		if first > 1 then
			g_Lines:GetInstance().Text:SetText(string.format("(%d older lines not shown)", first - 1));
		end
		for i = first, #lines do
			local line = lines[i];
			if line == "" then
				line = " ";
			end
			g_Lines:GetInstance().Text:SetText(line);
		end
	end

	Controls.LineStack:CalculateSize();
	Controls.LineStack:SetSizeY(Controls.LineStack:GetSizeY() + 8); -- room for the last line's descenders
	Controls.LineStack:ReprocessAnchoring();
	Controls.ScrollPanel:CalculateInternalSize();
	if bFollow then
		Controls.ScrollPanel:SetScrollValue(1);
	end
end

for _, category in ipairs(CATEGORIES) do
	local key = category.key;
	g_Shown[key] = tostring(g_Settings.GetValue("Show_" .. key)) ~= "0";
	local filter = g_Filters:GetInstance();
	filter.Box:GetTextButton():SetText(category.label);
	filter.Box:SetCheck(g_Shown[key]);
	filter.Box:RegisterCheckHandler(function(bChecked)
		g_Shown[key] = bChecked;
		g_Settings.SetValue("Show_" .. key, bChecked and "1" or "0");
		g_LastSeq = -1;
		Refresh(true);
	end);
end
Controls.FilterStack:CalculateSize();
Controls.FilterStack:ReprocessAnchoring();

local function Open()
	g_LastSeq = -1;
	Refresh(true);
	Controls.Popup:SetHide(false);
end

local function Close()
	Controls.Popup:SetHide(true);
end

Controls.OpenButton:RegisterCallback(Mouse.eLClick, Open);
Controls.CloseButton:RegisterCallback(Mouse.eLClick, Close);

local g_PendingDismiss = {};

local function GoodyText(popupInfo)
	local goody = GameInfo.GoodyHuts[popupInfo.Data1];
	if goody == nil then
		return "";
	end
	local text = Locale.ConvertTextKey(goody.Description);
	if goody.Gold > 0 or goody.Culture > 0 then
		text = Locale.ConvertTextKey(goody.Description, popupInfo.Data2);
	end
	return text;
end

local function OnGamePopup(popupInfo)
	local text = popupInfo.Text or "";
	if popupInfo.Type == ButtonPopupTypes.BUTTONPOPUP_GOODY_HUT_REWARD then
		text = GoodyText(popupInfo);
	elseif popupInfo.Type == ButtonPopupTypes.BUTTONPOPUP_CITY_STATE_MESSAGE then
		local message = UI.GetTempString();
		if message ~= nil and message ~= "" then
			text = message;
		end
	end
	Game.LLMNotePopup(popupInfo.Type, popupInfo.Data1 or -1, popupInfo.Data2 or -1, popupInfo.Data3 or -1, popupInfo.Option1 == true, text);
end
Events.SerialEventGameMessagePopup.Add(OnGamePopup);

local function CollectAcknowledged()
	local names = Game.LLMDialogsToClose();
	if names == nil or names == "" then
		return;
	end
	for name in string.gmatch(names, "[^\n]+") do
		if #g_PendingDismiss >= 50 then
			print("LLMLog: too many dialogs waiting to be closed; dropping " .. g_PendingDismiss[1].name);
			table.remove(g_PendingDismiss, 1);
		end
		table.insert(g_PendingDismiss, { name = name, waited = 0 });
	end
end

-- An acknowledged dialog may still be waiting behind other dialogs, so each entry
-- stays until its dialog has actually been shown and closed. If the human closed
-- it by hand it never shows again; the entry is dropped so it cannot close the
-- next dialog of that kind unread. A dialog stuck behind others for that long is
-- left for the human to close, which is the safer failure.
local MAX_DISMISS_WAIT_SECONDS = 10;

local function DismissPending(fDeltaTime)
	local remaining = {};
	local closed = {};
	for _, pending in ipairs(g_PendingDismiss) do
		local control = ContextPtr:LookUpControl("/InGame/" .. pending.name);
		if control == nil then
			print("LLMLog: no dialog named " .. pending.name);
		elseif not control:IsHidden() and not closed[pending.name] then
			UIManager:DequeuePopup(control);
			closed[pending.name] = true;
		else
			pending.waited = pending.waited + fDeltaTime;
			if pending.waited > MAX_DISMISS_WAIT_SECONDS then
				print("LLMLog: " .. pending.name .. " was not shown within " .. MAX_DISMISS_WAIT_SECONDS .. " s; dropping it");
			else
				table.insert(remaining, pending);
			end
		end
	end
	g_PendingDismiss = remaining;
end

UpdateIdentity();

local MAX_MOD_DESCRIPTION = 400;

-- One line per mod: "Name (v N)", a tab, then the description flattened to one line.
local function ReportActiveMods()
	local mods = {};
	for _, v in ipairs(Modding.GetActivatedMods()) do
		local name = Modding.GetModProperty(v.ID, v.Version, "Name") or v.ID;
		local description = Modding.GetModProperty(v.ID, v.Version, "Description") or "";
		description = description:gsub("%[NEWLINE%]", " "):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "");
		if #description > MAX_MOD_DESCRIPTION then
			description = description:sub(1, MAX_MOD_DESCRIPTION) .. "...";
		end
		table.insert(mods, string.format("%s (v %d)\t%s", (name:gsub("%s+", " ")), v.Version, description));
	end
	Game.LLMSetActiveMods(table.concat(mods, "\n"));
end
ReportActiveMods();

ContextPtr:SetUpdate(function(fDeltaTime)
	CollectAcknowledged();
	g_SinceIdentity = g_SinceIdentity + fDeltaTime;
	if g_SinceIdentity >= 1.0 then
		g_SinceIdentity = 0;
		UpdateIdentity();
	end
	if #g_PendingDismiss > 0 then
		DismissPending(fDeltaTime);
	end
	if Controls.Popup:IsHidden() then
		return;
	end
	g_SinceRefresh = g_SinceRefresh + fDeltaTime;
	if g_SinceRefresh >= 1.0 then
		g_SinceRefresh = 0;
		Refresh();
	end
end);

ContextPtr:SetInputHandler(function(uiMsg, wParam, lParam)
	if uiMsg == KeyEvents.KeyDown and wParam == Keys.VK_ESCAPE and not Controls.Popup:IsHidden() then
		Close();
		return true;
	end
	return false;
end);
