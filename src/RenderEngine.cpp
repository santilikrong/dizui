
#include "RenderEngine.hpp"
#include "IconsLucide.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>    // <-- INCLUSION FONDAMENTALE
#include <imgui_impl_opengl3.h> // <-- INCLUSION FONDAMENTALE
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>

RenderEngine::RenderEngine(GLFWwindow *window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // -------------------------------------------------------------------------
    // 0. RECHERCHE MULTIPLATEFORME DE LA POLICE PRINCIPALE (Linux / macOS)
    // -------------------------------------------------------------------------
    std::string resolved_font_path = "";

#ifdef __APPLE__
    std::string home_font = "";
    if (const char *home = std::getenv("HOME"))
        home_font = std::string(home) + "/Library/Fonts/JetBrainsMono-Regular.ttf";

    for (const auto &candidate : {
             home_font,
             std::string("/Library/Fonts/JetBrainsMono-Regular.ttf"),
             std::string("/System/Library/Fonts/Supplemental/Menlo.ttc"),
             std::string("/System/Library/Fonts/Supplemental/Arial.ttf"),
             std::string("/System/Library/Fonts/Helvetica.ttc")})
#else
    std::string home_font_1 = "";
    std::string home_font_2 = "";
    if (const char *home = std::getenv("HOME"))
    {
        home_font_1 = std::string(home) + "/.fonts/JetBrainsMono-Regular.ttf";
        home_font_2 = std::string(home) + "/.local/share/fonts/JetBrainsMono-Regular.ttf";
    }

    for (const auto &candidate : {
             home_font_1,
             home_font_2,
             std::string("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
             std::string("/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf")})
#endif
    {
        if (!candidate.empty() && std::filesystem::exists(candidate) && std::filesystem::file_size(candidate) > 0)
        {
            resolved_font_path = candidate;
            break;
        }
    }


    std::filesystem::path absolute_icon_path = std::filesystem::path(DIZUI_ASSETS_DIR) / "lucide.ttf";
    bool has_lucide = std::filesystem::exists(absolute_icon_path) && std::filesystem::file_size(absolute_icon_path) > 0;

    // Configuration de base pour la fusion des icônes Lucide
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = 16.0f;
    icons_config.GlyphOffset.y = 4.0f;
    static const ImWchar icons_ranges[] = {ICON_MIN_LC, ICON_MAX_LC, 0};

    // -------------------------------------------------------------------------
    // 1. CHARGEMENT DE LA POLICE PRINCIPALE (18.0f) + FUSION LUCIDE
    // -------------------------------------------------------------------------
    ImFont *main_font = nullptr;
    if (!resolved_font_path.empty())
    {
        main_font = io.Fonts->AddFontFromFileTTF(resolved_font_path.c_str(), 18.0f);
    }

    if (main_font == nullptr)
    {
        // Taille explicite obligatoire pour autoriser MergeMode avec ImGui docking récent
        ImFontConfig default_cfg;
        default_cfg.SizePixels = 18.0f;
        main_font = io.Fonts->AddFontDefault(&default_cfg);
    }

    if (has_lucide && main_font != nullptr)
    {
        io.Fonts->AddFontFromFileTTF(absolute_icon_path.string().c_str(), 20.0f, &icons_config, icons_ranges);
    }

    // -------------------------------------------------------------------------
    // 2. CHARGEMENT DE LA POLICE COMPACTE COMBOBOX (15.0f) + FUSION LUCIDE
    // -------------------------------------------------------------------------
    ImFont *small_font = nullptr;
    if (!resolved_font_path.empty())
    {
        small_font = io.Fonts->AddFontFromFileTTF(resolved_font_path.c_str(), 15.0f);
    }

    if (small_font == nullptr)
    {
        ImFontConfig small_default_cfg;
        small_default_cfg.SizePixels = 15.0f;
        small_font = io.Fonts->AddFontDefault(&small_default_cfg);
    }

    if (has_lucide && small_font != nullptr)
    {
        io.Fonts->AddFontFromFileTTF(absolute_icon_path.string().c_str(), 14.0f, &icons_config, icons_ranges);
    }

    if (has_lucide)
    {
        std::cout << "[dizui Info] Lucide Icons chargé et fusionné avec succès !" << std::endl;
    }
    else
    {
        std::cerr << "[dizui Error] Fichier d'icônes introuvable : " << absolute_icon_path << std::endl;
    }

    // Configuration de style d'interface
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __APPLE__
    const char *glsl_version = "#version 150";
#else
    const char *glsl_version = "#version 130";
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);
}


RenderEngine::~RenderEngine()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void RenderEngine::startFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void RenderEngine::endFrame(int display_w, int display_h)
{
    ImGui::Render();
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
