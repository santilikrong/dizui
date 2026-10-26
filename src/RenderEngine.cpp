
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

    // 1. Charger la police de texte système classique
    ImFont* font = nullptr;
    const char* font_paths[] = {
        "/home/santili/.fonts/JetBrainsMono-Regular.ttf"
    };
    for (const char* path : font_paths) {
        font = io.Fonts->AddFontFromFileTTF(path, 18.0f);
        if (font != nullptr) break;
    }
    if (font == nullptr) io.Fonts->AddFontDefault();

    // 2. FUSIONNER la police Lucide Icons brute depuis CMake
    std::filesystem::path absolute_font_path = std::filesystem::path(DIZUI_ASSETS_DIR) / "lucide.ttf";

    if (std::filesystem::exists(absolute_font_path) && std::filesystem::file_size(absolute_font_path) > 0) {
        ImFontConfig icons_config;
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.GlyphMinAdvanceX = 16.0f;
        icons_config.GlyphOffset.y = 4.0f; // Alignement vertical propre à Lucide

        // Définir la plage de recherche Lucide
        static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };
        
        io.Fonts->AddFontFromFileTTF(absolute_font_path.string().c_str(), 20.0f, &icons_config, icons_ranges);
        std::cout << "[dizui Info] Lucide Icons chargé avec succès !" << std::endl;
    } else {
        std::cerr << "[dizui Error] Fichier introuvable : " << absolute_font_path << std::endl;
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
