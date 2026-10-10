-- Claude Player UI add-in: the log popup (Game.GetClaudeLog) and popup capture.
-- Every game popup is reported to the DLL (Game.ClaudeNotePopup). It stays open
-- until Claude acknowledges it; the DLL then names the dialog in Game.ClaudeDialogsToClose.
include("InstanceManager");

local MAX_LINES_SHOWN = 400;

local g_Lines = InstanceManager:new("LineInstance", "Text", Controls.LineStack);
local g_LastSeq = -1;
local g_SinceRefresh = 0;

local function Refresh()
	local seq, text = Game.GetClaudeLog();
	if seq == g_LastSeq then
		return;
	end
	g_LastSeq = seq;

	local lines = {};
	if text ~= nil and text ~= "" then
		for line in string.gmatch(text .. "\n", "(.-)\n") do
			table.insert(lines, line);
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
	Controls.LineStack:ReprocessAnchoring();
	Controls.ScrollPanel:CalculateInternalSize();
	Controls.ScrollPanel:SetScrollValue(1);
end

local function Open()
	g_LastSeq = -1;
	Refresh();
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
	Game.ClaudeNotePopup(popupInfo.Type, popupInfo.Data1 or -1, popupInfo.Data2 or -1, popupInfo.Data3 or -1, popupInfo.Option1 == true, text);
end
Events.SerialEventGameMessagePopup.Add(OnGamePopup);

local function CollectAcknowledged()
	local names = Game.ClaudeDialogsToClose();
	if names == nil or names == "" then
		return;
	end
	for name in string.gmatch(names, "[^\n]+") do
		if #g_PendingDismiss >= 50 then
			print("ClaudeLog: too many dialogs waiting to be closed; dropping " .. g_PendingDismiss[1].name);
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
			print("ClaudeLog: no dialog named " .. pending.name);
		elseif not control:IsHidden() and not closed[pending.name] then
			UIManager:DequeuePopup(control);
			closed[pending.name] = true;
		else
			pending.waited = pending.waited + fDeltaTime;
			if pending.waited > MAX_DISMISS_WAIT_SECONDS then
				print("ClaudeLog: " .. pending.name .. " was not shown within " .. MAX_DISMISS_WAIT_SECONDS .. " s; dropping it");
			else
				table.insert(remaining, pending);
			end
		end
	end
	g_PendingDismiss = remaining;
end

ContextPtr:SetUpdate(function(fDeltaTime)
	CollectAcknowledged();
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
