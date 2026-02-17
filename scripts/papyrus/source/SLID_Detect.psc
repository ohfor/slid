ScriptName SLID_Detect extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.BeginDetect()
EndEvent
