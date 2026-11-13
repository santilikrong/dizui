
#include "RenderEngine.hpp"
#include "IconsLucide.hpp" 
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>   // <-- INCLUSION FONDAMENTALE
#include <imgui_impl_opengl3.h>  // <-- INCLUSION FONDAMENTALE
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>

RenderEngine::RenderEngine(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const char* font_path = "/home/santili/.fonts/JetBrainsMono-Regular.ttf";
    std::filesystem::path absolute_font_path = std::filesystem::path(DIZUI_ASSETS_DIR) / "lucide.ttf";
    bool has_lucide = std::filesystem::exists(absolute_font_path) && std::filesystem::file_size(absolute_font_path) > 0;

    // Configuration de base pour la fusion des icônes Lucide
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = 16.0f;
    icons_config.GlyphOffset.y = 4.0f; 
    static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

    // -------------------------------------------------------------------------
    // 1. CHARGEMENT DE LA POLICE PRINCIPALE (18.0f) + FUSION LUCIDE
    // -------------------------------------------------------------------------
    ImFont* main_font = nullptr;
    if (std::filesystem::exists(font_path)) {
        main_font = io.Fonts->AddFontFromFileTTF(font_path, 18.0f);
    } else {
        main_font = io.Fonts->AddFontDefault();
    }

    if (has_lucide && main_font != nullptr) {
        // Associe les icônes à la police principale active dans tout l'IDE
        io.Fonts->AddFontFromFileTTF(absolute_font_path.string().c_str(), 20.0f, &icons_config, icons_ranges);
    }

    // -------------------------------------------------------------------------
    // 2. CHARGEMENT DE LA POLICE COMPACTE COMBOBOX (13.0f) + FUSION LUCIDE
    // -------------------------------------------------------------------------
    ImFont* small_font = nullptr;
    if (std::filesystem::exists(font_path)) {
        small_font = io.Fonts->AddFontFromFileTTF(font_path, 15.0f);
        
        if (has_lucide && small_font != nullptr) {
            // Associe ÉGALEMENT les icônes à la petite police pour qu'elles s'affichent dans les combos
            io.Fonts->AddFontFromFileTTF(absolute_font_path.string().c_str(), 14.0f, &icons_config, icons_ranges);
        }
    }

    if (has_lucide) {
        std::cout << "[dizui Info] Lucide Icons chargé et fusionné sur toutes les tailles avec succès !" << std::endl;
    } else {
        std::cerr << "[dizui Error] Fichier d'icônes introuvable : " << absolute_font_path << std::endl;
    }

    // Configuration de style d'interface
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
}
// Le reste des fonctions du fichier reste identique...




RenderEngine::~RenderEngine() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void RenderEngine::startFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void RenderEngine::endFrame(int display_w, int display_h) {
    ImGui::Render();
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
