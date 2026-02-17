ScriptName SLID_ConjureMaster extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.BeginSummonChest()
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    SLID_Native.DespawnSummonChest()
EndEvent
