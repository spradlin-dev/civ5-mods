print('Loading DismissAllNotifications mod')

function DismissAllNotifications()
  local player = Players[Game.GetActivePlayer()]
  for i = 0, player:GetNumNotifications() - 1 do
    local notificationID = player:GetNotificationIndex(i)
    if notificationID ~= -1 then
      UI.RemoveNotification(notificationID)
    end
  end
end

function InputHandler( uiMsg, wParam, lParam )
  if uiMsg == KeyEvents.KeyDown then
    local bShift = UIManager:GetShift();
    local bAlt = UIManager:GetAlt();
    local bCtrl = UIManager:GetControl();

    if wParam == Keys.D and bCtrl and bShift then
      DismissAllNotifications();
      return true;
    end
  end
end

ContextPtr:SetInputHandler( InputHandler )