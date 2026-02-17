ScriptName SLID_Deregister extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.BeginDeregister()
EndEvent
