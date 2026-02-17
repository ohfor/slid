ScriptName SLID_Reset extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.RemoveAllNetworks()
    SLID_Native.RefreshPowers()
    Debug.Notification("SLID: All networks removed, powers refreshed")
EndEvent
