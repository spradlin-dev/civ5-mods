print('Loading AutoEndTurnShortcut mod')

function Toggle()
    local autoEndTurnOption = OptionsManager.GetSinglePlayerAutoEndTurnEnabled_Cached()
    local newOptionValue = not autoEndTurnOption
    OptionsManager.SetSinglePlayerAutoEndTurnEnabled_Cached(newOptionValue)
    OptionsManager.SetMultiplayerAutoEndTurnEnabled_Cached(newOptionValue)
    OptionsManager.CommitGameOptions()
    ShowNotification(newOptionValue)
end

function ShowNotification(autoEndTurnEnabled)
    local title = ""
    local message = ""

    if autoEndTurnEnabled then
        title = "Auto End Turn Enabled"
        message = "Auto End Turn is now enabled."
    else
        title = "Auto End Turn Disabled"
        message = "Auto End Turn is now disabled."
    end

    local notifType = -1
    local player = Players[Game.GetActivePlayer()]

    if player then
        player:AddNotification(notifType, message, title, -1, -1, -1, -1)
    end
end

function InputHandler( uiMsg, wParam, lParam )
    if uiMsg == KeyEvents.KeyDown then
        local bShift = UIManager:GetShift();
        local bAlt = UIManager:GetAlt();
        local bCtrl = UIManager:GetControl();
        if wParam == Keys.T and bCtrl and bShift then
            Toggle();
            return true;
        end
    end
end

ContextPtr:SetInputHandler( InputHandler )