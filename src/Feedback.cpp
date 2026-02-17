#include "Feedback.h"

namespace Feedback {

    namespace {
        constexpr auto kPluginName = "SLID.esp"sv;
        constexpr RE::FormID kShaderWhite = 0x810;
        constexpr RE::FormID kShaderBlue  = 0x811;
        constexpr RE::FormID kShaderRed    = 0x812;
        constexpr RE::FormID kShaderOrange = 0x815;

        void PlayShader(RE::TESObjectREFR* a_target, RE::FormID a_localID, float a_duration) {
            if (!a_target) return;

            auto* shader = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESEffectShader>(a_localID, kPluginName);
            if (!shader) {
                logger::warn("Feedback: shader {:03X} not found in {}", a_localID, kPluginName);
                return;
            }

            a_target->ApplyEffectShader(shader, a_duration);
        }

        void ShakeController(const char* a_context, float a_left, float a_right, float a_duration) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                logger::warn("Feedback [{}]: VM not available", a_context);
                return;
            }

            auto args = RE::MakeFunctionArguments(std::move(a_left), std::move(a_right), std::move(a_duration));
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            vm->DispatchStaticCall("Game", "ShakeController", args, callback);

            logger::info("Feedback [{}]: Game.ShakeController({}, {}, {})", a_context, a_left, a_right, a_duration);
        }
    }

    void OnSetMaster(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderWhite, 1.5f);
        ShakeController("SetMaster", 0.3f, 0.3f, 0.3f);
    }

    void OnTagContainer(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderBlue, 1.5f);
        ShakeController("TagContainer", 0.3f, 0.3f, 0.15f);
    }

    void OnUntagContainer(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderRed, 1.5f);
        ShakeController("UntagContainer", 0.4f, 0.1f, 0.08f);
    }

    void OnDismantleNetwork(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderRed, 1.5f);
        ShakeController("Dismantle", 0.4f, 0.1f, 0.08f);
    }

    void OnDetectContainers() {
        ShakeController("DetectContainers", 0.3f, 0.3f, 0.3f);
    }

    void OnAutoDistribute() {
        ShakeController("AutoDistribute", 0.3f, 0.3f, 0.3f);
    }

    void OnWhoosh() {
        ShakeController("Whoosh", 0.3f, 0.3f, 0.3f);
    }

    void OnSort() {
        ShakeController("Sort", 0.3f, 0.3f, 0.3f);
    }

    void OnSetSellContainer(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderOrange, 1.5f);
        ShakeController("SetSellContainer", 0.3f, 0.3f, 0.3f);
    }

    void OnClearSellContainer(RE::TESObjectREFR* a_container) {
        PlayShader(a_container, kShaderRed, 1.5f);
        ShakeController("ClearSellContainer", 0.4f, 0.1f, 0.08f);
    }

    void OnError() {
        ShakeController("Error", 0.4f, 0.1f, 0.08f);
    }
}
