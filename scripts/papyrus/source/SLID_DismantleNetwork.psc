ScriptName SLID_DismantleNetwork extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SLID_Native.BeginDismantleNetwork()
EndEvent
