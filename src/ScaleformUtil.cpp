#include "ScaleformUtil.h"

namespace ScaleformUtil {

    void DrawFilledRect(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                        double a_x, double a_y, double a_w, double a_h,
                        uint32_t a_color, int a_alpha) {
        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue clip;
        RE::GFxValue args[2];
        args[0].SetString(a_name);
        args[1].SetNumber(static_cast<double>(a_depth));
        root.Invoke("createEmptyMovieClip", &clip, args, 2);
        if (clip.IsUndefined()) return;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        clip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(a_x); pt[1].SetNumber(a_y);
        clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(a_x + a_w);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_y + a_h);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(a_x);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_y);
        clip.Invoke("lineTo", nullptr, pt, 2);

        clip.Invoke("endFill", nullptr, nullptr, 0);
    }

    void DrawBorderRect(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                        double a_x, double a_y, double a_w, double a_h,
                        uint32_t a_color) {
        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue clip;
        RE::GFxValue args[2];
        args[0].SetString(a_name);
        args[1].SetNumber(static_cast<double>(a_depth));
        root.Invoke("createEmptyMovieClip", &clip, args, 2);
        if (clip.IsUndefined()) return;

        RE::GFxValue styleArgs[3];
        styleArgs[0].SetNumber(1.0);
        styleArgs[1].SetNumber(static_cast<double>(a_color));
        styleArgs[2].SetNumber(100.0);
        clip.Invoke("lineStyle", nullptr, styleArgs, 3);

        RE::GFxValue pt[2];
        pt[0].SetNumber(a_x); pt[1].SetNumber(a_y);
        clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(a_x + a_w);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_y + a_h);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(a_x);
        clip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_y);
        clip.Invoke("lineTo", nullptr, pt, 2);
    }

    void DrawLine(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                  double a_x1, double a_y1, double a_x2, double a_y2,
                  uint32_t a_color) {
        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue clip;
        RE::GFxValue args[2];
        args[0].SetString(a_name);
        args[1].SetNumber(static_cast<double>(a_depth));
        root.Invoke("createEmptyMovieClip", &clip, args, 2);
        if (clip.IsUndefined()) return;

        RE::GFxValue styleArgs[3];
        styleArgs[0].SetNumber(1.0);
        styleArgs[1].SetNumber(static_cast<double>(a_color));
        styleArgs[2].SetNumber(100.0);
        clip.Invoke("lineStyle", nullptr, styleArgs, 3);

        RE::GFxValue pt[2];
        pt[0].SetNumber(a_x1); pt[1].SetNumber(a_y1);
        clip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(a_x2); pt[1].SetNumber(a_y2);
        clip.Invoke("lineTo", nullptr, pt, 2);
    }

    void CreateLabel(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                     double a_x, double a_y, double a_w, double a_h,
                     const char* a_text, int a_size, uint32_t a_color) {
        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue tfArgs[6];
        tfArgs[0].SetString(a_name);
        tfArgs[1].SetNumber(static_cast<double>(a_depth));
        tfArgs[2].SetNumber(a_x);
        tfArgs[3].SetNumber(a_y);
        tfArgs[4].SetNumber(a_w);
        tfArgs[5].SetNumber(a_h);
        root.Invoke("createTextField", nullptr, tfArgs, 6);

        std::string path = std::string("_root.") + a_name;

        RE::GFxValue tf;
        a_movie->GetVariable(&tf, path.c_str());
        if (tf.IsUndefined()) return;

        RE::GFxValue fmt;
        a_movie->CreateObject(&fmt, "TextFormat");
        if (!fmt.IsUndefined()) {
            RE::GFxValue fontVal, sizeVal, colorVal;
            fontVal.SetString("Arial");
            fmt.SetMember("font", fontVal);
            sizeVal.SetNumber(static_cast<double>(a_size));
            fmt.SetMember("size", sizeVal);
            colorVal.SetNumber(static_cast<double>(a_color));
            fmt.SetMember("color", colorVal);

            RE::GFxValue fmtArgs[1];
            fmtArgs[0] = fmt;
            tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
            tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
        }

        RE::GFxValue embedVal;
        embedVal.SetBoolean(true);
        tf.SetMember("embedFonts", embedVal);

        RE::GFxValue selVal;
        selVal.SetBoolean(false);
        tf.SetMember("selectable", selVal);

        RE::GFxValue textVal;
        textVal.SetString(a_text);
        a_movie->SetVariable((path + ".text").c_str(), textVal);
    }

    void SetTextFieldFormat(RE::GFxMovieView* a_movie, const std::string& a_path,
                            int a_size, uint32_t a_color) {
        RE::GFxValue textField;
        a_movie->GetVariable(&textField, a_path.c_str());

        if (textField.IsUndefined()) {
            logger::warn("SetTextFieldFormat: {} not found", a_path);
            return;
        }

        RE::GFxValue textFormat;
        a_movie->CreateObject(&textFormat, "TextFormat");

        if (textFormat.IsUndefined()) {
            RE::GFxValue fontVal, sizeVal, colorVal, embedVal;

            fontVal.SetString("Arial");
            textField.SetMember("font", fontVal);

            sizeVal.SetNumber(static_cast<double>(a_size));
            textField.SetMember("size", sizeVal);

            colorVal.SetNumber(static_cast<double>(a_color));
            textField.SetMember("textColor", colorVal);

            embedVal.SetBoolean(true);
            textField.SetMember("embedFonts", embedVal);
            return;
        }

        RE::GFxValue fontVal, sizeVal, colorVal;
        fontVal.SetString("Arial");
        textFormat.SetMember("font", fontVal);
        sizeVal.SetNumber(static_cast<double>(a_size));
        textFormat.SetMember("size", sizeVal);
        colorVal.SetNumber(static_cast<double>(a_color));
        textFormat.SetMember("color", colorVal);

        RE::GFxValue fmtArgs[1];
        fmtArgs[0] = textFormat;
        textField.Invoke("setTextFormat", nullptr, fmtArgs, 1);
        textField.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);

        RE::GFxValue embedVal;
        embedVal.SetBoolean(true);
        textField.SetMember("embedFonts", embedVal);

        RE::GFxValue selectVal;
        selectVal.SetBoolean(false);
        textField.SetMember("selectable", selectVal);
    }

}
