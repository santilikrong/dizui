#include "Application.hpp"
#include <Localization.hpp>
#include "AboutModal.hpp"
#include "SqlEditorHelpers.hpp"
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

    Localization::instance().loadLanguage(AppLanguage::FR);

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

        // Dans if (init_layout && !ImGui::DockBuilderGetNode(dockspace_id)) :
        ImGui::DockBuilderDockWindow("Outils", dock_left_id);
        ImGui::DockBuilderDockWindow("Panneau de Contrôle", dock_center_id);
        ImGui::DockBuilderDockWindow("Espace de Travail###MainWorkspacePanel", dock_main_id);

        ImGui::DockBuilderDockWindow("Console & Journaux d'Exécution###ConsoleBottomPanel", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Résultat du Graphe Visuel###ResultsBottomPanel", dock_bottom_id);
        ImGui::DockBuilderDockWindow("Assistant IA (Ollama)###AiAssistantBottomPanel", dock_bottom_id);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && m_rawSqlEditorBuf[0] != '\0')
        {
            std::string formatted = dizui::sql::formatRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
            snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", formatted.c_str());
            addConsoleLog(tr("edit_menu.log_formatted"), LogLevel::Info);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_C, false) && !m_visualQueryNodes.empty())
        {
            std::string compiledSql = dizui::sql::formatRawSqlQuery(compileVisualGraphToSelectQuery().toSQL());
            ImGui::SetClipboardText(compiledSql.c_str());
            addConsoleLog(tr("edit_menu.log_copied_clipboard"), LogLevel::Info);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !m_visualQueryNodes.empty())
        {
            std::string compiledSql = dizui::sql::formatRawSqlQuery(compileVisualGraphToSelectQuery().toSQL());
            snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", compiledSql.c_str());
            addConsoleLog(tr("cte_stack.log_sql_copied"), LogLevel::Info);
        }
    }

    // 1. Dessin de la barre supérieure (Menu Bar principal)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    if (ImGui::BeginMainMenuBar())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s", ICON_LC_CPU);
        ImGui::Spacing();
        if (ImGui::BeginMenu(tr("menu.file")))
        {
            if (ImGui::MenuItem((std::string(ICON_LC_FILE_PLUS) + "  " + tr("menu.new_project")).c_str(), "Ctrl+N"))
            {
                createNewProject(tr("project.default_name"));
            }
            ImGui::Separator();

            if (ImGui::MenuItem((std::string(ICON_LC_FOLDER_OPEN) + "  " + tr("menu.open")).c_str(), "Ctrl+O"))
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
            if (ImGui::MenuItem((std::string(ICON_LC_SAVE) + "  " + tr("menu.save")).c_str(), "Ctrl+S"))
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
            if (ImGui::MenuItem((std::string(ICON_LC_SAVE) + "  " + tr("menu.save_as")).c_str(), "Ctrl+Shift+S"))
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

            ImGui::Separator();
            if (ImGui::MenuItem((std::string(ICON_LC_LOG_OUT) + "  " + tr("menu.quit")).c_str(), "Ctrl+Q"))
            {
                if (GLFWwindow *currentWin = glfwGetCurrentContext())
                {
                    glfwSetWindowShouldClose(currentWin, GLFW_TRUE);
                }
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("menu.edit")))
        {
            if (ImGui::BeginMenu(tr("menu.edit")))
            {
                bool hasRawSql = (m_rawSqlEditorBuf[0] != '\0');
                bool hasVisualNodes = !m_visualQueryNodes.empty();

                // 1. Formater le SQL de l'éditeur
                if (ImGui::MenuItem((std::string(ICON_LC_ALIGN_LEFT) + "  " + tr("edit_menu.format_sql")).c_str(), "Ctrl+Shift+F", false, hasRawSql))
                {
                    std::string formatted = dizui::sql::formatRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
                    snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", formatted.c_str());
                    addConsoleLog(tr("edit_menu.log_formatted"), LogLevel::Info);
                }

                // 2. Copier le SQL compilé dans le presse-papiers système
                if (ImGui::MenuItem((std::string(ICON_LC_COPY) + "  " + tr("edit_menu.copy_compiled_sql")).c_str(), "Ctrl+Shift+C", false, hasVisualNodes))
                {
                    std::string compiledSql = dizui::sql::formatRawSqlQuery(compileVisualGraphToSelectQuery().toSQL());
                    ImGui::SetClipboardText(compiledSql.c_str());
                    addConsoleLog(tr("edit_menu.log_copied_clipboard"), LogLevel::Info);
                }

                // 3. Synchroniser le graphe visuel vers l'éditeur SQL texte
                if (ImGui::MenuItem((std::string(ICON_LC_REFRESH_CW) + "  " + tr("edit_menu.sync_from_designer")).c_str(), "Ctrl+Shift+S", false, hasVisualNodes))
                {
                    std::string compiledSql = dizui::sql::formatRawSqlQuery(compileVisualGraphToSelectQuery().toSQL());
                    snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", compiledSql.c_str());
                    addConsoleLog(tr("cte_stack.log_sql_copied"), LogLevel::Info);
                }

                ImGui::Separator();

                // 4. Vider l'éditeur SQL texte
                if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  " + tr("edit_menu.clear_sql_editor")).c_str(), nullptr, false, hasRawSql))
                {
                    m_rawSqlEditorBuf.fill('\0');
                    addConsoleLog(tr("edit_menu.log_cleared"), LogLevel::Info);
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("menu.view")))
        {
            if (ImGui::MenuItem((std::string(ICON_LC_TERMINAL) + "  " + tr("menu.toggle_console")).c_str(), nullptr, m_showConsolePanel))
            {
                m_showConsolePanel = !m_showConsolePanel;
                if (m_showConsolePanel)
                {
                    ImGui::SetWindowFocus("Console & Journaux d'Exécution###ConsoleBottomPanel");
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("menu.language")))
        {
            bool isFr = (Localization::instance().currentLanguage() == AppLanguage::FR);
            if (ImGui::MenuItem("Français", nullptr, isFr))
                Localization::instance().loadLanguage(AppLanguage::FR);

            if (ImGui::MenuItem("English", nullptr, !isFr))
                Localization::instance().loadLanguage(AppLanguage::EN);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(tr("menu.help")))
        {
            if (ImGui::MenuItem((std::string(ICON_LC_INFO) + "  " + tr("menu.about")).c_str()))
            {
                m_showAboutModal = true;
            }

            ImGui::EndMenu();
        }

        if (m_activeProject)
        {
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(20.0f, 0.0f));
            ImGuiIO &io = ImGui::GetIO();
            ImFont *small_font_ptr = (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[0] : nullptr;

            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("toolbar.connection"));
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4.0f, 0.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(250.0f);

            // Réinitialise automatiquement la passerelle si elle est vide ou absente du projet actif
            const auto &logConns = m_activeProject->logicalConnections();
            if (!logConns.empty() && (m_selectedGateway.empty() || !logConns.contains(m_selectedGateway)))
            {
                m_selectedGateway = logConns.begin()->first;
                m_selectedContext.clear();
            }

            if (small_font_ptr)
                ImGui::PushFont(small_font_ptr);
            const char *gatewayPreview = m_selectedGateway.empty() ? tr("toolbar.connection_placeholder") : m_selectedGateway.c_str();
            if (ImGui::BeginCombo("##GatewayCombo", gatewayPreview))
            {
                for (const auto &[name, logConn] : logConns)
                {
                    if (ImGui::Selectable(name.c_str(), m_selectedGateway == name))
                    {
                        m_selectedGateway = name;
                        m_selectedContext.clear();
                    }
                }
                ImGui::EndCombo();
            }
            if (small_font_ptr)
                ImGui::PopFont();

            ImGui::SameLine();
            ImGui::Dummy(ImVec2(15.0f, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("toolbar.context"));
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(4.0f, 0.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);

            std::vector<std::string> available_contexts;
            if (logConns.contains(m_selectedGateway))
            {
                const auto &currentGateway = logConns.at(m_selectedGateway);
                for (const auto &[envName, physConn] : currentGateway.environments())
                    available_contexts.push_back(envName);

                // Sélectionne le premier contexte disponible si m_selectedContext est vide ou invalide
                if (!available_contexts.empty() &&
                    std::find(available_contexts.begin(), available_contexts.end(), m_selectedContext) == available_contexts.end())
                {
                    m_selectedContext = available_contexts.front();
                }
            }

            if (small_font_ptr)
                ImGui::PushFont(small_font_ptr);
            const char *contextPreview = m_selectedContext.empty() ? tr("toolbar.context_placeholder") : m_selectedContext.c_str();
            if (ImGui::BeginCombo("##ContextCombo", contextPreview))
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
    renderQueryFolderModals();
    renderAiAssistantPanel();
    renderGlobalModals();
    dizui::ui::renderAboutModal(m_showAboutModal);
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

    drawToolbarButtonLocal(ICON_LC_BLOCKS, tr("activity.project"), ActiveTab::Project);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_SEARCH, tr("activity.search"), ActiveTab::Search);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_GIT_BRANCH, tr("activity.git"), ActiveTab::Git);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_BOX, tr("activity.package"), ActiveTab::Package);
    ImGui::Spacing();
    drawToolbarButtonLocal(ICON_LC_SETTINGS, tr("activity.settings"), ActiveTab::Settings);
    ImGui::PopStyleColor(2);
    ImGui::End();
}

void Application::renderMainContentZone()
{
    ImGuiWindowFlags workspace_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove;
    std::string workspaceTitle = std::string(tr("panel.workspace")) + "###MainWorkspacePanel";
    ImGui::Begin(workspaceTitle.c_str(), nullptr, workspace_flags);

    ImGuiWindow *current_win = ImGui::GetCurrentWindow();
    if (current_win && current_win->DockNode)
        current_win->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

    bool isQueryVisualActive = false;

    if (ImGui::BeginTabBar("DesignerTabBar", ImGuiTabBarFlags_None))
    {
        std::string wfTab = std::string(ICON_LC_ACTIVITY) + "  " + tr("tab.workflow_designer") + "###TabWorkflow";
        if (ImGui::BeginTabItem(wfTab.c_str()))
        {
            if (ImGui::IsItemActivated())
            {
                m_showConsolePanel = true;
                ImGui::SetWindowFocus("###ConsoleBottomPanel");
            }
            renderWorkflowEditor();
            ImGui::EndTabItem();
        }

        std::string qvTab = std::string(ICON_LC_SEARCH) + "  " + tr("tab.query_visual_designer") + "###TabQueryVisual";
        if (ImGui::BeginTabItem(qvTab.c_str()))
        {
            isQueryVisualActive = true;
            if (ImGui::IsItemActivated())
            {
                m_focusQueryResultsTab = true;
            }
            renderQueryEditor();
            ImGui::EndTabItem();
        }

        std::string qtTab = std::string(ICON_LC_BOX) + "  " + tr("tab.query_text_editor") + "###TabQueryText";
        if (ImGui::BeginTabItem(qtTab.c_str()))
        {
            isQueryVisualActive = true;
            if (ImGui::IsItemActivated())
            {
                m_focusQueryResultsTab = true;
            }
            renderQueryTextEditor();
            ImGui::EndTabItem();
        }

        std::string cteTab = std::string(ICON_LC_LAYERS) + "  " + tr("tab.cte_pipeline") + "###TabCtePipeline";
        if (ImGui::BeginTabItem(cteTab.c_str()))
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
    std::string modalTitle = std::string(tr("reverse.modal_title")) + "###ReverseEngModal";

    if (m_showReverseModal)
        ImGui::OpenPopup(modalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(900.0f, 550.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(650.0f, 400.0f), ImVec2(1920.0f, 1080.0f));

    if (ImGui::BeginPopupModal(modalTitle.c_str(), &m_showReverseModal, ImGuiWindowFlags_None))
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
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", tr("reverse.conn_error"));
            if (ImGui::Button(tr("btn.close")))
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
                    m_reverseCheckedObjects.clear();
                    m_cachedTables.clear();
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
        ImGui::InputTextWithHint("##TableSearchFilter", tr("reverse.filter_hint"), tableFilterBuf, IM_ARRAYSIZE(tableFilterBuf));
        ImGui::Spacing();

        // Construction de la liste filtrée
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

        // Boutons rapides Tout cocher / Tout décocher (agissent sur les tables visibles/filtrées)
        if (ImGui::SmallButton(tr("reverse.btn_select_all")))
        {
            for (const std::string *tblPtr : filteredTables)
                m_reverseCheckedObjects.insert(*tblPtr);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("reverse.btn_deselect_all")))
        {
            for (const std::string *tblPtr : filteredTables)
                m_reverseCheckedObjects.erase(*tblPtr);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", m_reverseCheckedObjects.size());
        ImGui::Spacing();

        std::string tablesNodeLabel = std::string(tr("reverse.tables_node")) + "###ReverseTablesNode";
        if (ImGui::TreeNodeEx(tablesNodeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filteredTables.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const std::string &tbl = *filteredTables[row];
                    ImGui::PushID(row);

                    bool isChecked = m_reverseCheckedObjects.contains(tbl);
                    if (ImGui::Checkbox("##chk_tbl", &isChecked))
                    {
                        if (isChecked)
                            m_reverseCheckedObjects.insert(tbl);
                        else
                            m_reverseCheckedObjects.erase(tbl);
                    }

                    ImGui::SameLine();
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
            ImGui::TableSetupColumn(tr("reverse.col_name"));
            ImGui::TableSetupColumn(tr("reverse.col_type"));
            ImGui::TableSetupColumn(tr("reverse.col_null"));
            ImGui::TableSetupColumn(tr("reverse.col_pk"));
            ImGui::TableHeadersRow();

            for (const auto &col : m_cachedStructure)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", col.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", col.dataType.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", col.isNullable ? tr("common.yes") : tr("common.no"));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", col.isPrimaryKey ? "[PK]" : "-");
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Prépare la liste des tables à importer (toutes les cases cochées, ou à défaut la table sélectionnée)
        std::vector<std::string> targetsToImport(m_reverseCheckedObjects.begin(), m_reverseCheckedObjects.end());
        if (targetsToImport.empty() && !m_reverseSelectedObject.empty())
        {
            targetsToImport.push_back(m_reverseSelectedObject);
        }

        bool canImport = (!targetsToImport.empty() && m_activeProject != nullptr);
        if (!canImport)
            ImGui::BeginDisabled();

        std::string importLabel = std::string(tr("reverse.btn_import")) +
                                  " (" + std::to_string(targetsToImport.size()) + ")###ImportReverseBtn";
        if (ImGui::Button(importLabel.c_str()))
        {
            cppsqlx::DatasetFactory factory;
            int importedCount = 0;

            for (const auto &tblName : targetsToImport)
            {
                auto datasetRes = factory.createFromDatabase(*modalConn, tblName, m_reverseSelectedSchema);
                if (datasetRes)
                {
                    m_activeProject->addDataset(std::move(datasetRes.value()));
                    ++importedCount;
                }
            }

            addConsoleLog(std::to_string(importedCount) + " " + tr("reverse.log_imported_count"), LogLevel::Success);
            m_reverseCheckedObjects.clear();
            m_showReverseModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canImport)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(tr("btn.cancel")))
        {
            m_reverseCheckedObjects.clear();
            m_showReverseModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::renderCreateTaskModal()
{
    std::string modalTitle = std::string(tr("task.modal_title")) + "###CreateTaskModal";

    if (m_showCreateTaskModal)
        ImGui::OpenPopup(modalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(950.0f, 550.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(750.0f, 400.0f), ImVec2(1920.0f, 1080.0f));

    if (ImGui::BeginPopupModal(modalTitle.c_str(), &m_showCreateTaskModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_ACTIVITY, tr("task.modal_subtitle"));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s", tr("task.name_label"));
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
        ImGui::TextColored(ImVec4(0.55f, 0.35f, 0.9f, 1.0f), "%s", tr("task.build_step_header"));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s", tr("task.step_index_label"));
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("##StepIndexInput", &m_currentStepIndex, 1, 1);
        if (m_currentStepIndex < 1)
            m_currentStepIndex = 1;

        ImGui::Spacing();

        ImGui::Text("%s", tr("task.target_name_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##StepNameInput", m_newTaskStepNameBuf, IM_ARRAYSIZE(m_newTaskStepNameBuf));

        ImGui::Spacing();

        ImGui::Text("%s", tr("task.target_type_label"));
        ImGui::SetNextItemWidth(-1.0f);
        const char *target_types_labels[] = {
            tr("task.target_table"),
            tr("task.target_view"),
            tr("task.target_temp_table"),
            tr("task.target_subquery")};
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
            ImGui::TextDisabled("%s", tr("task.write_strategy_label"));
            ImGui::RadioButton(tr("task.strategy_ctas"), &m_tableOperationMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton(tr("task.strategy_insert"), &m_tableOperationMode, 1);
            ImGui::Spacing();
        }

        ImGui::Spacing();

        ImGui::Text("%s", tr("task.select_query_label"));
        ImGui::SetNextItemWidth(-1.0f);
        std::string combo_preview = tr("task.select_query_placeholder");
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

        if (ImGui::Button((std::string(ICON_LC_PLUS_CIRCLE) + "  " + tr("task.btn_add_step")).c_str(), ImVec2(-1.0f, 30.0f)))
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
        ImGui::TextColored(ImVec4(0.25f, 0.75f, 0.45f, 1.0f), "%s", tr("task.steps_sequence_header"));
        ImGui::Separator();
        ImGui::Spacing();
        if (m_workingTaskContext.steps().empty())
        {
            ImGui::TextDisabled("%s", tr("task.no_steps_hint"));
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
                ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s %d :", tr("task.step_prefix"), stepId);
                ImGui::SameLine();
                ImGui::Text("[%s] -> %s", type_str.c_str(), unit.name.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 240.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_workingTaskContext = cppsqlx::Task();
            m_showCreateTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool is_task_ready = (strlen(m_newTaskNameBuf) > 0) && (!m_workingTaskContext.steps().empty());
        if (!is_task_ready)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("task.btn_validate"), ImVec2(130.0f, 0.0f)))
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
    std::string modalTitle = std::string(tr("save_as.modal_title")) + "###SaveProjectAsModal";

    if (m_showSaveProjectAsModal)
        ImGui::OpenPopup(modalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(640.0f, 440.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 340.0f), ImVec2(1200.0f, 800.0f));

    if (ImGui::BeginPopupModal(modalTitle.c_str(), &m_showSaveProjectAsModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_FOLDER_OPEN, tr("save_as.subtitle"));
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(tr("browser.parent_dir")))
        {
            if (m_browserCurrentDir.has_parent_path())
                m_browserCurrentDir = m_browserCurrentDir.parent_path();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_LC_PLUS_CIRCLE) + " " + tr("browser.new_folder")).c_str()))
        {
            m_showNewFolderInput = !m_showNewFolderInput;
            memset(m_newFolderNameBuf, 0, sizeof(m_newFolderNameBuf));
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("browser.current_dir"));
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_browserCurrentDir.string().c_str());

        if (m_showNewFolderInput)
        {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputTextWithHint("##NewFolderName", tr("browser.new_folder_hint"), m_newFolderNameBuf, IM_ARRAYSIZE(m_newFolderNameBuf));
            ImGui::SameLine();
            if (ImGui::Button(tr("btn.create")) && strlen(m_newFolderNameBuf) > 0)
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
            std::string cancelDirBtn = std::string(tr("btn.cancel")) + "##CancelDir";
            if (ImGui::Button(cancelDirBtn.c_str()))
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
        ImGui::Text("%s", tr("browser.file_name"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##ProjectFileNameInput", m_projectFileNameBuf, IM_ARRAYSIZE(m_projectFileNameBuf));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showSaveProjectAsModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool is_name_valid = (strlen(m_projectFileNameBuf) > 0);
        if (!is_name_valid)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.save"), ImVec2(120.0f, 0.0f)))
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
    std::string modalTitle = std::string(tr("open_project.modal_title")) + "###OpenProjectModal";

    if (m_showOpenProjectModal)
        ImGui::OpenPopup(modalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(740.0f, 520.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 320.0f), ImVec2(1200.0f, 800.0f));

    if (ImGui::BeginPopupModal(modalTitle.c_str(), &m_showOpenProjectModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_FOLDER_OPEN, tr("open_project.subtitle"));
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(tr("browser.parent_dir")))
        {
            if (m_browserCurrentDir.has_parent_path())
                m_browserCurrentDir = m_browserCurrentDir.parent_path();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr("browser.current_dir"));
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
        ImGui::TextDisabled("%s", tr("open_project.selected_file"));
        ImGui::SameLine();
        ImGui::Text("%s", strlen(m_projectFileNameBuf) > 0 ? m_projectFileNameBuf : tr("common.none"));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showOpenProjectModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool can_open = (strlen(m_projectFileNameBuf) > 0);
        if (!can_open)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.open"), ImVec2(120.0f, 0.0f)))
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
    std::string renameQueryTitle = std::string(tr("rename.query_modal_title")) + "###RenameQueryModal";
    if (m_showRenameQueryModal)
        ImGui::OpenPopup(renameQueryTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(renameQueryTitle.c_str(), &m_showRenameQueryModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("rename.query_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameQueryInput", m_renameQueryBuf, IM_ARRAYSIZE(m_renameQueryBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(95.0f, 0.0f)))
        {
            m_showRenameQueryModal = false;
            m_renamingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool isValid = (strlen(m_renameQueryBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.confirm"), ImVec2(110.0f, 0.0f)) || (enterPressed && isValid))
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
            m_renamingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // 2. Modale : Renommer un workflow
    std::string renameWfTitle = std::string(tr("rename.workflow_modal_title")) + "###RenameWorkflowModal";
    if (m_showRenameWorkflowModal)
        ImGui::OpenPopup(renameWfTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(renameWfTitle.c_str(), &m_showRenameWorkflowModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("rename.workflow_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameWorkflowInput", m_renameWorkflowBuf, IM_ARRAYSIZE(m_renameWorkflowBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(95.0f, 0.0f)))
        {
            m_showRenameWorkflowModal = false;
            m_renamingWorkflowIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool isValid = (strlen(m_renameWorkflowBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.confirm"), ImVec2(105.0f, 0.0f)) || (enterPressed && isValid))
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
            ImGui::TextDisabled("%s %s : %s | %s : %s | %s : %d %s",
                                ICON_LC_ACTIVITY,
                                tr("status.connected_tool"),
                                m_selectedGateway.empty() ? tr("status.none") : m_selectedGateway.c_str(),
                                tr("status.environment"),
                                m_selectedContext.empty() ? tr("status.none") : m_selectedContext.c_str(),
                                tr("status.graph"),
                                static_cast<int>(m_visualQueryNodes.size()),
                                tr("status.active_nodes"));

            // Alignement de la case à cocher Console tout à droite
            float checkWidth = 115.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > checkWidth)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - checkWidth);
            }

            std::string consoleCheckLabel = std::string(ICON_LC_TERMINAL) + " " + tr("status.console") + "###StatusBarConsoleCheck";
            if (ImGui::Checkbox(consoleCheckLabel.c_str(), &m_showConsolePanel))
            {
                if (m_showConsolePanel)
                {
                    ImGui::SetWindowFocus("Console & Journaux d'Exécution###ConsoleBottomPanel");
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
    std::string physModalTitle = std::string(tr("conn.phys_modal_title")) + "###PhysConnModal";
    if (m_showPhysConnModal)
        ImGui::OpenPopup(physModalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(580.0f, 480.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(physModalTitle.c_str(), &m_showPhysConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_SERVER,
                           m_isEditingPhysConn ? tr("conn.phys_edit_subtitle") : tr("conn.phys_new_subtitle"));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s", tr("conn.phys_name_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PhysName", tr("conn.phys_name_hint"), m_physConnNameBuf, IM_ARRAYSIZE(m_physConnNameBuf));

        ImGui::Spacing();

        // Ligne Hôte + Port
        ImGui::BeginGroup();
        ImGui::Text("%s", tr("conn.host_label"));
        ImGui::SetNextItemWidth(310.0f);
        ImGui::InputText("##PhysHost", m_physConnHostBuf, IM_ARRAYSIZE(m_physConnHostBuf));
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::Text("%s", tr("conn.port_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("##PhysPort", &m_physConnPort, 0, 0);
        ImGui::EndGroup();

        ImGui::Spacing();

        ImGui::Text("%s", tr("conn.dbname_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PhysDbName", tr("conn.dbname_hint"), m_physConnDbNameBuf, IM_ARRAYSIZE(m_physConnDbNameBuf));

        ImGui::Spacing();

        ImGui::Text("%s", tr("conn.user_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##PhysUser", m_physConnUserBuf, IM_ARRAYSIZE(m_physConnUserBuf));

        ImGui::Spacing();

        // Sélecteur Mode SSL
        const char *sslModes[] = {"disable", "allow", "prefer", "require", "verify-ca", "verify-full"};
        ImGui::Text("%s", tr("conn.ssl_label"));
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

        ImGui::Text("%s", tr("conn.password_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##PhysPass", m_physConnPassBuf, IM_ARRAYSIZE(m_physConnPassBuf), ImGuiInputTextFlags_Password);

        ImGui::Spacing();

        // Bouton de test de connexion en direct
        if (ImGui::Button((std::string(ICON_LC_PLAY) + "  " + tr("conn.btn_test")).c_str()))
        {
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
                m_physConnTestStatus = tr("conn.test_success");
                testConn.close();
            }
            else
            {
                m_physConnTestOk = false;
                m_physConnTestStatus = std::string(tr("conn.test_failure")) + " : " + res.error();
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

        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showPhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool canSavePhys = (strlen(m_physConnNameBuf) > 0) && (strlen(m_physConnDbNameBuf) > 0);
        if (!canSavePhys)
            ImGui::BeginDisabled();

        if (ImGui::Button(tr("btn.save"), ImVec2(120.0f, 0.0f)))
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
    std::string renamePhysTitle = std::string(tr("conn.rename_phys_modal_title")) + "###RenamePhysConnModal";
    if (m_showRenamePhysConnModal)
        ImGui::OpenPopup(renamePhysTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(renamePhysTitle.c_str(), &m_showRenamePhysConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("conn.rename_phys_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenamePhysInput", m_renamePhysConnBuf, IM_ARRAYSIZE(m_renamePhysConnBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);

        std::string cancelPhysBtn = std::string(tr("btn.cancel")) + "##PhysRen";
        if (ImGui::Button(cancelPhysBtn.c_str(), ImVec2(95.0f, 0.0f)))
        {
            m_showRenamePhysConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool isValid = (strlen(m_renamePhysConnBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();

        if (ImGui::Button((std::string(tr("btn.confirm")) + "##PhysRenOk").c_str(), ImVec2(105.0f, 0.0f)) || (enterPressed && isValid))
        {
            std::string newName(m_renamePhysConnBuf);
            auto &physMap = m_activeProject->physicalConnections();
            if (!m_renamingPhysConnOldName.empty() && m_renamingPhysConnOldName != newName && physMap.contains(m_renamingPhysConnOldName))
            {
                cppsqlx::PhysicalConnection copy = physMap.at(m_renamingPhysConnOldName);
                copy.name_ = newName;
                physMap.erase(m_renamingPhysConnOldName);
                physMap[newName] = std::move(copy);
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
    // MODALE 3 : CRÉER / ÉDITER UNE PASSERELLE LOGIQUE (DEV / TEST / PROD)
    // =========================================================================
    std::string logModalTitle = std::string(tr("conn.log_modal_title")) + "###LogConnModal";
    if (m_showLogConnModal)
        ImGui::OpenPopup(logModalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(logModalTitle.c_str(), &m_showLogConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_GIT_BRANCH,
                           m_isEditingLogConn ? tr("conn.log_edit_subtitle") : tr("conn.log_new_subtitle"));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("%s", tr("conn.log_name_label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##LogName", tr("conn.log_name_hint"), m_logConnNameBuf, IM_ARRAYSIZE(m_logConnNameBuf));

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.75f, 0.55f, 1.0f, 1.0f), "%s", tr("conn.env_mapping_header"));
        ImGui::Separator();
        ImGui::Spacing();

        auto renderEnvSelector = [&](const char *envLabel, const char *comboId, std::string &selectedPhys)
        {
            ImGui::Text("%-6s :", envLabel);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const char *preview = selectedPhys.empty() ? tr("conn.unassigned") : selectedPhys.c_str();
            if (ImGui::BeginCombo(comboId, preview))
            {
                if (ImGui::Selectable(tr("conn.unassigned"), selectedPhys.empty()))
                {
                    selectedPhys.clear();
                }
                for (const auto &[physName, physObj] : m_activeProject->physicalConnections())
                {
                    if (ImGui::Selectable(physName.c_str(), selectedPhys == physName))
                    {
                        selectedPhys = physName;
                    }
                }
                ImGui::EndCombo();
            }
        };

        renderEnvSelector("DEV", "##LogDevCombo", m_logConnDevTarget);
        renderEnvSelector("TEST", "##LogTestCombo", m_logConnTestTarget);
        renderEnvSelector("PROD", "##LogProdCombo", m_logConnProdTarget);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);

        std::string cancelLogBtn = std::string(tr("btn.cancel")) + "##LogModalCancel";
        if (ImGui::Button(cancelLogBtn.c_str(), ImVec2(100.0f, 0.0f)))
        {
            m_showLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool canSaveLog = (strlen(m_logConnNameBuf) > 0);
        if (!canSaveLog)
            ImGui::BeginDisabled();

        std::string saveLogBtn = std::string(tr("btn.save")) + "##LogModalSave";
        if (ImGui::Button(saveLogBtn.c_str(), ImVec2(120.0f, 0.0f)))
        {
            std::string newName(m_logConnNameBuf);
            auto &gateways = m_activeProject->logicalConnections();
            const auto &physMap = m_activeProject->physicalConnections();

            if (m_isEditingLogConn && !m_originalLogConnName.empty() && m_originalLogConnName != newName)
            {
                gateways.erase(m_originalLogConnName);
                if (m_selectedGateway == m_originalLogConnName)
                    m_selectedGateway = newName;
            }

            cppsqlx::LogicalConnection gw(newName);
            if (!m_logConnDevTarget.empty() && physMap.contains(m_logConnDevTarget))
                gw.registerEnvironment("DEV", physMap.at(m_logConnDevTarget));
            if (!m_logConnTestTarget.empty() && physMap.contains(m_logConnTestTarget))
                gw.registerEnvironment("TEST", physMap.at(m_logConnTestTarget));
            if (!m_logConnProdTarget.empty() && physMap.contains(m_logConnProdTarget))
                gw.registerEnvironment("PROD", physMap.at(m_logConnProdTarget));

            gateways[newName] = std::move(gw);
            m_activeProject->resolveDependencies();

            m_showLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canSaveLog)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE 4 : RENOMMER UNE PASSERELLE LOGIQUE
    // =========================================================================
    std::string renameLogTitle = std::string(tr("conn.rename_log_modal_title")) + "###RenameLogConnModal";
    if (m_showRenameLogConnModal)
        ImGui::OpenPopup(renameLogTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(renameLogTitle.c_str(), &m_showRenameLogConnModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("conn.rename_log_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenameLogInput", m_renameLogConnBuf, IM_ARRAYSIZE(m_renameLogConnBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 210.0f);

        std::string cancelLogRenBtn = std::string(tr("btn.cancel")) + "##LogRenCancel";
        if (ImGui::Button(cancelLogRenBtn.c_str(), ImVec2(95.0f, 0.0f)))
        {
            m_showRenameLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool isValidLogRen = (strlen(m_renameLogConnBuf) > 0);
        if (!isValidLogRen)
            ImGui::BeginDisabled();

        if (ImGui::Button((std::string(tr("btn.confirm")) + "##LogRenOk").c_str(), ImVec2(105.0f, 0.0f)) || (enterPressed && isValidLogRen))
        {
            std::string newName(m_renameLogConnBuf);
            auto &gateways = m_activeProject->logicalConnections();
            if (!m_renamingLogConnOldName.empty() && m_renamingLogConnOldName != newName && gateways.contains(m_renamingLogConnOldName))
            {
                cppsqlx::LogicalConnection copy = gateways.at(m_renamingLogConnOldName);
                copy.setName(newName);
                gateways.erase(m_renamingLogConnOldName);
                gateways[newName] = std::move(copy);
                m_activeProject->resolveDependencies();

                if (m_selectedGateway == m_renamingLogConnOldName)
                    m_selectedGateway = newName;
            }
            m_showRenameLogConnModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!isValidLogRen)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}
