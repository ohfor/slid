#include "MouseGlow.h"

namespace MouseGlow {

    void Create(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                double a_maskX, double a_maskY, double a_maskW, double a_maskH) {
        if (!a_movie) return;

        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        // Create the glow clip (gradient drawn at origin; positioned via _x/_y)
        RE::GFxValue clip;
        RE::GFxValue cArgs[2];
        cArgs[0].SetString(a_name);
        cArgs[1].SetNumber(static_cast<double>(a_depth));
        root.Invoke("createEmptyMovieClip", &clip, cArgs, 2);
        if (clip.IsUndefined()) return;

        // Radial gradient: center color -> transparent
        RE::GFxValue colors, alphas, ratios;
        a_movie->CreateArray(&colors);
        a_movie->CreateArray(&alphas);
        a_movie->CreateArray(&ratios);

        RE::GFxValue v;
        v.SetNumber(static_cast<double>(COLOR));
        colors.PushBack(v); colors.PushBack(v);
        v.SetNumber(ALPHA);  alphas.PushBack(v);
        v.SetNumber(0.0);    alphas.PushBack(v);
        v.SetNumber(0.0);    ratios.PushBack(v);
        v.SetNumber(255.0);  ratios.PushBack(v);

        RE::GFxValue matrix;
        a_movie->CreateObject(&matrix);
        RE::GFxValue sv; sv.SetString("box");
        matrix.SetMember("matrixType", sv);
        v.SetNumber(-RADIUS); matrix.SetMember("x", v);
        v.SetNumber(-RADIUS); matrix.SetMember("y", v);
        v.SetNumber(RADIUS * 2.0); matrix.SetMember("w", v);
        v.SetNumber(RADIUS * 2.0); matrix.SetMember("h", v);
        v.SetNumber(0.0); matrix.SetMember("r", v);

        RE::GFxValue gradArgs[5];
        gradArgs[0].SetString("radial");
        gradArgs[1] = colors; gradArgs[2] = alphas;
        gradArgs[3] = ratios; gradArgs[4] = matrix;
        clip.Invoke("beginGradientFill", nullptr, gradArgs, 5);

        RE::GFxValue pt[2];
        pt[0].SetNumber(-RADIUS); pt[1].SetNumber(-RADIUS);
        clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(RADIUS);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(RADIUS);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(-RADIUS);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(-RADIUS);
        clip.Invoke("lineTo", nullptr, pt, 2);
        clip.Invoke("endFill", nullptr, nullptr, 0);

        // Mask clip: white-filled rectangle at panel bounds
        std::string maskName = std::string(a_name) + "_mask";
        RE::GFxValue mask;
        RE::GFxValue mArgs[2];
        mArgs[0].SetString(maskName.c_str());
        mArgs[1].SetNumber(static_cast<double>(a_depth + 1));
        root.Invoke("createEmptyMovieClip", &mask, mArgs, 2);
        if (!mask.IsUndefined()) {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(0xFFFFFF);
            fillArgs[1].SetNumber(100.0);
            mask.Invoke("beginFill", nullptr, fillArgs, 2);

            pt[0].SetNumber(a_maskX); pt[1].SetNumber(a_maskY);
            mask.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(a_maskX + a_maskW);
            mask.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(a_maskY + a_maskH);
            mask.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(a_maskX);
            mask.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(a_maskY);
            mask.Invoke("lineTo", nullptr, pt, 2);
            mask.Invoke("endFill", nullptr, nullptr, 0);

            RE::GFxValue maskArg[1];
            maskArg[0] = mask;
            clip.Invoke("setMask", nullptr, maskArg, 1);
        }
    }

    void SetPosition(RE::GFxMovieView* a_movie, const char* a_name, double a_x, double a_y) {
        if (!a_movie) return;

        std::string path = std::string("_root.") + a_name;
        RE::GFxValue clip;
        a_movie->GetVariable(&clip, path.c_str());
        if (clip.IsUndefined()) return;

        RE::GFxValue v;
        v.SetNumber(a_x); clip.SetMember("_x", v);
        v.SetNumber(a_y); clip.SetMember("_y", v);
    }

    void Destroy(RE::GFxMovieView* a_movie, const char* a_name) {
        if (!a_movie) return;

        auto removeClip = [&](const std::string& a_path) {
            RE::GFxValue clip;
            a_movie->GetVariable(&clip, a_path.c_str());
            if (clip.IsDisplayObject()) {
                clip.Invoke("removeMovieClip");
            }
        };

        removeClip(std::string("_root.") + a_name);
        removeClip(std::string("_root.") + a_name + "_mask");
    }

}
