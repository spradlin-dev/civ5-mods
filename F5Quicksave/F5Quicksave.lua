print('Loading F5 Quicksave mod')

function InputHandler( uiMsg, wParam, lParam )


  if uiMsg == KeyEvents.KeyDown then
    if wParam == Keys.VK_F5 then
      UI.SaveGame("F5 QuickSave")
	  local player = Players[Game.GetActivePlayer()]
      player:AddNotification(-1, os.date("%c"), "Quicksave Complete", -1, -1, -1, -1)
    end	
  end
end

ContextPtr:SetInputHandler( InputHandler )