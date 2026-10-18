print('Loading AutoCycleUnitShortcut mod')

function Toggle()
    local autoCycleUnitOption = OptionsManager.GetAutoUnitCycle_Cached()
    local newOptionValue = not autoCycleUnitOption
    OptionsManager.SetAutoUnitCycle_Cached(newOptionValue)
    OptionsManager.CommitGameOptions()
    ShowNotification(newOptionValue)
end

function ShowNotification(autoEndTurnEnabled)
    local title = ""
    local message = ""

    if autoEndTurnEnabled then
        title = "Auto Cycle Units Enabled"
        message = "Auto Cycle Units is now enabled."
    else
        title = "Auto Cycle Units Disabled"
        message = "AutoCycle Units is now disabled."
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
        if wParam == Keys.C and bCtrl and bShift then
            Toggle();
            return true;
        end
    end
end

ContextPtr:SetInputHandler( InputHandler )