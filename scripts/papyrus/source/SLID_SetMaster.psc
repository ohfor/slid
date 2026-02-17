ScriptName SLID_SetMaster extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.SetMasterAuto()
EndEvent
