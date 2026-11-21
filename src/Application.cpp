#include "Application.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include "IconsLucide.hpp"
#include <cppsqlx/task.hpp>
#include <cppsqlx/job.hpp>
#include <cppsqlx/workflow.hpp>
#include <cppsqlx/logger.hpp>
#include <iostream>
#include <fstream>
#include <cppsqlx/sqlliterals.hpp>
#include <cppsqlx/sqljoinexpressionbuilder.hpp>
#include <cppsqlx/selectquery.hpp>
#include <cppsqlx/statement.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include <log4cxx/appenderskeleton.h>
#include <log4cxx/spi/loggingevent.h>

using namespace cppsqlx::literals;

class DizuiConsoleAppender : public log4cxx::AppenderSkeleton
{
public:
    explicit DizuiConsoleAppender(Application *app) : m_app(app) {}

    void close() override {}
    bool requiresLayout() const override { return false; }

protected:
    void append(const log4cxx::spi::LoggingEventPtr &event, log4cxx::helpers::Pool &) override
    {
        if (!m_app || !event)
            return;

        std::string msg = event->getRenderedMessage();
        auto levelInt = event->getLevel()->toInt();

        if (levelInt >= log4cxx::Level::ERROR_INT)
            m_app->addConsoleLog(msg, Application::LogLevel::Error);
        else if (levelInt >= log4cxx::Level::WARN_INT)
            m_app->addConsoleLog(msg, Application::LogLevel::Warning);
        else
            m_app->addConsoleLog(msg, Application::LogLevel::Info);
    }

private:
    Application *m_app = nullptr;
};

namespace
{
    class GuiStreamRedirector : public std::streambuf
    {
    public:
        GuiStreamRedirector(std::ostream &stream, std::function<void(const std::string &)> callback)
            : m_stream(stream), m_oldBuf(stream.rdbuf()), m_callback(std::move(callback))
        {
            m_stream.rdbuf(this);
        }

        ~GuiStreamRedirector() override
        {
            if (m_oldBuf)
                m_stream.rdbuf(m_oldBuf);
        }

    protected:
        int overflow(int c) override
        {
            if (c != EOF)
            {
                char ch = static_cast<char>(c);
                if (m_oldBuf)
                    m_oldBuf->sputc(ch);

                if (ch == '\n')
                {
                    if (!m_lineBuffer.empty())
                    {
                        m_callback(m_lineBuffer);
                        m_lineBuffer.clear();
                    }
                }
                else if (ch != '\r')
                {
                    m_lineBuffer.push_back(ch);
                }
            }
            return c;
        }

    private:
        std::ostream &m_stream;
        std::streambuf *m_oldBuf;
        std::function<void(const std::string &)> m_callback;
        std::string m_lineBuffer;
    };

    std::unique_ptr<GuiStreamRedirector> g_coutRedirector;
    std::unique_ptr<GuiStreamRedirector> g_cerrRedirector;
}

Application::Application()
{

    auto rootLogger = log4cxx::Logger::getRootLogger();
    rootLogger->addAppender(std::make_shared<DizuiConsoleAppender>(this));

    g_coutRedirector = std::make_unique<GuiStreamRedirector>(std::cout, [this](const std::string &line)
                                                             {
        LogLevel lvl = LogLevel::Info;
        if (line.find("succès") != std::string::npos || line.find("OK") != std::string::npos)
            lvl = LogLevel::Success;
        addConsoleLog(line, lvl); });

    g_cerrRedirector = std::make_unique<GuiStreamRedirector>(std::cerr, [this](const std::string &line)
                                                             { addConsoleLog(line, LogLevel::Error); });

    m_windowManager = std::make_unique<WindowManager>(2200, 1200, "dizui - Linux C++20 Workspace");
    m_renderEngine = std::make_unique<RenderEngine>(m_windowManager->getNativeWindow());

    ed::Config canvasConfig;
    canvasConfig.SettingsFile = nullptr;
    canvasConfig.NavigateButtonIndex = 1;    // 1 = Clic droit maintenu pour déplacer le canevas (0 = Gauche, 2 = Molette)
    canvasConfig.ContextMenuButtonIndex = 2; // 2 = Clic molette pour menu contextuel (libère le clic droit pour le Pan)
    canvasConfig.EnableSmoothZoom = true;

    m_workflowContext = ed::CreateEditor(&canvasConfig);
    m_queryContext = ed::CreateEditor(&canvasConfig);

#ifdef __APPLE__
    auto res = cppsqlx::DataIntegrationProject::loadFromDisk("/Users/70075J/cppsqlx/projects/finance/project.json");
#else
    auto res = cppsqlx::DataIntegrationProject::loadFromDisk("/home/santili/cppsqlx/projects/finance/project.json");
#endif

    if (res)
    {
        m_activeProject = std::make_unique<cppsqlx::DataIntegrationProject>(std::move(*res));
        m_activeProject->resolveDependencies();

        if (m_activeProject->logicalConnections().empty())
        {
            cppsqlx::LogicalConnection mockGateway("Finance_DB_Gateway");
            if (!m_activeProject->physicalConnections().empty())
            {
                for (const auto &[name, physConn] : m_activeProject->physicalConnections())
                {
                    mockGateway.registerEnvironment(name, physConn);
                }
            }
            m_activeProject->logicalConnections().insert_or_assign("Finance_DB_Gateway", std::move(mockGateway));
        }
    }

    if (!m_activeProject)
    {
        createNewProject("Nouveau_Projet");
    }

    applyTheme(AppTheme::Nord);
}

Application::~Application()
{

    if (m_workflowContext)
        ed::DestroyEditor(m_workflowContext);
    if (m_queryContext)
        ed::DestroyEditor(m_queryContext);
    g_coutRedirector.reset();
    g_cerrRedirector.reset();

    if (m_ctePreviewContext)
    {
        ed::DestroyEditor(m_ctePreviewContext);
        m_ctePreviewContext = nullptr;
    }
}

void Application::run()
{
    while (!m_windowManager->shouldClose())
    {
        m_windowManager->pollEvents();
        m_renderEngine->startFrame();

        renderUI();

        int display_w, display_h;
        glfwGetFramebufferSize(m_windowManager->getNativeWindow(), &display_w, &display_h);
        m_renderEngine->endFrame(display_w, display_h);
        m_windowManager->swapBuffers();
    }
}


void Application::renderUI()
{

    renderStatusBar();
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DizuiWorkspaceHost", nullptr, host_window_flags);
    ImGui::PopStyleVar(3);

    float statusBarHeight = 28.0f;
    float mainAreaHeight = ImGui::GetContentRegionAvail().y - statusBarHeight;
    if (mainAreaHeight < 100.0f)
        mainAreaHeight = 100.0f;

    ImGuiID dockspace_id = ImGui::GetID("DizuiMainDockSpace");

    static bool init_layout = true;
    if (init_layout && !ImGui::DockBuilderGetNode(dockspace_id))
    {
        init_layout = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_left_id, dock_center_id;

        dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.04f, nullptr, &dock_main_id);
        ImGuiDockNode *left_node = ImGui::DockBuilderGetNode(dock_left_id);
        if (left_node)
            left_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | static_cast<int>(ImGuiDockNodeFlags_NoResize);

        dock_center_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.25f, nullptr, &dock_main_id);
        ImGuiDockNode *center_node = ImGui::DockBuilderGetNode(dock_center_id);
        if (center_node)
            center_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

        ImGuiID dock_bottom_result_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.333f, nullptr, &dock_main_id);
        std::ignore = dock_bottom_result_id;

        ImGuiDockNode *main_node = ImGui::DockBuilderGetNode(dock_main_id);
        if (main_node)
            main_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

        // Découpe 25% de la hauteur en bas de l'espace de travail pour la console et les résultats SQL
        ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("Outils", dock_left_id);
        ImGui::DockBuilderDockWindow("Panneau de Contrôle", dock_center_id);
        ImGui::DockBuilderDockWindow("Espace de Travail (Canevas)", dock_main_id);

        // Regroupement des deux panneaux sous forme d'onglets dans le dock du bas
        ImGui::DockBuilderDockWindow("Console & Journaux d'Exécution", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Résultat du Graphe Visuel", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Assistant IA (Ollama)", dock_bottom_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // 1. Dessin de la barre supérieure (Menu Bar principal)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    if (ImGui::BeginMainMenuBar())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s", ICON_LC_CPU);
        ImGui::Spacing();
        if (ImGui::BeginMenu("File"))
        {

            if (ImGui::MenuItem((std::string(ICON_LC_FILE_PLUS) + "  Nouveau projet").c_str(), "Ctrl+N"))
            {
                createNewProject("Nouveau_Projet");
            }
            ImGui::Separator();

            if (ImGui::MenuItem("Ouvrir...", "Ctrl+O"))
            {
                if (!m_currentProjectFilePath.empty())
                {
                    m_browserCurrentDir = std::filesystem::path(m_currentProjectFilePath).parent_path();
                }
                else
                {
                    m_browserCurrentDir = std::filesystem::current_path();
                }
                memset(m_projectFileNameBuf, 0, sizeof(m_projectFileNameBuf));
                m_showOpenProjectModal = true;
            }

            ImGui::Separator();
            // ENREGISTRER : Sauvegarde directe si un chemin existe déjà, sinon bascule sur "Enregistrer sous"
            if (ImGui::MenuItem("Enregistrer", "Ctrl+S"))
            {
                if (!m_currentProjectFilePath.empty())
                {
                    saveProjectToFile(m_currentProjectFilePath);
                }
                else
                {
                    m_showSaveProjectAsModal = true;
                }
            }

            // ENREGISTRER SOUS : Ouvre systématiquement la modale de choix de fichier
            if (ImGui::MenuItem("Enregistrer sous...", "Ctrl+Shift+S"))
            {
                if (!m_currentProjectFilePath.empty())
                {
                    std::filesystem::path p(m_currentProjectFilePath);
                    m_browserCurrentDir = p.parent_path();
                    strncpy(m_projectFileNameBuf, p.filename().string().c_str(), sizeof(m_projectFileNameBuf) - 1);
                }
                else
                {
                    m_browserCurrentDir = std::filesystem::current_path();
                }
                m_showNewFolderInput = false;
                m_showSaveProjectAsModal = true;
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
            ImGui::EndMenu();
        if (ImGui::BeginMenu("View"))
            ImGui::EndMenu();
        if (ImGui::BeginMenu("Help"))
            ImGui::EndMenu();

        if (m_activeProject)
        {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(20.0f, 0.0f));
            ImGuiIO &io = ImGui::GetIO();
            ImFont *small_font_ptr = (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[0] : nullptr;

            ImGui::SameLine();
            ImGui::TextDisabled("Connexion :");
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4.0f, 0.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(250.0f);

            if (m_selectedGateway.empty() && !m_activeProject->logicalConnections().empty())
                m_selectedGateway = m_activeProject->logicalConnections().begin()->first;

            if (small_font_ptr)
                ImGui::PushFont(small_font_ptr);
            if (ImGui::BeginCombo("##GatewayCombo", m_selectedGateway.empty() ? "Connexion..." : m_selectedGateway.c_str()))
            {
                for (const auto &[name, logConn] : m_activeProject->logicalConnections())
                {
                    if (ImGui::Selectable(name.c_str(), m_selectedGateway == name))
                    {
                        m_selectedGateway = name;
                        m_selectedContext = "";
                    }
                }
                ImGui::EndCombo();
            }
            if (small_font_ptr)
                ImGui::PopFont();

            ImGui::SameLine();
            ImGui::Dummy(ImVec2(15.0f, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("Contexte :");
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4.0f, 0.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);

            std::vector<std::string> available_contexts;
            if (m_activeProject->logicalConnections().contains(m_selectedGateway))
            {
                const auto &currentGateway = m_activeProject->logicalConnections().at(m_selectedGateway);
                for (const auto &[envName, physConn] : currentGateway.environments())
                    available_contexts.push_back(envName);
                if (m_selectedContext.empty() && !available_contexts.empty())
                    m_selectedContext = available_contexts.front();
            }

            if (small_font_ptr)
                ImGui::PushFont(small_font_ptr);
            if (ImGui::BeginCombo("##ContextCombo", m_selectedContext.empty() ? "Contexte..." : m_selectedContext.c_str()))
            {
                for (const auto &envName : available_contexts)
                {
                    if (ImGui::Selectable(envName.c_str(), m_selectedContext == envName))
                        m_selectedContext = envName;
                }
                ImGui::EndCombo();
            }
            if (small_font_ptr)
                ImGui::PopFont();
        }

        float text_width = ImGui::CalcTextSize("dizui v1.0").x;
        ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 20.0f);
        ImGui::TextDisabled("dizui v1.0");
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleVar();

    // DES SOUS-SYSTÈMES GRAPHIOQUES
    renderLeftSidebar();
    renderMainContentZone();
    renderConsolePanel();
    renderQueryResultsPanel();
    renderAiAssistantPanel();
    renderGlobalModals();
    
}

void Application::applyTheme(AppTheme theme)
{
    m_currentTheme = theme;
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;

    ImVec4 canvasBg;
    ImVec4 canvasGrid;

    switch (theme)
    {
    case AppTheme::Light:
    {
        ImGui::StyleColorsLight();
        colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.80f, 0.82f, 0.86f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.22f, 0.52f, 0.88f, 0.25f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.52f, 0.88f, 0.40f);
        colors[ImGuiCol_Button] = ImVec4(0.86f, 0.88f, 0.92f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.52f, 0.88f, 0.35f);
        canvasBg = ImVec4(0.92f, 0.93f, 0.96f, 1.00f);
        canvasGrid = ImVec4(0.82f, 0.84f, 0.88f, 0.60f);
        break;
    }

    case AppTheme::Nord:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);     // Snow Storm
        colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f); // Polar Night #2E3440
        colors[ImGuiCol_ChildBg] = ImVec4(0.21f, 0.24f, 0.29f, 1.00f);  // #3B4252
        colors[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.20f, 0.25f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.30f, 0.34f, 0.42f, 1.00f);        // #4C566A
        colors[ImGuiCol_FrameBg] = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);       // #434C5E
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f); // Frost #5E81AC
        colors[ImGuiCol_Header] = ImVec4(0.53f, 0.75f, 0.82f, 0.30f);        // Frost #88C0D0
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.75f, 0.82f, 0.45f);
        colors[ImGuiCol_Button] = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.37f, 0.51f, 0.67f, 0.65f);
        canvasBg = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
        canvasGrid = ImVec4(0.26f, 0.30f, 0.37f, 0.50f);
        break;
    }

    case AppTheme::Dracula:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text] = ImVec4(0.97f, 0.97f, 0.95f, 1.00f);     // Foreground #F8F8F2
        colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.16f, 0.21f, 1.00f); // Background #282A36
        colors[ImGuiCol_ChildBg] = ImVec4(0.19f, 0.20f, 0.26f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.21f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.38f, 0.45f, 0.64f, 0.70f);        // Comment #6272A4
        colors[ImGuiCol_FrameBg] = ImVec4(0.27f, 0.28f, 0.35f, 1.00f);       // Current Line #44475A
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.74f, 0.58f, 0.98f, 0.85f); // Purple #BD93F9
        colors[ImGuiCol_Header] = ImVec4(0.74f, 0.58f, 0.98f, 0.30f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 0.47f, 0.78f, 0.40f); // Pink #FF79C6
        colors[ImGuiCol_Button] = ImVec4(0.27f, 0.28f, 0.35f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.74f, 0.58f, 0.98f, 0.50f);
        canvasBg = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
        canvasGrid = ImVec4(0.27f, 0.28f, 0.35f, 0.55f);
        break;
    }

    case AppTheme::DarkModern:
    default:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.15f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.24f, 0.26f, 0.31f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.23f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.75f, 0.35f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.52f, 0.85f, 0.50f);
        colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.52f, 0.85f, 0.45f);
        canvasBg = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        canvasGrid = ImVec4(0.22f, 0.24f, 0.28f, 0.45f);
        break;
    }
    }

    // Application des couleurs de grille sur les deux éditeurs de nœuds
    auto applyNodeCanvasStyle = [&](ed::EditorContext *ctx)
    {
        if (!ctx)
            return;
        ed::SetCurrentEditor(ctx);
        ed::GetStyle().Colors[ed::StyleColor_Bg] = canvasBg;
        ed::GetStyle().Colors[ed::StyleColor_Grid] = canvasGrid;
        ed::SetCurrentEditor(nullptr);
    };

    applyNodeCanvasStyle(m_queryContext);
    applyNodeCanvasStyle(m_workflowContext);
    applyNodeCanvasStyle(m_ctePreviewContext);
}

void Application::renderLeftSidebar()
{
    renderActivityBar();
    renderProjectExplorerTree();
}

void Application::renderActivityBar()
{
    ImGuiWindowFlags toolbar_flags_local = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    ImGui::Begin("Outils", nullptr, toolbar_flags_local);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 0.60f));

    auto drawToolbarButtonLocal = [&](const char *icon, const char *tooltip, ActiveTab tabTarget)
    {
        if (m_currentTab == tabTarget)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 0.80f));
        else
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        if (ImGui::Button(icon, ImVec2(48, 48)))
        {
            m_currentTab = tabTarget;
        }
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::PopStyleColor();
    };

    drawToolbarButtonLocal(ICON_LC_BLOCKS, "Project workspace", ActiveTab::Project);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_SEARCH, "Rechercher dans le Graphe", ActiveTab::Search);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_GIT_BRANCH, "Gérer les sources", ActiveTab::Git);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_BOX, "Gestion package", ActiveTab::Package);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_SETTINGS, "Paramètres de l'application", ActiveTab::Settings);
    ImGui::PopStyleColor(2);
    ImGui::End();
}

void Application::renderMainContentZone()
{
    ImGuiWindowFlags workspace_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;
    ImGui::Begin("Espace de Travail (Canevas)", nullptr, workspace_flags);

    ImGuiWindow *current_win = ImGui::GetCurrentWindow();
    if (current_win && current_win->DockNode)
        current_win->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

    bool isQueryVisualActive = false;

    if (ImGui::BeginTabBar("DesignerTabBar", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem((std::string(ICON_LC_ACTIVITY) + "  Workflow Designer").c_str()))
        {
            if (ImGui::IsItemActivated())
            {
                m_showConsolePanel = true;
                ImGui::SetWindowFocus("Console & Journaux d'Exécution");
            }
            renderWorkflowEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem((std::string(ICON_LC_SEARCH) + "  Query Visual Designer").c_str()))
        {
            isQueryVisualActive = true;
            if (ImGui::IsItemActivated())
            {
                m_focusQueryResultsTab = true;
            }
            renderQueryEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem((std::string(ICON_LC_BOX) + "  Query Text Editor").c_str()))
        {
            isQueryVisualActive = true;
            if (ImGui::IsItemActivated())
            {
                m_focusQueryResultsTab = true;
            }
            renderQueryTextEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem((std::string(ICON_LC_LAYERS) + "  Pipeline CTE").c_str()))
        {
            isQueryVisualActive = true;
            if (ImGui::IsItemActivated())
            {
                m_focusQueryResultsTab = true;
            }
            renderCteStackEditor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    m_showQueryResultsPanel = isQueryVisualActive;

    ImGui::End();
}

void Application::renderGlobalModals()
{
    renderReverseEngineeringModal();
    renderCreateTaskModal();
    renderSaveProjectAsModal();
    renderOpenProjectModal();
    renderRenameModals();
    renderConnectionModals();
    renderTaskManagementModals();
    renderQueryInspectAndDeleteModals();
    renderCtePreviewModal();
}

void Application::renderReverseEngineeringModal()
{
    if (m_showReverseModal)
        ImGui::OpenPopup("Reverse Engineering - Catalogue PostgreSQL");

    ImGui::SetNextWindowSize(ImVec2(900.0f, 550.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(650.0f, 400.0f), ImVec2(1920.0f, 1080.0f));

    if (ImGui::BeginPopupModal("Reverse Engineering - Catalogue PostgreSQL", &m_showReverseModal, ImGuiWindowFlags_None))
    {
        static std::string last_gateway = "";
        static std::string last_env = "";
        using PgConnectionPtr = std::shared_ptr<cppsqlx::Connection>;
        static PgConnectionPtr modalConn = nullptr;

        if (!modalConn || last_gateway != m_selectedGateway || last_env != m_selectedContext)
        {
            last_gateway = m_selectedGateway;
            last_env = m_selectedContext;
            modalConn.reset();

            // Réinitialisation complète du cache de métadonnées
            m_cachedSchemas.clear();
            m_cachedTables.clear();
            m_cachedStructure.clear();
            m_reverseSelectedObject = "";

            if (m_activeProject && m_activeProject->logicalConnections().contains(m_selectedGateway))
            {
                const auto &gateway = m_activeProject->logicalConnections().at(m_selectedGateway);
                if (gateway.environments().contains(m_selectedContext))
                {
                    const auto &phys = gateway.environments().at(m_selectedContext);
                    modalConn = PgConnectionPtr(new cppsqlx::Connection(phys.toConnectionString()));
                    modalConn->connect();
                }
            }
        }

        if (!modalConn || !modalConn->isConnected())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Erreur Connexion Active.");
            if (ImGui::Button("Fermer"))
            {
                m_showReverseModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        cppsqlx::DatabaseMetadata metaDiscover(modalConn);
        if (m_cachedSchemas.empty())
        {
            auto schemaRes = metaDiscover.getSchemas();
            if (schemaRes)
                m_cachedSchemas = std::move(*schemaRes);
        }

        float child_panel_height = ImMax(150.0f, ImGui::GetContentRegionAvail().y - 45.0f);
        ImGui::BeginChild("LeftDiscoveryPanel", ImVec2(280.0f, child_panel_height), true);

        if (ImGui::BeginCombo("##SchemaSelector", m_reverseSelectedSchema.c_str()))
        {
            for (const auto &sch : m_cachedSchemas)
            {
                if (ImGui::Selectable(sch.c_str(), m_reverseSelectedSchema == sch))
                {
                    m_reverseSelectedSchema = sch;
                    m_reverseSelectedObject = "";
                    m_cachedTables.clear(); // Force le rechargement des tables du nouveau schéma
                    m_cachedStructure.clear();
                }
            }
            ImGui::EndCombo();
        }

        if (m_cachedTables.empty())
        {
            auto tablesRes = metaDiscover.getTables(m_reverseSelectedSchema);
            if (tablesRes)
                m_cachedTables = std::move(*tablesRes);
        }

        // Champ de filtre rapide pour les schémas volumineux
        static char tableFilterBuf[128] = "";
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##TableSearchFilter", "Filtrer les tables...", tableFilterBuf, IM_ARRAYSIZE(tableFilterBuf));
        ImGui::Spacing();

        if (ImGui::TreeNodeEx("Tables", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Construction de la liste filtrée (insensible à la casse)
            std::vector<const std::string *> filteredTables;
            filteredTables.reserve(m_cachedTables.size());
            std::string filterStr(tableFilterBuf);
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            for (const auto &tbl : m_cachedTables)
            {
                if (filterStr.empty())
                {
                    filteredTables.push_back(&tbl);
                }
                else
                {
                    std::string lowerTbl = tbl;
                    std::transform(lowerTbl.begin(), lowerTbl.end(), lowerTbl.begin(), ::tolower);
                    if (lowerTbl.find(filterStr) != std::string::npos)
                    {
                        filteredTables.push_back(&tbl);
                    }
                }
            }

            // Virtualisation du rendu : ne dessine que les lignes visibles à l'écran
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filteredTables.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const std::string &tbl = *filteredTables[row];
                    ImGui::PushID(row);
                    if (ImGui::Selectable(tbl.c_str(), m_reverseSelectedObject == tbl))
                    {
                        m_reverseSelectedObject = tbl;
                        auto structRes = metaDiscover.getTableStructure(m_reverseSelectedObject, m_reverseSelectedSchema);
                        if (structRes)
                            m_cachedStructure = std::move(*structRes);
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();

            ImGui::TreePop();
        }

        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("RightStructurePanel", ImVec2(0.0f, child_panel_height), true);

        if (!m_reverseSelectedObject.empty() && ImGui::BeginTable("MetadataColumnsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Nom");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Null");
            ImGui::TableSetupColumn("PK");
            ImGui::TableHeadersRow();

            for (const auto &col : m_cachedStructure)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", col.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", col.dataType.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", col.isNullable ? "Oui" : "Non");
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", col.isPrimaryKey ? "[PK]" : "-");
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Monter dans le Projet") && m_activeProject)
        {
            auto datasetRes = cppsqlx::DatasetFactory().createFromDatabase(*modalConn, m_reverseSelectedObject, m_reverseSelectedSchema);
            if (datasetRes)
                m_activeProject->addDataset(std::move(datasetRes.value()));
            m_showReverseModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler"))
        {
            m_showReverseModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::renderCreateTaskModal()
{
    if (m_showCreateTaskModal)
        ImGui::OpenPopup("Créer une nouvelle tâche de flux");

    ImGui::SetNextWindowSize(ImVec2(950.0f, 550.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(750.0f, 400.0f), ImVec2(1920.0f, 1080.0f));

    if (ImGui::BeginPopupModal("Créer une nouvelle tâche de flux", &m_showCreateTaskModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Studio d'Orchestration : Configuration Multi-Étapes", ICON_LC_ACTIVITY);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Nom du Pipeline Task (Workflow) :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::InputText("##TaskNameInput", m_newTaskNameBuf, IM_ARRAYSIZE(m_newTaskNameBuf)))
        {
            m_workingTaskContext.setName(m_newTaskNameBuf);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float panel_height = ImGui::GetContentRegionAvail().y - 45.0f;
        float form_panel_width = 410.0f;
        float preview_panel_width = ImGui::GetContentRegionAvail().x - form_panel_width - ImGui::GetStyle().ItemSpacing.x;

        // Panneau gauche : Formulaire d'étape
        ImGui::BeginChild("LeftStepFormPanel", ImVec2(form_panel_width, panel_height), true);
        ImGui::TextColored(ImVec4(0.55f, 0.35f, 0.9f, 1.0f), "CONSTRUIRE UNE ÉTAPE");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Index de l'étape (Step Index dans la Map) :");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("##StepIndexInput", &m_currentStepIndex, 1, 1);
        if (m_currentStepIndex < 1)
            m_currentStepIndex = 1;

        ImGui::Spacing();

        ImGui::Text("Nom de la Cible de calcul (name) :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##StepNameInput", m_newTaskStepNameBuf, IM_ARRAYSIZE(m_newTaskStepNameBuf));

        ImGui::Spacing();

        ImGui::Text("Type de Target d'atterrissage (TargetType) :");
        ImGui::SetNextItemWidth(-1.0f);
        const char *target_types_labels[] = {"Table physique (Table)", "Vue SQL (View)", "Table Temporaire (TemporaryTable)", "Sous-requête (SubQuery)"};
        if (ImGui::BeginCombo("##TargetTypeComboSelector", target_types_labels[m_selectedTargetTypeIndex]))
        {
            for (int i = 0; i < 4; ++i)
            {
                if (ImGui::Selectable(target_types_labels[i], m_selectedTargetTypeIndex == i))
                    m_selectedTargetTypeIndex = i;
            }
            ImGui::EndCombo();
        }

        if (m_selectedTargetTypeIndex == 0)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Stratégie d'écriture de la Table :");
            ImGui::RadioButton("Écraser (CTAS)", &m_tableOperationMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Ajouter à la suite (INSERT)", &m_tableOperationMode, 1);
            ImGui::Spacing();
        }

        ImGui::Spacing();

        ImGui::Text("Associer la requête SQL du graphe :");
        ImGui::SetNextItemWidth(-1.0f);
        std::string combo_preview = "Sélectionner une requête...";
        if (m_selectedQueryIndexForTask >= 0 && m_selectedQueryIndexForTask < static_cast<int>(m_savedQueries.size()))
        {
            combo_preview = m_savedQueries[m_selectedQueryIndexForTask].name;
        }
        if (ImGui::BeginCombo("##TaskQuerySelectorCombo", combo_preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(m_savedQueries.size()); ++i)
            {
                if (ImGui::Selectable(m_savedQueries[i].name.c_str(), m_selectedQueryIndexForTask == i))
                    m_selectedQueryIndexForTask = i;
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool is_step_valid = (strlen(m_newTaskStepNameBuf) > 0) && (m_selectedQueryIndexForTask >= 0);
        if (!is_step_valid)
            ImGui::BeginDisabled();

        if (ImGui::Button((std::string(ICON_LC_PLUS_CIRCLE) + "  Ajouter cette étape au pipeline").c_str(), ImVec2(-1.0f, 30.0f)))
        {
            cppsqlx::UnitSelect currentUnit;
            currentUnit.name = m_newTaskStepNameBuf;
            currentUnit.quoted = false;
            cppsqlx::SelectQuery targetSelect;
            if (m_selectedQueryIndexForTask >= 0 && m_selectedQueryIndexForTask < static_cast<int>(m_savedQueries.size()))
            {
                const auto &selectedQuery = m_savedQueries[m_selectedQueryIndexForTask];
                if (!selectedQuery.compiledSelectJson.is_null() && !selectedQuery.compiledSelectJson.empty())
                {
                    auto selRes = cppsqlx::SelectQuery::fromJSON(selectedQuery.compiledSelectJson);
                    if (selRes.has_value())
                    {
                        targetSelect = std::move(selRes.value());
                    }
                }
            }
            if (m_selectedTargetTypeIndex == 0)
            {
                if (m_tableOperationMode == 0)
                    currentUnit.convertToCTAS(targetSelect);
                else
                    currentUnit.convertToInsert(targetSelect);
            }
            else
            {
                if (m_selectedTargetTypeIndex == 1)
                    currentUnit.targetType = cppsqlx::TargetType::View;
                if (m_selectedTargetTypeIndex == 2)
                    currentUnit.targetType = cppsqlx::TargetType::TemporaryTable;
                if (m_selectedTargetTypeIndex == 3)
                    currentUnit.targetType = cppsqlx::TargetType::SubQuery;
                currentUnit.query = targetSelect;
            }
            m_workingTaskContext.addStep(m_currentStepIndex, std::move(currentUnit));
            memset(m_newTaskStepNameBuf, 0, sizeof(m_newTaskStepNameBuf));
            m_currentStepIndex++;
        }
        if (!is_step_valid)
            ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        // Panneau droit : Récapitulatif des étapes
        ImGui::BeginChild("RightStepListPreviewPanel", ImVec2(preview_panel_width, panel_height), true);
        ImGui::TextColored(ImVec4(0.25f, 0.75f, 0.45f, 1.0f), "SÉQUENCE DES ÉTAPES DE LA TÂCHE");
        ImGui::Separator();
        ImGui::Spacing();
        if (m_workingTaskContext.steps().empty())
        {
            ImGui::TextDisabled("Aucune étape insérée pour le moment.\nUtilisez le panneau de gauche pour peupler la tâche.");
        }
        else
        {
            for (const auto &[stepId, unit] : m_workingTaskContext.steps())
            {
                ImGui::PushID(stepId);
                std::string type_str = "SELECT";
                if (unit.query.index() == 1)
                    type_str = "INSERT";
                if (unit.query.index() == 2)
                    type_str = "CTAS";
                ImGui::Bullet();
                ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "Étape %d :", stepId);
                ImGui::SameLine();
                ImGui::Text("[%s] -> %s", type_str.c_str(), unit.name.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 240.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_workingTaskContext = cppsqlx::Task();
            m_showCreateTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool is_task_ready = (strlen(m_newTaskNameBuf) > 0) && (!m_workingTaskContext.steps().empty());
        if (!is_task_ready)
            ImGui::BeginDisabled();
        if (ImGui::Button("Valider la Tâche", ImVec2(130.0f, 0.0f)))
        {
            m_savedTasks.push_back(std::move(m_workingTaskContext));
            m_workingTaskContext = cppsqlx::Task();
            memset(m_newTaskNameBuf, 0, sizeof(m_newTaskNameBuf));
            m_currentStepIndex = 1;
            m_showCreateTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!is_task_ready)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void Application::renderSaveProjectAsModal()
{
    if (m_showSaveProjectAsModal)
        ImGui::OpenPopup("Enregistrer le projet sous");
    ImGui::SetNextWindowSize(ImVec2(640.0f, 440.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 340.0f), ImVec2(1200.0f, 800.0f));
    if (ImGui::BeginPopupModal("Enregistrer le projet sous", &m_showSaveProjectAsModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Destination de sauvegarde du projet", ICON_LC_FOLDER_OPEN);
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button(".. (Parent)"))
        {
            if (m_browserCurrentDir.has_parent_path())
                m_browserCurrentDir = m_browserCurrentDir.parent_path();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_LC_PLUS_CIRCLE) + " Nouveau dossier").c_str()))
        {
            m_showNewFolderInput = !m_showNewFolderInput;
            memset(m_newFolderNameBuf, 0, sizeof(m_newFolderNameBuf));
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Répertoire actuel :");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_browserCurrentDir.string().c_str());
        if (m_showNewFolderInput)
        {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputTextWithHint("##NewFolderName", "Nom du nouveau répertoire...", m_newFolderNameBuf, IM_ARRAYSIZE(m_newFolderNameBuf));
            ImGui::SameLine();
            if (ImGui::Button("Créer") && strlen(m_newFolderNameBuf) > 0)
            {
                std::error_code ec;
                std::filesystem::path targetNewDir = m_browserCurrentDir / m_newFolderNameBuf;
                if (std::filesystem::create_directories(targetNewDir, ec))
                {
                    m_browserCurrentDir = targetNewDir;
                    m_showNewFolderInput = false;
                    memset(m_newFolderNameBuf, 0, sizeof(m_newFolderNameBuf));
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Annuler##CancelDir"))
                m_showNewFolderInput = false;
        }
        ImGui::Spacing();
        float listHeight = ImGui::GetContentRegionAvail().y - 85.0f;
        ImGui::BeginChild("DirectoryBrowserList", ImVec2(-1.0f, listHeight), true);
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> dirs;
        std::vector<std::filesystem::directory_entry> files;
        for (const auto &entry : std::filesystem::directory_iterator(m_browserCurrentDir, ec))
        {
            std::string fname = entry.path().filename().string();
            if (fname.starts_with("."))
                continue;
            if (entry.is_directory(ec))
                dirs.push_back(entry);
            else if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
                files.push_back(entry);
        }
        auto sortByName = [](const auto &a, const auto &b)
        { return a.path().filename() < b.path().filename(); };
        std::sort(dirs.begin(), dirs.end(), sortByName);
        std::sort(files.begin(), files.end(), sortByName);
        for (const auto &dir : dirs)
        {
            std::string label = std::string(ICON_LC_FOLDER_OPEN) + "  " + dir.path().filename().string();
            if (ImGui::Selectable(label.c_str(), false))
                m_browserCurrentDir = dir.path();
        }
        for (const auto &file : files)
        {
            std::string fname = file.path().filename().string();
            std::string label = std::string(ICON_LC_BOX) + "  " + fname;
            if (ImGui::Selectable(label.c_str(), fname == std::string(m_projectFileNameBuf)))
                strncpy(m_projectFileNameBuf, fname.c_str(), sizeof(m_projectFileNameBuf) - 1);
        }
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Nom du fichier :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##ProjectFileNameInput", m_projectFileNameBuf, IM_ARRAYSIZE(m_projectFileNameBuf));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showSaveProjectAsModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool is_name_valid = (strlen(m_projectFileNameBuf) > 0);
        if (!is_name_valid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
        {
            std::string fileName(m_projectFileNameBuf);
            if (fileName.find(".json") == std::string::npos)
                fileName += ".json";
            std::filesystem::path fullTargetPath = m_browserCurrentDir / fileName;
            if (saveProjectToFile(fullTargetPath.string()))
            {
                m_showSaveProjectAsModal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!is_name_valid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void Application::renderOpenProjectModal()
{
    if (m_showOpenProjectModal)
        ImGui::OpenPopup("Ouvrir un projet");
    ImGui::SetNextWindowSize(ImVec2(740.0f, 520.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 320.0f), ImVec2(1200.0f, 800.0f));
    if (ImGui::BeginPopupModal("Ouvrir un projet", &m_showOpenProjectModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Sélectionner un fichier projet (.json)", ICON_LC_FOLDER_OPEN);
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button(".. (Parent)"))
        {
            if (m_browserCurrentDir.has_parent_path())
                m_browserCurrentDir = m_browserCurrentDir.parent_path();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Répertoire :");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_browserCurrentDir.string().c_str());
        ImGui::Spacing();
        float listHeight = ImGui::GetContentRegionAvail().y - 75.0f;
        ImGui::BeginChild("OpenProjectBrowserList", ImVec2(-1.0f, listHeight), true);
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> dirs;
        std::vector<std::filesystem::directory_entry> files;
        for (const auto &entry : std::filesystem::directory_iterator(m_browserCurrentDir, ec))
        {
            std::string fname = entry.path().filename().string();
            if (fname.starts_with("."))
                continue;
            if (entry.is_directory(ec))
                dirs.push_back(entry);
            else if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
                files.push_back(entry);
        }
        auto sortByName = [](const auto &a, const auto &b)
        { return a.path().filename() < b.path().filename(); };
        std::sort(dirs.begin(), dirs.end(), sortByName);
        std::sort(files.begin(), files.end(), sortByName);
        for (const auto &dir : dirs)
        {
            std::string label = std::string(ICON_LC_FOLDER_OPEN) + "  " + dir.path().filename().string();
            if (ImGui::Selectable(label.c_str(), false))
                m_browserCurrentDir = dir.path();
        }
        for (const auto &file : files)
        {
            std::string fname = file.path().filename().string();
            std::string label = std::string(ICON_LC_BOX) + "  " + fname;
            bool isSelected = (fname == std::string(m_projectFileNameBuf));
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                strncpy(m_projectFileNameBuf, fname.c_str(), sizeof(m_projectFileNameBuf) - 1);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    std::filesystem::path fullPath = m_browserCurrentDir / fname;
                    if (loadProjectFromFile(fullPath.string()))
                    {
                        m_showOpenProjectModal = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::TextDisabled("Fichier sélectionné :");
        ImGui::SameLine();
        ImGui::Text("%s", strlen(m_projectFileNameBuf) > 0 ? m_projectFileNameBuf : "(Aucun)");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showOpenProjectModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool can_open = (strlen(m_projectFileNameBuf) > 0);
        if (!can_open)
            ImGui::BeginDisabled();
        if (ImGui::Button("Ouvrir", ImVec2(120.0f, 0.0f)))
        {
            std::filesystem::path fullPath = m_browserCurrentDir / std::string(m_projectFileNameBuf);
            if (loadProjectFromFile(fullPath.string()))
            {
                m_showOpenProjectModal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!can_open)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void Application::renderRenameModals()
{
    // 1. Modale : Renommer une requête
    if (m_showRenameQueryModal)
        ImGui::OpenPopup("Renommer la requête");
    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Renommer la requête", &m_showRenameQueryModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Nouveau nom de la requête :");
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameQueryInput", m_renameQueryBuf, IM_ARRAYSIZE(m_renameQueryBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);
        if (ImGui::Button("Annuler", ImVec2(95.0f, 0.0f)))
        {
            m_showRenameQueryModal = false;
            m_renamingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool isValid = (strlen(m_renameQueryBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Valider", ImVec2(110.0f, 0.0f)))
        {
            if (m_renamingQueryIndex >= 0 && m_renamingQueryIndex < static_cast<int>(m_savedQueries.size()))
            {
                std::string oldName = m_savedQueries[m_renamingQueryIndex].name;
                std::string newName(m_renameQueryBuf);

                if (!newName.empty() && oldName != newName)
                {
                    m_savedQueries[m_renamingQueryIndex].name = newName;

                    // Synchronise les nœuds CTE déjà ouverts sur le canevas
                    for (auto &vNode : m_visualQueryNodes)
                    {
                        if (vNode.isCTE && vNode.datasetName == oldName)
                        {
                            vNode.datasetName = newName;
                        }
                    }
                }
            }
            m_showRenameQueryModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    // 2. Modale : Renommer un workflow
    if (m_showRenameWorkflowModal)
        ImGui::OpenPopup("Renommer le workflow");
    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Renommer le workflow", &m_showRenameWorkflowModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Nouveau nom du workflow :");
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameWorkflowInput", m_renameWorkflowBuf, IM_ARRAYSIZE(m_renameWorkflowBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);
        if (ImGui::Button("Annuler", ImVec2(95.0f, 0.0f)))
        {
            m_showRenameWorkflowModal = false;
            m_renamingWorkflowIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool isValid = (strlen(m_renameWorkflowBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Valider", ImVec2(105.0f, 0.0f)) || (enterPressed && isValid))
        {
            if (m_renamingWorkflowIndex >= 0 && m_renamingWorkflowIndex < static_cast<int>(m_savedWorkflows.size()))
            {
                std::string newName(m_renameWorkflowBuf);
                auto &wf = m_savedWorkflows[m_renamingWorkflowIndex];
                wf.name = newName;
                try
                {
                    nlohmann::json j = nlohmann::json::parse(wf.jsonPreview);
                    if (j.contains("workflow"))
                        j["workflow"]["name"] = newName;
                    else
                        j["name"] = newName;
                    wf.jsonPreview = j.dump(4);
                }
                catch (...)
                {
                }
            }
            m_showRenameWorkflowModal = false;
            m_renamingWorkflowIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void Application::renderStatusBar()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    float barHeight = ImGui::GetFrameHeight() + 8.0f;

    ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    if (ImGui::BeginViewportSideBar("##DizuiStatusBar", viewport, ImGuiDir_Down, barHeight, barFlags))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::TextDisabled("%s Outil connecté : %s | Environnement : %s | Graphe : %d nœuds actifs",
                                ICON_LC_ACTIVITY,
                                m_selectedGateway.empty() ? "Aucun" : m_selectedGateway.c_str(),
                                m_selectedContext.empty() ? "Aucun" : m_selectedContext.c_str(),
                                static_cast<int>(m_visualQueryNodes.size()));

            // Alignement de la case à cocher Console tout à droite
            float checkWidth = 115.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > checkWidth)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - checkWidth);
            }

            if (ImGui::Checkbox((std::string(ICON_LC_TERMINAL) + " Console").c_str(), &m_showConsolePanel))
            {
                if (m_showConsolePanel)
                {
                    ImGui::SetWindowFocus("Console & Journaux d'Exécution");
                }
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();
}

void Application::renderConnectionModals()
{
    if (!m_activeProject)
        return;

    // =========================================================================
    // MODALE 1 : CRÉER / ÉDITER UNE CONNEXION PHYSIQUE (AVEC TEST DE CONNEXION)
    // =========================================================================
    if (m_showPhysConnModal)
        ImGui::OpenPopup("Configuration Connexion Physique");

    ImGui::SetNextWindowSize(ImVec2(580.0f, 480.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Configuration Connexion Physique", &m_showPhysConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_SERVER,
                           m_isEditingPhysConn ? "Éditer la connexion PostgreSQL" : "Nouvelle connexion PostgreSQL");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Nom de la connexion :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PhysName", "ex: pg_dev_local", m_physConnNameBuf, IM_ARRAYSIZE(m_physConnNameBuf));

        ImGui::Spacing();

        // Ligne Hôte + Port
        ImGui::BeginGroup();
        ImGui::Text("Hôte (Host) :");
        ImGui::SetNextItemWidth(310.0f);
        ImGui::InputText("##PhysHost", m_physConnHostBuf, IM_ARRAYSIZE(m_physConnHostBuf));
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::Text("Port :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("##PhysPort", &m_physConnPort, 0, 0);
        ImGui::EndGroup();

        ImGui::Spacing();

        ImGui::Text("Base de données (dbname) :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PhysDbName", "ex: t_financedb", m_physConnDbNameBuf, IM_ARRAYSIZE(m_physConnDbNameBuf));

        ImGui::Spacing();

        ImGui::Text("Utilisateur (user) :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##PhysUser", m_physConnUserBuf, IM_ARRAYSIZE(m_physConnUserBuf));

        ImGui::Spacing();

        // Sélecteur Mode SSL
        const char *sslModes[] = {"disable", "allow", "prefer", "require", "verify-ca", "verify-full"};
        ImGui::Text("Sécurité SSL (sslmode) :");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##PhysSslMode", sslModes[m_physConnSslModeIndex]))
        {
            for (int i = 0; i < 6; ++i)
            {
                if (ImGui::Selectable(sslModes[i], m_physConnSslModeIndex == i))
                    m_physConnSslModeIndex = i;
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        ImGui::Text("Mot de passe (optionnel) :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##PhysPass", m_physConnPassBuf, IM_ARRAYSIZE(m_physConnPassBuf), ImGuiInputTextFlags_Password);

        ImGui::Spacing();

        // Bouton de test de connexion en direct
        if (ImGui::Button((std::string(ICON_LC_PLAY) + "  Tester la connexion").c_str()))
        {
            // Dans "Tester la connexion" :
            std::string connStr = "dbname=" + std::string(m_physConnDbNameBuf) +
                                  " user=" + std::string(m_physConnUserBuf) +
                                  " host=" + std::string(m_physConnHostBuf) +
                                  " port=" + std::to_string(m_physConnPort) +
                                  " sslmode=" + std::string(sslModes[m_physConnSslModeIndex]);
            if (strlen(m_physConnPassBuf) > 0)
                connStr += " password=" + std::string(m_physConnPassBuf);

            cppsqlx::Connection testConn(connStr);
            if (auto res = testConn.connect(); res.has_value())
            {
                m_physConnTestOk = true;
                m_physConnTestStatus = "Connexion réussie !";
                testConn.close();
            }
            else
            {
                m_physConnTestOk = false;
                m_physConnTestStatus = "Échec : " + res.error();
            }
        }

        if (!m_physConnTestStatus.empty())
        {
            ImGui::SameLine();
            ImVec4 statusCol = m_physConnTestOk ? ImVec4(0.3f, 0.85f, 0.45f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
            ImGui::TextColored(statusCol, "%s", m_physConnTestStatus.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);

        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showPhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool canSavePhys = (strlen(m_physConnNameBuf) > 0) && (strlen(m_physConnDbNameBuf) > 0);
        if (!canSavePhys)
            ImGui::BeginDisabled();

        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
        {
            std::string newName(m_physConnNameBuf);
            cppsqlx::PhysicalConnection phys;
            phys.name_ = newName;
            phys.dbhost_ = std::string(m_physConnHostBuf);
            phys.dbport_ = m_physConnPort;
            phys.dbname_ = std::string(m_physConnDbNameBuf);
            phys.dbuser_ = std::string(m_physConnUserBuf);
            phys.dbpassword_ = std::string(m_physConnPassBuf);
            phys.sslmode_ = std::string(sslModes[m_physConnSslModeIndex]);

            if (m_isEditingPhysConn && !m_originalPhysConnName.empty() && m_originalPhysConnName != newName)
            {
                m_activeProject->physicalConnections().erase(m_originalPhysConnName);
            }

            m_activeProject->physicalConnections()[newName] = phys;
            m_activeProject->resolveDependencies();

            m_showPhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canSavePhys)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE 2 : RENOMMER UNE CONNEXION PHYSIQUE
    // =========================================================================
    if (m_showRenamePhysConnModal)
        ImGui::OpenPopup("Renommer la connexion physique");

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Renommer la connexion physique", &m_showRenamePhysConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Nouveau nom de la connexion physique :");
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenamePhysInput", m_renamePhysConnBuf, IM_ARRAYSIZE(m_renamePhysConnBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);

        if (ImGui::Button("Annuler##PhysRen", ImVec2(95.0f, 0.0f)))
        {
            m_showRenamePhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool isValid = (strlen(m_renamePhysConnBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();

        if (ImGui::Button("Valider##PhysRen", ImVec2(105.0f, 0.0f)) || (enterPressed && isValid))
        {
            std::string newName(m_renamePhysConnBuf);
            if (!m_renamingPhysConnOldName.empty() && m_renamingPhysConnOldName != newName &&
                m_activeProject->physicalConnections().contains(m_renamingPhysConnOldName))
            {
                cppsqlx::PhysicalConnection phys = m_activeProject->physicalConnections().at(m_renamingPhysConnOldName);
                phys.name_ = newName;
                m_activeProject->physicalConnections().erase(m_renamingPhysConnOldName);
                m_activeProject->physicalConnections()[newName] = std::move(phys);
                m_activeProject->resolveDependencies();
            }
            m_showRenamePhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE 3 : CRÉER / ÉDITER UNE CONNEXION LOGIQUE (GATEWAY DEV / TEST / PROD)
    // =========================================================================
    if (m_showLogConnModal)
        ImGui::OpenPopup("Configuration Connexion Logique");

    ImGui::SetNextWindowSize(ImVec2(480.0f, 310.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Configuration Connexion Logique", &m_showLogConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_DATABASE,
                           m_isEditingLogConn ? "Éditer la passerelle logique" : "Nouvelle passerelle logique");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Nom de la passerelle (Gateway) :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##LogName", "ex: DWH_FINANCE", m_logConnNameBuf, IM_ARRAYSIZE(m_logConnNameBuf));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Sélecteur réutilisable pour chaque environnement (DEV, TEST, PROD)
        auto drawEnvSelector = [&](const char *envLabel, std::string &targetPhysName)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%-6s ->", envLabel);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);

            const char *preview = targetPhysName.empty() ? "(Aucune)" : targetPhysName.c_str();
            if (ImGui::BeginCombo((std::string("##EnvCombo_") + envLabel).c_str(), preview))
            {
                if (ImGui::Selectable("(Aucune)", targetPhysName.empty()))
                    targetPhysName = "";

                for (const auto &[pName, pObj] : m_activeProject->physicalConnections())
                {
                    if (ImGui::Selectable(pName.c_str(), targetPhysName == pName))
                        targetPhysName = pName;
                }
                ImGui::EndCombo();
            }
        };

        drawEnvSelector("DEV", m_logConnDevTarget);
        ImGui::Spacing();
        drawEnvSelector("TEST", m_logConnTestTarget);
        ImGui::Spacing();
        drawEnvSelector("PROD", m_logConnProdTarget);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);

        if (ImGui::Button("Annuler##LogCfg", ImVec2(100.0f, 0.0f)))
        {
            m_showLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool canSaveLog = (strlen(m_logConnNameBuf) > 0);
        if (!canSaveLog)
            ImGui::BeginDisabled();
        if (ImGui::Button("Enregistrer##LogCfg", ImVec2(120.0f, 0.0f)))
        {
            std::string gwName(m_logConnNameBuf);
            cppsqlx::LogicalConnection gw(gwName);
            auto assignEnvIfValid = [&](const std::string &envName, const std::string &physName)
            {
                if (!physName.empty() && m_activeProject->physicalConnections().contains(physName))
                {
                    gw.registerEnvironment(envName, m_activeProject->physicalConnections().at(physName));
                }
            };

            assignEnvIfValid("DEV", m_logConnDevTarget);
            assignEnvIfValid("TEST", m_logConnTestTarget);
            assignEnvIfValid("PROD", m_logConnProdTarget);
            if (m_isEditingLogConn && !m_originalLogConnName.empty() && m_originalLogConnName != gwName)
            {
                m_activeProject->logicalConnections().erase(m_originalLogConnName);
            }
            m_activeProject->logicalConnections()[gwName] = std::move(gw);
            m_activeProject->resolveDependencies();
            m_showLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canSaveLog)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    // =========================================================================
    // MODALE 4 : RENOMMER UNE CONNEXION LOGIQUE
    // =========================================================================
    if (m_showRenameLogConnModal)
        ImGui::OpenPopup("Renommer la connexion logique");
    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Renommer la connexion logique", &m_showRenameLogConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Nouveau nom de la passerelle logique :");
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameLogInput", m_renameLogConnBuf, IM_ARRAYSIZE(m_renameLogConnBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);
        if (ImGui::Button("Annuler##LogRen", ImVec2(95.0f, 0.0f)))
        {
            m_showRenameLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool isValid = (strlen(m_renameLogConnBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Valider##LogRen", ImVec2(105.0f, 0.0f)) || (enterPressed && isValid))
        {
            std::string newName(m_renameLogConnBuf);
            if (!m_renamingLogConnOldName.empty() && m_renamingLogConnOldName != newName &&
                m_activeProject->logicalConnections().contains(m_renamingLogConnOldName))
            {
                cppsqlx::LogicalConnection gw = m_activeProject->logicalConnections().at(m_renamingLogConnOldName);
                gw.setName(newName);
                m_activeProject->logicalConnections().erase(m_renamingLogConnOldName);
                m_activeProject->logicalConnections()[newName] = std::move(gw);
                m_activeProject->resolveDependencies();
                if (m_selectedGateway == m_renamingLogConnOldName)
                    m_selectedGateway = newName;
            }
            m_showRenameLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}


