#include "Application.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include "IconsLucide.hpp"

Application::Application() {
    m_windowManager = std::make_unique<WindowManager>(1600, 900, "dizui - Linux C++20 Workspace");
    m_renderEngine = std::make_unique<RenderEngine>(m_windowManager->getNativeWindow());
    
    // Initialisation des deux éditeurs graphiques autonomes
    m_workflowContext = ed::CreateEditor();
    m_queryContext = ed::CreateEditor();
}

Application::~Application() {
    // Libération propre des ressources mémoire
    if (m_workflowContext) ed::DestroyEditor(m_workflowContext);
    if (m_queryContext) ed::DestroyEditor(m_queryContext);
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
    // Configuration de l'environnement hôte global Dockspace
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

    ImGuiID dockspace_id = ImGui::GetID("DizuiMainDockSpace");
    
    // Initialisation automatique du Layout (Inversion Gauche / Centre / Droite)
    static bool init_layout = true;
    if (init_layout && !ImGui::DockBuilderGetNode(dockspace_id)) {
        init_layout = false;
        
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_left_id, dock_center_id;

        // 1. Découper l'extrême gauche pour la Toolbar (0.05f)
        dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.05f, nullptr, &dock_main_id);
        ImGuiDockNode* left_node = ImGui::DockBuilderGetNode(dock_left_id);
        if (left_node) {
            left_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | static_cast<int>(ImGuiDockNodeFlags_NoResize);
        }

        // 2. Découper le reste depuis la gauche pour le Panneau de Contrôle (0.25f)
        dock_center_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.25f, nullptr, &dock_main_id);
        ImGuiDockNode* center_node = ImGui::DockBuilderGetNode(dock_center_id);
        if (center_node) {
            center_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }

        // --- NOUVEAU : Masquer la TabBar également sur la zone Workspace restante à droite ---
        ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_main_id);
        if (main_node) {
            main_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }

        // Assigner les fenêtres aux emplacements fixes
        ImGui::DockBuilderDockWindow("Outils", dock_left_id);
        ImGui::DockBuilderDockWindow("Panneau de Contrôle", dock_center_id);
        ImGui::DockBuilderDockWindow("Espace de Travail (Canevas)", dock_main_id); // Toujours le même nom de fenêtre
        
        ImGui::DockBuilderFinish(dockspace_id);
    }


    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    // =========================================================================
    // 1. Barre de Titre / Menu Principal
    // =========================================================================
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));

    if (ImGui::BeginMainMenuBar()) {
        
        ImGui::Spacing();
        // Icône Logo de l'application
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s", ICON_LC_CPU);
        ImGui::Spacing();

        // ---------------------------------------------------------------------
        // MENU : File
        // ---------------------------------------------------------------------
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) { m_currentTab = ActiveTab::Project; }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) { m_currentTab = ActiveTab::Project; }
            if (ImGui::MenuItem("Close Project", "Ctrl+W")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {}
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+P")) { m_currentTab = ActiveTab::Settings; }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::MenuItem("Delete Project", "")) {}
            ImGui::PopStyleColor();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(m_windowManager->getNativeWindow(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        // ---------------------------------------------------------------------
        // MENU : Edit (Nouveau)
        // ---------------------------------------------------------------------
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                // TODO: Logique d'annulation pour le graphe
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                // TODO: Logique de rétablissement
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {}
            if (ImGui::MenuItem("Clear Canvas", "Delete")) {
                // TODO: Logique pour vider tous les nœuds
            }
            ImGui::EndMenu();
        }

        // ---------------------------------------------------------------------
        // MENU : View (Nouveau)
        // ---------------------------------------------------------------------
        if (ImGui::BeginMenu("View")) {
            // Options pour basculer rapidement d'onglet depuis le menu
            if (ImGui::MenuItem("Show Project Workspace", "Alt+1"))   { m_currentTab = ActiveTab::Project; }
            if (ImGui::MenuItem("Show Search Panel", "Alt+2"))        { m_currentTab = ActiveTab::Search; }
            if (ImGui::MenuItem("Show Source Control", "Alt+3"))      { m_currentTab = ActiveTab::Git; }
            if (ImGui::MenuItem("Show Package Manager", "Alt+4"))     { m_currentTab = ActiveTab::Package; }
            if (ImGui::MenuItem("Show Settings", "Alt+5"))            { m_currentTab = ActiveTab::Settings; }
            
            ImGui::Separator();
            
            // Flags d'affichage pour le canevas de nœuds
            static bool show_grid = true;
            if (ImGui::MenuItem("Grid background", nullptr, &show_grid)) {
                // TODO: Transmettre l'état au imgui-node-editor
            }
            
            static bool show_fps = false;
            ImGui::MenuItem("Show Performance Overlay", nullptr, &show_fps);
            ImGui::EndMenu();
        }

        // ---------------------------------------------------------------------
        // MENU : Help (Nouveau)
        // ---------------------------------------------------------------------
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation", "F1")) {
                // Logique pour ouvrir une URL ou un panneau d'aide
            }
            if (ImGui::MenuItem("Keyboard Shortcuts", "Ctrl+?")) {}
            ImGui::Separator();
            
            static bool show_about_popup = false;
            if (ImGui::MenuItem("About dizui...", nullptr)) {
                show_about_popup = true;
            }
            
            // Fenêtre modale "À propos" déclenchée par le menu
            if (show_about_popup) {
                ImGui::OpenPopup("About Dizui");
            }
            
            ImGui::EndMenu();
        }
        
        // Alignement des informations à droite
        float text_width = ImGui::CalcTextSize("dizui v1.0").x;
        ImGui::SameLine(ImGui::GetWindowWidth() - text_width - 20.0f);
        ImGui::TextDisabled("dizui v1.0");

        ImGui::EndMainMenuBar();
    }

    // Gestion de la modale "À propos" en dehors du bloc de la barre de menus
    if (ImGui::BeginPopupModal("About Dizui", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("dizui — Workspace de Graphes C++20");
        ImGui::Separator();
        ImGui::Text("Version : 1.0.0");
        ImGui::Text("Environnement : Linux (Wayland Native)");
        ImGui::Text("Moteurs : ImGui Docking & Lucide Vector Icons");
        ImGui::Spacing();
        if (ImGui::Button("Fermer", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();



    // =========================================================================
    // 2. Barre d'outils verticale (Gauche)
    // =========================================================================
    ImGuiWindowFlags toolbar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    ImGui::Begin("Outils", nullptr, toolbar_flags);
    
    // Style de base des boutons (Transparent au repos, gris clair au survol)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 0.60f));

    // --- Fonction utilitaire locale pour dessiner un bouton avec état Actif ---
    auto drawToolbarButton = [&](const char* icon, const char* tooltip, ActiveTab tabTarget) {
        bool is_active = (m_currentTab == tabTarget);
        if (is_active) {
            // Si le bouton correspond à l'onglet actif, on lui applique un fond gris fixe
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.20f, 0.80f));
        } else {
            // Sinon fond transparent
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        }

        if (ImGui::Button(icon, ImVec2(48, 48))) {
            m_currentTab = tabTarget; // Change l'état de l'application
        }
        ImGui::SetItemTooltip("%s", tooltip);

        ImGui::PopStyleColor(); // Restaure la couleur de fond du bouton
    };

    // Bouton 1: Project
    drawToolbarButton(ICON_LC_BLOCKS, "Project workspace", ActiveTab::Project);
    ImGui::Spacing();

    // Bouton 2: Recherche
    drawToolbarButton(ICON_LC_SEARCH, "Rechercher dans le Graphe", ActiveTab::Search);
    ImGui::Spacing();

    // Bouton 3: Git
    drawToolbarButton(ICON_LC_GIT_BRANCH, "Gérer les sources", ActiveTab::Git);
    ImGui::Spacing();

    // Bouton 4: Package
    drawToolbarButton(ICON_LC_BOX, "Gestion package", ActiveTab::Package);
    ImGui::Spacing();

    // Bouton 5: Paramètres
    drawToolbarButton(ICON_LC_SETTINGS, "Paramètres de l'application", ActiveTab::Settings);

    ImGui::PopStyleColor(2); // Restaure Hovered et Active généraux
    ImGui::End();

    // =========================================================================
    // 3. Panneau de Contrôle Vertical (Milieu - Dynamique)
    // =========================================================================
    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Panneau de Contrôle", nullptr, panel_flags);

    // --- FONCTION UTILITAIRE : Dessiner une icône à taille personnalisée ---
    // Cette fonction réserve l'espace, calcule la position et dessine l'icône à la taille demandée
    // --- FONCTION UTILITAIRE CORRIGÉE : Dessiner une icône à taille personnalisée ---
    auto drawLargeIconText = [](const char* icon, const char* label, float icon_size) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return;

        ImGuiContext& g = *GImGui;
        
        // 1. Déterminer la police courante
        ImFont* font = g.Font; 

        // 2. Calculer l'espace nécessaire pour l'icône agrandie
        ImVec2 padding(8.0f, 4.0f);
        float line_height = ImMax(icon_size, ImGui::GetTextLineHeight());
        ImVec2 size(icon_size + padding.x * 2.0f, line_height);
        
        // CORRECTION : Calcul manuel des coordonnées vectorielles sans surcharger l'opérateur +
        ImVec2 pos_max(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y);
        ImRect bb(window->DC.CursorPos, pos_max);
        
        ImGui::ItemSize(size, g.Style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, 0)) return;

        // 3. Dessiner l'icône directement dans la liste de rendu à la taille 'icon_size'
        // CORRECTION : Addition manuelle composante par composante
        ImVec2 icon_pos(bb.Min.x + padding.x, bb.Min.y + padding.y);
        icon_pos.y += (line_height - icon_size) * 0.5f;

        window->DrawList->AddText(font, icon_size, icon_pos, ImGui::GetColorU32(ImGuiCol_Text), icon);

        // 4. Placer le texte normal juste à côté sur la même ligne
        ImGui::SameLine(0.0f, g.Style.ItemSpacing.x);
        
        // CORRECTION : Remplacement par les vraies fonctions Y d'ImGui
        float current_y = ImGui::GetCursorPosY();
        float offset_y = (line_height - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::SetCursorPosY(current_y + offset_y);
        
        ImGui::Text("%s", label);
        
        // Réaligner le curseur pour la ligne suivante
        ImGui::SetCursorPosY(current_y + line_height + g.Style.ItemSpacing.y);
    };


    // --- APPLICATION DANS LE SWITCH CASE ---
    switch (m_currentTab) 
    {
        case ActiveTab::Project:
            ImGui::TextDisabled("ONGLET");
            
            // Icône dessinée à 32 pixels de hauteur, le texte reste à la taille par défaut (18px)
            drawLargeIconText(ICON_LC_BLOCKS, "Espace Projet", 14.0f);
            
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::TreeNode("dizui_project")) {
                ImGui::Text("📄 main.cpp");
                ImGui::Text("📄 Application.cpp");
                ImGui::TreePop();
            }
            break;

        case ActiveTab::Search:
            ImGui::TextDisabled("ONGLET");
            
            // Icône dessinée encore plus grande (40 pixels de hauteur)
            drawLargeIconText(ICON_LC_SEARCH, "Recherche Avancée", 14.0f);
            
            ImGui::Separator();
            ImGui::Spacing();
            static char search_buffer[128] = "";
            ImGui::InputText("Filtrer les nœuds", search_buffer, IM_ARRAYSIZE(search_buffer));
            break;

        case ActiveTab::Git:
            ImGui::TextDisabled("ONGLET");
            drawLargeIconText(ICON_LC_GIT_BRANCH, "Gestion des Sources", 14.0f);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Branche active : main");
            break;

        case ActiveTab::Package:
            ImGui::TextDisabled("ONGLET");
            drawLargeIconText(ICON_LC_BOX, "Gestionnaire de Packages", 14.0f);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Bibliothèques liées :");
            break;

        case ActiveTab::Settings:
            ImGui::TextDisabled("ONGLET");
            drawLargeIconText(ICON_LC_SETTINGS, "Paramètres de l'Application", 14.0f);
            ImGui::Separator();
            ImGui::Spacing();
            static float scale = 1.0f;
            ImGui::SliderFloat("Échelle Canvas", &scale, 0.5f, 2.0f);
            break;
    }

    ImGui::End();




    // 4. Barre de Statut (Bas de page fixe)
    ImGuiViewport *main_vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(main_vp->Pos.x, main_vp->Pos.y + main_vp->Size.y - 25.0f));
    ImGui::SetNextWindowSize(ImVec2(main_vp->Size.x, 25.0f));
    ImGuiWindowFlags status_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));
    ImGui::Begin("BarreDeStatut", nullptr, status_flags);
    ImGui::Text("Statut: Prêt | Moteur: OpenGL Core | ImGui Docking OK | FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
    ImGui::PopStyleVar();

     // =========================================================================
    // 5. Zone Workspace (Droite - Alignement Plat des 3 Designers Majeurs)
    // =========================================================================
    ImGuiWindowFlags workspace_flags = ImGuiWindowFlags_NoTitleBar | 
                                       ImGuiWindowFlags_NoCollapse | 
                                       ImGuiWindowFlags_NoScrollbar;
                                         
    ImGui::Begin("Espace de Travail (Canevas)", nullptr, workspace_flags);

    // Forcer le masquage de l'onglet bleu parent du Dockspace global
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window && window->DockNode) {
        window->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    }

    // Une seule barre d'onglets principale contenant nos 3 outils alignés
    if (ImGui::BeginTabBar("DesignerTabBar", ImGuiTabBarFlags_None))
    {
        // ---------------------------------------------------------------------
        // DESIGNER 1 : WORKFLOW DESIGNER (Graphe de flux)
        // ---------------------------------------------------------------------
        std::string workflow_title = std::string(ICON_LC_ACTIVITY) + "  Workflow Designer";
        if (ImGui::BeginTabItem(workflow_title.c_str()))
        {
            renderWorkflowEditor(); 
            ImGui::EndTabItem();
        }

        // ---------------------------------------------------------------------
        // DESIGNER 2 : QUERY VISUAL DESIGNER (Graphe de requêtage)
        // ---------------------------------------------------------------------
        std::string query_vis_title = std::string(ICON_LC_SEARCH) + "  Query Visual Designer";
        if (ImGui::BeginTabItem(query_vis_title.c_str()))
        {
            renderQueryEditor(); 
            ImGui::EndTabItem();
        }

        // ---------------------------------------------------------------------
        // DESIGNER 3 : QUERY TEXT EDITOR (Avec Splitter Interactif)
        // ---------------------------------------------------------------------
        std::string query_text_title = std::string(ICON_LC_BOX) + "  Query Text Editor";
        if (ImGui::BeginTabItem(query_text_title.c_str()))
        {
            ImGui::Spacing();
            
            // 1. Enregistrer la position de départ de la ligne
            float initial_y = ImGui::GetCursorPosY();
            ImGui::TextDisabled("ÉDITEUR TEXTUEL DE CODE");
            
            // 2. Alignement des boutons miniatures à l'extrême droite
            float button_size = 24.0f;
            float target_icon_size = 16.0f;
            float total_buttons_width = (button_size * 2.0f) + ImGui::GetStyle().ItemSpacing.x;
            
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - total_buttons_width - 15.0f);
            ImGui::SetCursorPosY(initial_y - 2.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 0.55f));

            if (ImGui::Button("##exec_btn", ImVec2(button_size, button_size))) {}
            ImGui::SetItemTooltip("Exécuter le script (Ctrl+Entrée)");
            
            float offset_xy = (button_size - target_icon_size) * 0.5f;
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), target_icon_size, 
                ImVec2(ImGui::GetCursorScreenPos().x - button_size + offset_xy - ImGui::GetStyle().ItemSpacing.x, ImGui::GetCursorScreenPos().y + offset_xy), 
                ImGui::GetColorU32(ImGuiCol_Text), ICON_LC_PLAY);
            
            ImGui::SameLine();
            
            if (ImGui::Button("##format_btn", ImVec2(button_size, button_size))) {}
            ImGui::SetItemTooltip("Formater le code SQL");
            
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), target_icon_size, 
                ImVec2(ImGui::GetCursorScreenPos().x - button_size + offset_xy, ImGui::GetCursorScreenPos().y + offset_xy), 
                ImGui::GetColorU32(ImGuiCol_Text), ICON_LC_BOX);
            
            ImGui::PopStyleColor(3);
            ImGui::SetCursorPosY(initial_y + button_size + 2.0f);
            ImGui::Separator();

            // -----------------------------------------------------------------
            // CALCULS ET GESTION DU SPLITTER DYNAMIQUE
            // -----------------------------------------------------------------
            float total_available_height = ImGui::GetContentRegionAvail().y - 25.0f;
            float minimum_zone_height = 80.0f; // Empêche d'écraser complètement une zone

            // --- ZONE SUPÉRIEURE : ÉDITEUR TEXTUEL ---
            ImGui::BeginChild("CodeEditorZone", ImVec2(-1.0f, m_queryEditorHeight), false, ImGuiWindowFlags_NoScrollbar);
            static char query_text_buffer[] = 
                "-- Requête dizui générée automatiquement\n"
                "SELECT users.id, users.name, orders.total\n"
                "FROM users\n"
                "LEFT JOIN orders ON users.id = orders.user_id\n"
                "WHERE users.age > 18\n"
                "ORDER BY orders.total DESC;";

            ImGuiInputTextFlags editor_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CtrlEnterForNewLine;
            ImGui::InputTextMultiline("##query_code_editor", query_text_buffer, IM_ARRAYSIZE(query_text_buffer), 
                                      ImVec2(-1.0f, -1.0f), editor_flags);
            ImGui::EndChild();

            // --- LE SPLITTER INTERACTIF (Barre de préhension) ---
            // On modifie l'apparence pour en faire une ligne fine et discrète
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));         // Couleur au repos
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.75f, 1.0f, 0.8f));  // Bleu cyan au survol
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.75f, 1.0f, 1.0f));   // Bleu vif quand cliqué

            // Un bouton horizontal qui prend toute la largeur
            ImGui::Button("##VerticalSplitterBar", ImVec2(-1.0f, 4.0f));
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); // Change le curseur en flèche Haut/Bas

            // Logique de Drag (Glisser) de la souris pour recalculer la hauteur en temps réel
            if (ImGui::IsItemActive()) {
                m_queryEditorHeight += ImGui::GetIO().MouseDelta.y;
                
                // Contraintes de sécurité pour éviter que les fenêtres disparaissent
                if (m_queryEditorHeight < minimum_zone_height) 
                    m_queryEditorHeight = minimum_zone_height;
                if (m_queryEditorHeight > total_available_height - minimum_zone_height) 
                    m_queryEditorHeight = total_available_height - minimum_zone_height;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            // Titre de la section basse
            ImGui::TextDisabled("%s  RÉSULTAT DE LA REQUÊTE", ICON_LC_ACTIVITY);
            ImGui::Separator();

            // --- ZONE INFÉRIEURE : TABLEAU DE DONNÉES (Prend tout le reste) ---
            float bottom_zone_height = ImGui::GetContentRegionAvail().y - 10.0f;
            ImGui::BeginChild("ResultTableZone", ImVec2(-1.0f, bottom_zone_height), false);

            ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | 
                                          ImGuiTableFlags_RowBg | 
                                          ImGuiTableFlags_ScrollY | 
                                          ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("SQLResultTable", 3, table_flags))
            {
                ImGui::TableSetupColumn("users.id");
                ImGui::TableSetupColumn("users.name");
                ImGui::TableSetupColumn("orders.total");
                ImGui::TableHeadersRow();

                struct FakeRow { int id; const char* name; float total; };
                static const FakeRow data_rows[] = {
                    { 102, "Alice Martin", 1450.50f },
                    { 105, "Bob Bernard", 890.00f },
                    { 112, "Charlie Dubois", 2300.15f },
                    { 115, "David Durand", 120.40f },
                    { 121, "Emma Robert", 3410.00f },
                    { 124, "Franck Moreau", 75.00f },
                    { 130, "Grace Petit", 199.99f },
                    { 133, "Henri Thomas", 1150.00f },
                    { 140, "Iris Michel", 450.20f },
                    { 145, "Julien Garcia", 620.00f }
                };

                for (int row = 0; row < 10; row++)
                {
                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", data_rows[row].id);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", data_rows[row].name);

                    ImGui::TableSetColumnIndex(2);
                    float avail_x = ImGui::GetContentRegionAvail().x;
                    float text_x = ImGui::CalcTextSize("0000.00 EUR").x;
                    if (avail_x > text_x) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - text_x);
                    }
                    ImGui::Text("%.2f €", data_rows[row].total);
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }





        ImGui::EndTabBar();
    }

    ImGui::End();


}


void Application::renderWorkflowEditor() {
    // 1. Activer le contexte du Workflow
    ed::SetCurrentEditor(m_workflowContext);
    ed::Begin("WorkflowCanvas");

    // Exemple de structure orientée "Tâches / Flux" (ID 1 à 9)
    ed::BeginNode(1);
        ImGui::Text("Tâche Initiale");
        ed::BeginPin(1, ed::PinKind::Output);
            ImGui::Text("Suivant ->");
        ed::EndPin();
    ed::EndNode();

    ed::BeginNode(2);
        ImGui::Text("Condition Exécution");
        ed::BeginPin(2, ed::PinKind::Input);
            ImGui::Text("-> Entrée");
        ed::EndPin();
    ed::EndNode();

    ed::Link(1, 1, 2);

    ed::End();
    ed::SetCurrentEditor(nullptr); // Décharger
}

void Application::renderQueryEditor() {
    // ---------------------------------------------------------------------
    // EN-TÊTE : TITRE ET BOUTON D'EXÉCUTION MINIATURE
    // ---------------------------------------------------------------------
    float initial_y = ImGui::GetCursorPosY();
    ImGui::TextDisabled("CONCEPTEUR GRAPHIQUE DE REQUÊTE");
    
    // Aligner le bouton d'exécution miniature à droite (24x24 pixels)
    float button_size = 24.0f;
    float target_icon_size = 16.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - button_size - 15.0f);
    ImGui::SetCursorPosY(initial_y - 2.0f);

    // Style de bouton transparent / survol gris
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.35f, 0.35f, 0.55f));

    ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##exec_visual_btn", ImVec2(button_size, button_size))) {
        // TODO: Déclencher l'analyse du graphe et l'exécution de la requête
    }
    ImGui::SetItemTooltip("Exécuter le graphe de requête");
    
    // Dessiner l'icône réduite au centre du bouton
    float offset_xy = (button_size - target_icon_size) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), target_icon_size, 
        ImVec2(screen_pos.x + offset_xy, screen_pos.y + offset_xy), 
        ImGui::GetColorU32(ImGuiCol_Text), ICON_LC_PLAY);
    
    ImGui::PopStyleColor(3);
    
    // Caler le curseur sous l'en-tête
    ImGui::SetCursorPosY(initial_y + button_size + 2.0f);
    ImGui::Separator();

    // ---------------------------------------------------------------------
    // LOGIQUE DU SPLITTER DYNAMIQUE
    // ---------------------------------------------------------------------
    float total_available_height = ImGui::GetContentRegionAvail().y - 25.0f;
    float minimum_zone_height = 100.0f; // Garder assez d'espace pour manipuler les nœuds

    // --- ZONE SUPÉRIEURE : CANEVAS GRAPHIQUE DE NŒUDS ---
    ImGui::BeginChild("QueryNodeCanvasZone", ImVec2(-1.0f, m_queryVisualHeight), false, ImGuiWindowFlags_NoScrollbar);
    
    // Moteur graphique imgui-node-editor
    ed::SetCurrentEditor(m_queryContext);
    ed::Begin("QueryCanvas");

    // Exemple de structure orientée traitement de données
    ed::BeginNode(10);
        ImGui::Text("Source: Table 'Users'");
        ed::BeginPin(10, ed::PinKind::Output);
            ImGui::Text("Data Row ->");
        ed::EndPin();
    ed::EndNode();

    ed::BeginNode(11);
        ImGui::Text("Filtre: Age > 18");
        ed::BeginPin(11, ed::PinKind::Input);
            ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(12, ed::PinKind::Output);
            ImGui::Text("Out ->");
        ed::EndPin();
    ed::EndNode();

    ed::Link(10, 10, 11);

    ed::End();
    ed::SetCurrentEditor(nullptr);
    
    ImGui::EndChild();

    // --- LE SPLITTER INTERACTIF ---
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.75f, 1.0f, 0.8f)); // S'allume en bleu
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.75f, 1.0f, 1.0f));

    ImGui::Button("##VisualSplitterBar", ImVec2(-1.0f, 4.0f));
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    if (ImGui::IsItemActive()) {
        m_queryVisualHeight += ImGui::GetIO().MouseDelta.y;
        
        // Contraintes anti-débordement
        if (m_queryVisualHeight < minimum_zone_height) 
            m_queryVisualHeight = minimum_zone_height;
        if (m_queryVisualHeight > total_available_height - minimum_zone_height) 
            m_queryVisualHeight = total_available_height - minimum_zone_height;
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    // Titre section basse
    ImGui::TextDisabled("%s  RÉSULTAT DU GRAPHE VISUEL", ICON_LC_ACTIVITY);
    ImGui::Separator();

    // --- ZONE INFÉRIEURE : TABLEAU DE DONNÉES EN MODE ZÈBRE ---
    float bottom_zone_height = ImGui::GetContentRegionAvail().y - 10.0f;
    ImGui::BeginChild("VisualResultTableZone", ImVec2(-1.0f, bottom_zone_height), false);

    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | 
                                  ImGuiTableFlags_RowBg | 
                                  ImGuiTableFlags_ScrollY | 
                                  ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("VisualSQLResultTable", 3, table_flags))
    {
        ImGui::TableSetupColumn("users.id");
        ImGui::TableSetupColumn("users.name");
        ImGui::TableSetupColumn("orders.total");
        ImGui::TableHeadersRow();

        struct FakeRow { int id; const char* name; float total; };
        static const FakeRow data_rows[] = {
            { 102, "Alice Martin", 1450.50f },
            { 105, "Bob Bernard", 890.00f },
            { 112, "Charlie Dubois", 2300.15f },
            { 115, "David Durand", 120.40f },
            { 121, "Emma Robert", 3410.00f },
            { 124, "Franck Moreau", 75.00f },
            { 130, "Grace Petit", 199.99f },
            { 133, "Henri Thomas", 1150.00f },
            { 140, "Iris Michel", 450.20f },
            { 145, "Julien Garcia", 620.00f }
        };

        for (int row = 0; row < 10; row++)
        {
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", data_rows[row].id);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", data_rows[row].name);

            ImGui::TableSetColumnIndex(2);
            float avail_x = ImGui::GetContentRegionAvail().x;
            float text_x = ImGui::CalcTextSize("0000.00 EUR").x;
            if (avail_x > text_x) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - text_x);
            }
            ImGui::Text("%.2f €", data_rows[row].total);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
