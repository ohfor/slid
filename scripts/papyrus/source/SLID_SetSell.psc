ScriptName SLID_SetSell extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.BeginSellContainer()
EndEvent
