-- Popup that shows the log written by the Claude Player DLL (Game.GetClaudeLog).
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

ContextPtr:SetUpdate(function(fDeltaTime)
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
