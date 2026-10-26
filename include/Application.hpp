#pragma once
#include "WindowManager.hpp"
#include "RenderEngine.hpp"
#include <imgui_node_editor.h>
#include <memory>

namespace ed = ax::NodeEditor;

class Application {
public:
    Application();
    ~Application();

    void run();

private:
    void renderUI();
    // Séparation claire des fonctions de rendu des deux canevas graphiques
    void renderWorkflowEditor();
    void renderQueryEditor();
    
    enum class ActiveTab {
        Project,
        Search,
        Git,
        Package,
        Settings
    };

    std::unique_ptr<WindowManager> m_windowManager;
    std::unique_ptr<RenderEngine> m_renderEngine;
    // Dans include/Application.hpp -> section private de la classe Application :
    float m_queryEditorHeight = 300.0f; // Hauteur initiale par défaut de la zone de code
    float m_queryVisualHeight = 350.0f;


    // --- CORRECTION : Deux instances de contextes indépendantes ---
    ed::EditorContext* m_workflowContext = nullptr;
    ed::EditorContext* m_queryContext = nullptr;

    ActiveTab m_currentTab = ActiveTab::Project; 
};
