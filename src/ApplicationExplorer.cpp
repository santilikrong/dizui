#include "Application.hpp"
#include <Localization.hpp>
#include <SqlEditorHelpers.hpp>
#include "IconsLucide.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

void Application::renderProjectExplorerTree()
{
    ImGuiWindowFlags panel_flags_local = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Panneau de Contrôle", nullptr, panel_flags_local);

    auto drawLargeIconText = [](const char *icon, const char *label, float icon_size)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;
        ImGuiContext &g = *GImGui;
        ImFont *font = g.Font;
        ImVec2 padding(8.0f, 4.0f);
        float line_height = ImMax(icon_size, ImGui::GetTextLineHeight());
        ImVec2 size(icon_size + padding.x * 2.0f, line_height);
        ImVec2 pos_max(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y);
        ImRect bb(window->DC.CursorPos, pos_max);
        ImGui::ItemSize(size, g.Style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, 0))
            return;
        ImVec2 icon_pos(bb.Min.x + padding.x, bb.Min.y + padding.y);
        icon_pos.y += (line_height - icon_size) * 0.5f;
        window->DrawList->AddText(font, icon_size, icon_pos, ImGui::GetColorU32(ImGuiCol_Text), icon);
        ImGui::SameLine(0.0f, g.Style.ItemSpacing.x);
        float current_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(current_y + (line_height - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::Text("%s", label);
        ImGui::SetCursorPosY(current_y + line_height + g.Style.ItemSpacing.y);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    };

    switch (m_currentTab)
    {
    case ActiveTab::Project:
        drawLargeIconText(ICON_LC_BLOCKS, tr("explorer.project_space"), 14.0f);
        ImGui::Separator();
        ImGui::Spacing();
        if (m_activeProject)
        {
            std::string project_root_label = std::string(ICON_LC_LAYOUT_GRID) + "  " + m_activeProject->name() + "###ProjectRootNode";
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode(project_root_label.c_str()))
            {
                drawLargeIconText(ICON_LC_FOLDER_OPEN, (std::string(tr("explorer.path_label")) + " : " + m_activeProject->path()).c_str(), 14.0f);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 1. Connexions Physiques
                ImGui::PushID("PhysConnsRoot");
                std::string physRootLabel = std::string(tr("explorer.phys_conns_root")) + "###PhysConnsTree";
                bool phys_open = ImGui::TreeNode(physRootLabel.c_str());
                if (ImGui::BeginPopupContextItem("PhysConnsRootContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_PLUS_CIRCLE) + "  " + tr("explorer.ctx_create_phys")).c_str()))
                    {
                        m_isEditingPhysConn = false;
                        m_originalPhysConnName = "";
                        memset(m_physConnNameBuf, 0, sizeof(m_physConnNameBuf));
                        strncpy(m_physConnHostBuf, "localhost", sizeof(m_physConnHostBuf) - 1);
                        m_physConnPort = 5432;
                        memset(m_physConnDbNameBuf, 0, sizeof(m_physConnDbNameBuf));
                        memset(m_physConnUserBuf, 0, sizeof(m_physConnUserBuf));
                        memset(m_physConnPassBuf, 0, sizeof(m_physConnPassBuf));
                        m_physConnTestStatus = "";
                        m_showPhysConnModal = true;
                    }
                    ImGui::EndPopup();
                }

                if (phys_open)
                {
                    std::string physToDelete = "";
                    for (const auto &[name, physConn] : m_activeProject->physicalConnections())
                    {
                        ImGui::PushID(name.c_str());
                        std::string rowLabel = std::string(ICON_LC_SERVER) + "  " + name + " (" + physConn.dbname_ + ")";
                        ImGui::Selectable(rowLabel.c_str(), false);

                        if (ImGui::BeginPopupContextItem("PhysConnItemContext", ImGuiPopupFlags_MouseButtonRight))
                        {
                            if (ImGui::MenuItem(tr("explorer.ctx_edit_conn")))
                            {
                                m_isEditingPhysConn = true;
                                m_originalPhysConnName = name;
                                strncpy(m_physConnNameBuf, name.c_str(), sizeof(m_physConnNameBuf) - 1);
                                strncpy(m_physConnHostBuf, physConn.dbhost_.c_str(), sizeof(m_physConnHostBuf) - 1);
                                m_physConnPort = physConn.dbport_;
                                strncpy(m_physConnDbNameBuf, physConn.dbname_.c_str(), sizeof(m_physConnDbNameBuf) - 1);
                                strncpy(m_physConnUserBuf, physConn.dbuser_.c_str(), sizeof(m_physConnUserBuf) - 1);
                                strncpy(m_physConnPassBuf, physConn.dbpassword_.c_str(), sizeof(m_physConnPassBuf) - 1);
                                m_physConnTestStatus = "";
                                m_showPhysConnModal = true;

                                const char *sslModes[] = {"disable", "allow", "prefer", "require", "verify-ca", "verify-full"};
                                m_physConnSslModeIndex = 2; // "prefer" par défaut
                                for (int i = 0; i < 6; ++i)
                                {
                                    if (physConn.sslmode_ == sslModes[i])
                                    {
                                        m_physConnSslModeIndex = i;
                                        break;
                                    }
                                }
                            }

                            if (ImGui::MenuItem(tr("explorer.ctx_rename_conn")))
                            {
                                m_renamingPhysConnOldName = name;
                                strncpy(m_renamePhysConnBuf, name.c_str(), sizeof(m_renamePhysConnBuf) - 1);
                                m_showRenamePhysConnModal = true;
                            }

                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                            if (ImGui::MenuItem(tr("explorer.ctx_delete_conn")))
                            {
                                physToDelete = name;
                            }
                            ImGui::PopStyleColor();
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }

                    if (!physToDelete.empty())
                    {
                        m_activeProject->physicalConnections().erase(physToDelete);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();

                // 2. Passerelles Logiques
                ImGui::PushID("LogConnsRoot");
                std::string logRootLabel = std::string(tr("explorer.log_conns_root")) + "###LogConnsTree";
                bool log_open = ImGui::TreeNode(logRootLabel.c_str());
                if (ImGui::BeginPopupContextItem("LogConnsRootContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    std::string createLogLabel = std::string(ICON_LC_PLUS_CIRCLE) + "  " + tr("explorer.ctx_create_log") + "###CreateLogConnItem";
                    if (ImGui::MenuItem(createLogLabel.c_str()))
                    {
                        m_isEditingLogConn = false;
                        m_originalLogConnName.clear();
                        memset(m_logConnNameBuf, 0, sizeof(m_logConnNameBuf));
                        m_logConnDevTarget.clear();
                        m_logConnTestTarget.clear();
                        m_logConnProdTarget.clear();
                        m_showLogConnModal = true;
                    }
                    ImGui::EndPopup();
                }

                if (log_open)
                {
                    std::string logToDelete = "";
                    for (const auto &[name, logConn] : m_activeProject->logicalConnections())
                    {
                        ImGui::PushID(name.c_str());
                        bool gwNodeOpen = ImGui::TreeNode(name.c_str());

                        if (ImGui::BeginPopupContextItem("LogConnItemContext", ImGuiPopupFlags_MouseButtonRight))
                        {
                            if (ImGui::MenuItem(tr("explorer.ctx_edit_conn")))
                            {
                                m_isEditingLogConn = true;
                                m_originalLogConnName = name;
                                strncpy(m_logConnNameBuf, name.c_str(), sizeof(m_logConnNameBuf) - 1);
                                const auto &envs = logConn.environments();
                                m_logConnDevTarget = envs.contains("DEV") ? envs.at("DEV").name_ : "";
                                m_logConnTestTarget = envs.contains("TEST") ? envs.at("TEST").name_ : "";
                                m_logConnProdTarget = envs.contains("PROD") ? envs.at("PROD").name_ : "";
                                m_showLogConnModal = true;
                            }

                            if (ImGui::MenuItem(tr("explorer.ctx_rename_conn")))
                            {
                                m_renamingLogConnOldName = name;
                                strncpy(m_renameLogConnBuf, name.c_str(), sizeof(m_renameLogConnBuf) - 1);
                                m_showRenameLogConnModal = true;
                            }

                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                            if (ImGui::MenuItem(tr("explorer.ctx_delete_conn")))
                            {
                                logToDelete = name;
                            }
                            ImGui::PopStyleColor();
                            ImGui::EndPopup();
                        }

                        if (gwNodeOpen)
                        {
                            for (const auto &[envName, physConn] : logConn.environments())
                            {
                                if (ImGui::TreeNode(envName.c_str()))
                                {
                                    drawLargeIconText(ICON_LC_SERVER, (std::string(tr("explorer.cluster_label")) + " : " + physConn.name_).c_str(), 12.0f);
                                    drawLargeIconText(ICON_LC_DATABASE, (std::string(tr("explorer.db_label")) + " : " + physConn.dbname_).c_str(), 12.0f);
                                    ImGui::TreePop();
                                }
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    if (!logToDelete.empty())
                    {
                        m_activeProject->logicalConnections().erase(logToDelete);
                        if (m_selectedGateway == logToDelete)
                        {
                            m_selectedGateway = "";
                            m_selectedContext = "";
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();

                // 3. Datasets (Tables)
                ImGui::PushID("DatasetsHeaderNode");
                std::string datasetsRootLabel = std::string(tr("explorer.datasets_root")) + "###DatasetsTree";
                bool datasets_open = ImGui::TreeNode(datasetsRootLabel.c_str());
                if (ImGui::BeginPopupContextItem("DatasetsContextMenu", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_REFRESH_CW) + "  " + tr("explorer.ctx_reverse_datasets")).c_str()))
                    {
                        m_showReverseModal = true;
                        m_reverseSelectedObject = "";
                        m_cachedSchemas.clear();
                    }
                    ImGui::EndPopup();
                }
                if (datasets_open)
                {
                    for (const auto &[name, dataset] : m_activeProject->datasets())
                    {
                        ImGui::PushID(name.c_str());
                        if (ImGui::Selectable(("##select_" + name).c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, ImGui::GetTextLineHeightWithSpacing())))
                        {
                            m_reverseSelectedObject = name;
                        }
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("DND_DATASET_NODE", name.c_str(), name.size() + 1);
                            drawLargeIconText(ICON_LC_TABLE, name.c_str(), 12.0f);
                            ImGui::EndDragDropSource();
                        }
                        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y));
                        drawLargeIconText(ICON_LC_TABLE, name.c_str(), 12.0f);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();

                // 4. Opérateurs de flux
                std::string flowOpsLabel = std::string(tr("explorer.flow_ops_root")) + "###FlowOpsTree";
                if (ImGui::TreeNode(flowOpsLabel.c_str()))
                {
                    ImGui::PushID("FilterDragRow");
                    ImGui::Selectable("##select_filter_node", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        std::string payload_type = "WHERE_FILTER";
                        ImGui::SetDragDropPayload("DND_PREDICATE_NODE", payload_type.c_str(), payload_type.size() + 1);
                        drawLargeIconText(ICON_LC_FILTER, tr("explorer.where_filter_item"), 12.0f);
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y));
                    drawLargeIconText(ICON_LC_FILTER, tr("explorer.where_filter_item"), 12.0f);
                    ImGui::PopID();
                    ImGui::TreePop();
                }

                // 5. Requêtes Enregistrées (avec classement par dossiers logiques)
                ImGui::PushID("SavedQueriesSection");
                std::string savedQueriesLabel = std::string(tr("explorer.saved_queries_root")) + "###SavedQueriesTree";
                bool queriesTreeOpen = ImGui::TreeNode(savedQueriesLabel.c_str());
                // Cible Drag & Drop sur la racine : permet de sortir une requête d'un dossier
                if (m_activeProject && ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_CTE_QUERY_NODE"))
                    {
                        std::string droppedQueryName(static_cast<const char *>(payload->Data));
                        for (auto &sq : m_savedQueries)
                        {
                            if (sq.name == droppedQueryName)
                            {
                                sq.folder.clear();
                                m_activeProject->setQueryFolder(sq.name, "");
                                break;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Menu contextuel sur la racine : Créer un dossier logique
                if (ImGui::BeginPopupContextItem("SavedQueriesRootCtx", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::MenuItem((std::string(ICON_LC_FOLDER_PLUS) + "  " + tr("explorer.ctx_new_folder")).c_str()))
                    {
                        memset(m_newQueryFolderNameBuf, 0, sizeof(m_newQueryFolderNameBuf));
                        m_showCreateQueryFolderModal = true;
                    }
                    ImGui::EndPopup();
                }

                if (queriesTreeOpen)
                {
                    bool hasFolders = (m_activeProject && !m_activeProject->folders().empty());
                    if (m_savedQueries.empty() && !hasFolders)
                    {
                        ImGui::TextDisabled("   %s", tr("explorer.no_queries"));
                    }
                    else
                    {
                        int queryIndexToDuplicate = -1;

                        // Helper d'affichage d'une ligne de requête (réutilisé pour chaque dossier et pour la racine)
                        auto drawQueryRow = [&](int i)
                        {
                            auto &saved = m_savedQueries[i];
                            ImGui::PushID(i);

                            if (ImGui::Selectable("##QueryRow", false, ImGuiSelectableFlags_AllowDoubleClick))
                            {
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                    loadQueryIntoVisualDesigner(saved);
                            }

                            if (ImGui::BeginPopupContextItem("QueryContextMenu"))
                            {
                                ImGui::TextDisabled("%s %s : %s", ICON_LC_FILE_TEXT, tr("explorer.query_prefix"), saved.name.c_str());
                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_FOLDER_OPEN) + "  " + tr("explorer.ctx_edit_designer")).c_str()))
                                    loadQueryIntoVisualDesigner(saved);

                                if (ImGui::MenuItem((std::string(ICON_LC_SEARCH) + "  " + tr("explorer.ctx_inspect_sql")).c_str()))
                                {
                                    m_inspectingQueryIndex = i;
                                    m_showInspectQueryModal = true;
                                }

                                // Sous-menu : Déplacer vers un dossier virtuel
                                if (m_activeProject && ImGui::BeginMenu((std::string(ICON_LC_FOLDER_INPUT) + "  " + tr("explorer.ctx_move_to_folder")).c_str()))
                                {
                                    if (ImGui::MenuItem(tr("explorer.root_folder_option"), nullptr, saved.folder.empty()))
                                    {
                                        saved.folder.clear();
                                        m_activeProject->setQueryFolder(saved.name, "");
                                    }

                                    if (!m_activeProject->folders().empty())
                                        ImGui::Separator();

                                    for (const auto &folderName : m_activeProject->folders())
                                    {
                                        bool isCurrentFolder = (saved.folder == folderName);
                                        if (ImGui::MenuItem(folderName.c_str(), nullptr, isCurrentFolder))
                                        {
                                            saved.folder = folderName;
                                            m_activeProject->setQueryFolder(saved.name, folderName);
                                        }
                                    }
                                    ImGui::EndMenu();
                                }

                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_TYPE) + "  " + tr("explorer.ctx_rename")).c_str()))
                                {
                                    m_renamingQueryIndex = i;
                                    strncpy(m_renameQueryBuf, saved.name.c_str(), sizeof(m_renameQueryBuf) - 1);
                                    m_renameQueryBuf[sizeof(m_renameQueryBuf) - 1] = '\0';
                                    m_showRenameQueryModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_COPY) + "  " + tr("explorer.ctx_duplicate")).c_str()))
                                    queryIndexToDuplicate = i;

                                ImGui::Separator();

                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  " + tr("explorer.ctx_delete_project")).c_str()))
                                {
                                    m_deletingQueryIndex = i;
                                    m_showDeleteQueryModal = true;
                                }
                                ImGui::PopStyleColor();

                                ImGui::EndPopup();
                            }

                            if (ImGui::IsItemHovered())
                            {
                                if (!saved.description.empty())
                                    ImGui::SetItemTooltip("%s\n%s", tr("explorer.query_hover_tooltip"), saved.description.c_str());
                                else
                                    ImGui::SetItemTooltip("%s", tr("explorer.query_hover_tooltip"));
                            }

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("DND_CTE_QUERY_NODE", saved.name.c_str(), saved.name.size() + 1);
                                drawLargeIconText(ICON_LC_FILE_TEXT, ("CTE : " + saved.name).c_str(), 12.0f);
                                ImGui::EndDragDropSource();
                            }

                            ImGui::SameLine(0.0f, 0.0f);
                            drawLargeIconText(ICON_LC_FILE_TEXT, saved.name.c_str(), 12.0f);
                            ImGui::PopID();
                        };

                        int totalQueries = static_cast<int>(m_savedQueries.size());

                        // A. Affichage des dossiers logiques (Finance, RH, etc.)
                        if (m_activeProject)
                        {
                            std::string folderToDelete;
                            for (const auto &folderName : m_activeProject->folders())
                            {
                                ImGui::PushID(("QueryFolder_" + folderName).c_str());
                                std::string folderLabel = std::string(ICON_LC_FOLDER) + "  " + folderName + "###QueryFolderNode";
                                bool folderOpen = ImGui::TreeNode(folderLabel.c_str());
                                // Cible Drag & Drop : déposer une requête dans ce dossier
                                if (ImGui::BeginDragDropTarget())
                                {
                                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_CTE_QUERY_NODE"))
                                    {
                                        std::string droppedQueryName(static_cast<const char *>(payload->Data));
                                        for (auto &sq : m_savedQueries)
                                        {
                                            if (sq.name == droppedQueryName)
                                            {
                                                sq.folder = folderName;
                                                m_activeProject->setQueryFolder(sq.name, folderName);
                                                break;
                                            }
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }

                                // Menu contextuel du dossier : Renommer / Supprimer
                                if (ImGui::BeginPopupContextItem("QueryFolderContextMenu"))
                                {
                                    if (ImGui::MenuItem((std::string(ICON_LC_TYPE) + "  " + tr("explorer.ctx_rename_folder")).c_str()))
                                    {
                                        m_renamingQueryFolderOldName = folderName;
                                        strncpy(m_renameQueryFolderBuf, folderName.c_str(), sizeof(m_renameQueryFolderBuf) - 1);
                                        m_renameQueryFolderBuf[sizeof(m_renameQueryFolderBuf) - 1] = '\0';
                                        m_showRenameQueryFolderModal = true;
                                    }

                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                    if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  " + tr("explorer.ctx_delete_folder")).c_str()))
                                    {
                                        folderToDelete = folderName;
                                    }
                                    ImGui::PopStyleColor();
                                    ImGui::EndPopup();
                                }

                                if (folderOpen)
                                {
                                    for (int i = 0; i < totalQueries; ++i)
                                    {
                                        if (m_savedQueries[i].folder == folderName)
                                            drawQueryRow(i);
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }

                            if (!folderToDelete.empty())
                            {
                                m_activeProject->removeFolder(folderToDelete, true);
                                for (auto &sq : m_savedQueries)
                                {
                                    if (sq.folder == folderToDelete)
                                        sq.folder.clear();
                                }
                            }
                            // B. Affichage des requêtes sans dossier (à la racine)
                            for (int i = 0; i < totalQueries; ++i)
                            {
                                if (m_savedQueries[i].folder.empty())
                                    drawQueryRow(i);
                            }
                            // Duplication différée hors de la boucle d'itération
                            if (queryIndexToDuplicate >= 0 && queryIndexToDuplicate < static_cast<int>(m_savedQueries.size()))
                            {
                                SavedQuery copy = m_savedQueries[queryIndexToDuplicate];
                                std::string baseName = copy.name + tr("explorer.copy_suffix");
                                std::string finalName = baseName;
                                int suffix = 2;
                                while (std::any_of(m_savedQueries.begin(), m_savedQueries.end(),
                                                   [&finalName](const SavedQuery &q)
                                                   { return q.name == finalName; }))
                                {
                                    finalName = baseName + "_" + std::to_string(suffix++);
                                }
                                copy.name = finalName;
                                if (m_activeProject && !copy.folder.empty())
                                    m_activeProject->setQueryFolder(copy.name, copy.folder);
                                m_savedQueries.push_back(std::move(copy));
                            }
                        }
                        ImGui::TreePop();
                    }
                    // ImGui::PopID();
                }
                ImGui::PopID();

                // 6. Tâches (Pipelines)
                ImGui::PushID("TasksHeaderNode");
                std::string tasksRootLabel = std::string(tr("explorer.tasks_root")) + "###TasksTree";
                bool tasks_open = ImGui::TreeNode(tasksRootLabel.c_str());
                if (ImGui::BeginPopupContextItem("TasksContextMenu", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_PLUS_CIRCLE) + "  " + tr("explorer.ctx_new_task")).c_str()))
                    {
                        m_showCreateTaskModal = true;
                        m_selectedQueryIndexForTask = -1;
                        m_selectedTargetTypeIndex = 0;
                        memset(m_newTaskNameBuf, 0, sizeof(m_newTaskNameBuf));
                        memset(m_newTaskStepNameBuf, 0, sizeof(m_newTaskStepNameBuf));
                    }
                    ImGui::EndPopup();
                }
                if (tasks_open)
                {
                    if (m_savedTasks.empty())
                    {
                        ImGui::TextDisabled("   %s", tr("explorer.no_tasks"));
                    }
                    else
                    {
                        std::string taskToDuplicate = "";

                        for (size_t tIdx = 0; tIdx < m_savedTasks.size(); ++tIdx)
                        {
                            const auto &task = m_savedTasks[tIdx];
                            ImGui::PushID(task.name().c_str());
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
                            std::string row_label = std::string(ICON_LC_ACTIVITY) + "  " + task.name();

                            if (ImGui::Selectable(row_label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 22.0f)))
                            {
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    m_targetTaskName = task.name();
                                    m_editTaskUnits.clear();
                                    for (const auto &[stepId, unit] : task.steps())
                                        m_editTaskUnits.push_back(unit);
                                    m_showEditTaskModal = true;
                                }
                            }

                            // 1. Menu contextuel clic droit sur la Tâche
                            if (ImGui::BeginPopupContextItem(("TaskItemCtx_" + task.name()).c_str(), ImGuiPopupFlags_MouseButtonRight))
                            {
                                ImGui::TextDisabled("%s %s : %s", ICON_LC_ACTIVITY, tr("explorer.pipeline_prefix"), task.name().c_str());
                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_EDIT) + "  " + tr("explorer.ctx_edit_steps")).c_str()))
                                {
                                    m_targetTaskName = task.name();
                                    m_editTaskUnits.clear();
                                    for (const auto &[stepId, unit] : task.steps())
                                        m_editTaskUnits.push_back(unit);
                                    m_showEditTaskModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_TYPE) + "  " + tr("explorer.ctx_rename")).c_str()))
                                {
                                    m_targetTaskName = task.name();
                                    snprintf(m_editTaskNameBuf, sizeof(m_editTaskNameBuf), "%s", task.name().c_str());
                                    m_showRenameTaskModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_COPY) + "  " + tr("explorer.ctx_duplicate")).c_str()))
                                {
                                    taskToDuplicate = task.name();
                                }

                                ImGui::Separator();

                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  " + tr("explorer.ctx_delete")).c_str()))
                                {
                                    m_targetTaskName = task.name();
                                    m_showDeleteTaskModal = true;
                                }
                                ImGui::PopStyleColor();

                                ImGui::EndPopup();
                            }

                            // 2. Source Drag & Drop vers le Workflow Designer
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("DND_WORKFLOW_TASK_NODE", task.name().c_str(), task.name().length() + 1);
                                ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s  %s : %s", ICON_LC_ACTIVITY, tr("explorer.schedule_prefix"), task.name().c_str());
                                ImGui::EndDragDropSource();
                            }

                            // 3. Info-bulle des étapes
                            if (ImGui::IsItemHovered())
                            {
                                std::string tooltip_txt = std::string(tr("explorer.pipeline_prefix")) + " : " + task.name() + "\n" + tr("explorer.seq_steps_label");
                                if (task.steps().empty())
                                    tooltip_txt += std::string("\n   ") + tr("explorer.no_steps_item");
                                else
                                {
                                    for (const auto &[stepId, unit] : task.steps())
                                        tooltip_txt += "\n   [" + std::string(tr("task.step_prefix")) + " " + std::to_string(stepId) + "] -> " + unit.name;
                                }
                                ImGui::SetItemTooltip("%s", tooltip_txt.c_str());
                            }
                            ImGui::PopID();
                        }

                        // Traitement différé de la duplication hors de la boucle
                        if (!taskToDuplicate.empty())
                        {
                            auto it = std::find_if(m_savedTasks.begin(), m_savedTasks.end(),
                                                   [&taskToDuplicate](const cppsqlx::Task &t)
                                                   { return t.name() == taskToDuplicate; });
                            if (it != m_savedTasks.end())
                            {
                                std::string baseCopyName = taskToDuplicate + tr("explorer.copy_suffix");
                                std::string copyName = baseCopyName;
                                int suffix = 2;
                                while (std::any_of(m_savedTasks.begin(), m_savedTasks.end(),
                                                   [&copyName](const cppsqlx::Task &t)
                                                   { return t.name() == copyName; }))
                                {
                                    copyName = baseCopyName + "_" + std::to_string(suffix++);
                                }
                                cppsqlx::Task clonedTask(copyName);
                                for (const auto &[stepId, unit] : it->steps())
                                {
                                    clonedTask.addStep(stepId, unit);
                                }
                                m_savedTasks.push_back(std::move(clonedTask));
                            }
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();

                // 8. Workflows Enregistrés
                std::string savedWfLabel = std::string(tr("explorer.saved_workflows_root")) + "###SavedWorkflowsTreeNode";
                if (ImGui::TreeNode(savedWfLabel.c_str()))
                {
                    if (m_savedWorkflows.empty())
                    {
                        ImGui::TextDisabled("   %s", tr("explorer.no_workflows"));
                    }
                    else
                    {
                        int workflowIndexToDelete = -1;
                        int totalWorkflows = static_cast<int>(m_savedWorkflows.size());
                        for (int i = 0; i < totalWorkflows; ++i)
                        {
                            auto &wf = m_savedWorkflows[i];
                            ImGui::PushID(i);
                            if (ImGui::Selectable("##WorkflowRow", false, ImGuiSelectableFlags_AllowDoubleClick))
                            {
                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                    loadWorkflowIntoVisualDesigner(wf);
                            }
                            if (ImGui::BeginPopupContextItem("WorkflowContextMenu"))
                            {
                                if (ImGui::MenuItem(tr("explorer.ctx_edit_designer")))
                                    loadWorkflowIntoVisualDesigner(wf);
                                if (ImGui::MenuItem(tr("explorer.ctx_rename_simple")))
                                {
                                    m_renamingWorkflowIndex = i;
                                    strncpy(m_renameWorkflowBuf, wf.name.c_str(), sizeof(m_renameWorkflowBuf) - 1);
                                    m_renameWorkflowBuf[sizeof(m_renameWorkflowBuf) - 1] = '\0';
                                    m_showRenameWorkflowModal = true;
                                }
                                ImGui::Separator();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem(tr("explorer.ctx_delete_project_simple")))
                                    workflowIndexToDelete = i;
                                ImGui::PopStyleColor();
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered())
                            {
                                if (!wf.description.empty())
                                    ImGui::SetItemTooltip("%s\n%s", tr("explorer.query_hover_tooltip"), wf.description.c_str());
                                else
                                    ImGui::SetItemTooltip("%s", tr("explorer.query_hover_tooltip"));
                            }
                            ImGui::SameLine(0.0f, 0.0f);
                            drawLargeIconText(ICON_LC_ACTIVITY, wf.name.c_str(), 12.0f);
                            ImGui::PopID();
                        }
                        if (workflowIndexToDelete >= 0)
                            m_savedWorkflows.erase(m_savedWorkflows.begin() + workflowIndexToDelete);
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }
        break;
    case ActiveTab::Search:
        drawLargeIconText(ICON_LC_SEARCH, tr("sidebar.search_title"), 14.0f);
        break;
    case ActiveTab::Git:
        drawLargeIconText(ICON_LC_GIT_BRANCH, tr("sidebar.git_title"), 14.0f);
        break;
    case ActiveTab::Package:
        drawLargeIconText(ICON_LC_BOX, tr("sidebar.package_title"), 14.0f);
        break;
    case ActiveTab::Settings:
    {
        drawLargeIconText(ICON_LC_SETTINGS, tr("sidebar.settings_title"), 14.0f);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s", tr("settings.appearance_header"));
        ImGui::Spacing();

        const char *themeNames[] = {
            tr("settings.theme_dark_full"),
            tr("settings.theme_light_full"),
            tr("settings.theme_nord_full"),
            tr("settings.theme_dracula_full")};

        int currentThemeIdx = static_cast<int>(m_currentTheme);
        ImGui::Text("%s", tr("settings.palette_label"));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##AppThemeSelector", &currentThemeIdx, themeNames, IM_ARRAYSIZE(themeNames)))
        {
            applyTheme(static_cast<AppTheme>(currentThemeIdx));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Boutons rapides de prévisualisation avec pastille de couleur
        auto drawThemePresetCard = [&](const char *label, AppTheme targetTheme, ImVec4 swatchColor)
        {
            bool isSelected = (m_currentTheme == targetTheme);
            int themeId = static_cast<int>(targetTheme);
            ImGui::ColorButton(("##swatch_" + std::to_string(themeId)).c_str(), swatchColor, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(18.0f, 18.0f));
            ImGui::SameLine();
            std::string itemLabel = std::string(label) + "###ThemePreset_" + std::to_string(themeId);
            if (ImGui::Selectable(itemLabel.c_str(), isSelected))
            {
                applyTheme(targetTheme);
            }
        };

        drawThemePresetCard(tr("settings.theme_dark_short"), AppTheme::DarkModern, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
        drawThemePresetCard(tr("settings.theme_light_short"), AppTheme::Light, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
        drawThemePresetCard(tr("settings.theme_nord_short"), AppTheme::Nord, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
        drawThemePresetCard(tr("settings.theme_dracula_short"), AppTheme::Dracula, ImVec4(0.16f, 0.16f, 0.21f, 1.0f));

        break;
    }
    }
    ImGui::End();
}

void Application::renderTaskManagementModals()
{
    // =========================================================================
    // 1. MODALE D'ÉDITION / RÉORGANISATION DES ÉTAPES
    // =========================================================================
    std::string editTaskTitle = std::string(tr("edit_task.modal_title")) + "###EditTaskStepsModal";
    if (m_showEditTaskModal)
        ImGui::OpenPopup(editTaskTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(560.0f, 470.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(editTaskTitle.c_str(), &m_showEditTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s : %s", ICON_LC_ACTIVITY, tr("explorer.pipeline_prefix"), m_targetTaskName.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        // Zone d'ajout d'une nouvelle étape depuis les requêtes enregistrées
        ImGui::Text("%s", tr("edit_task.add_step_label"));
        static int s_editTaskQueryIdx = 0;
        static std::array<char, 256> s_editTaskNewStepName{};

        if (m_savedQueries.empty())
        {
            ImGui::TextDisabled("  %s", tr("edit_task.no_saved_queries"));
        }
        else
        {
            if (s_editTaskQueryIdx >= static_cast<int>(m_savedQueries.size()))
                s_editTaskQueryIdx = 0;

            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##EditTaskQueryCombo", m_savedQueries[s_editTaskQueryIdx].name.c_str()))
            {
                for (int qIdx = 0; qIdx < static_cast<int>(m_savedQueries.size()); ++qIdx)
                {
                    if (ImGui::Selectable(m_savedQueries[qIdx].name.c_str(), s_editTaskQueryIdx == qIdx))
                    {
                        s_editTaskQueryIdx = qIdx;
                        if (strlen(s_editTaskNewStepName.data()) == 0)
                        {
                            snprintf(s_editTaskNewStepName.data(), s_editTaskNewStepName.size(), "%s", m_savedQueries[qIdx].name.c_str());
                        }
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##EditTaskStepLabel", tr("edit_task.step_name_hint"), s_editTaskNewStepName.data(), s_editTaskNewStepName.size());

            ImGui::SameLine();
            std::string addStepBtnLabel = std::string(ICON_LC_PLUS) + " " + tr("edit_task.btn_add_step") + "###EditTaskAddStepBtn";
            if (ImGui::Button(addStepBtnLabel.c_str(), ImVec2(145.0f, 0.0f)))
            {
                cppsqlx::UnitSelect newUnit{};
                newUnit.name = (strlen(s_editTaskNewStepName.data()) > 0)
                                   ? std::string(s_editTaskNewStepName.data())
                                   : m_savedQueries[s_editTaskQueryIdx].name;
                newUnit.query = cppsqlx::SelectQuery::fromJSON(m_savedQueries[s_editTaskQueryIdx].compiledSelectJson).value_or(cppsqlx::SelectQuery());

                m_editTaskUnits.push_back(std::move(newUnit));
                s_editTaskNewStepName.fill('\0');
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Liste ordonnée des étapes existantes
        ImGui::Text("%s (%d %s) :", tr("edit_task.sequence_label"), static_cast<int>(m_editTaskUnits.size()), tr("edit_task.steps_unit"));
        ImGui::BeginChild("EditTaskStepsZone", ImVec2(-1.0f, 230.0f), true);

        int moveUp = -1, moveDown = -1, removeIdx = -1;
        for (int i = 0; i < static_cast<int>(m_editTaskUnits.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::Text("[%s %d]  %s", tr("task.step_prefix"), i + 1, m_editTaskUnits[i].name.c_str());

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 95.0f);
            if (ImGui::SmallButton(ICON_LC_ARROW_UP) && i > 0)
                moveUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_LC_ARROW_DOWN) && i + 1 < static_cast<int>(m_editTaskUnits.size()))
                moveDown = i;
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            if (ImGui::SmallButton(ICON_LC_TRASH))
                removeIdx = i;
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        if (moveUp > 0)
            std::swap(m_editTaskUnits[moveUp], m_editTaskUnits[moveUp - 1]);
        if (moveDown >= 0 && moveDown + 1 < static_cast<int>(m_editTaskUnits.size()))
            std::swap(m_editTaskUnits[moveDown], m_editTaskUnits[moveDown + 1]);
        if (removeIdx >= 0)
            m_editTaskUnits.erase(m_editTaskUnits.begin() + removeIdx);

        ImGui::EndChild();
        ImGui::Spacing();

        if (ImGui::Button(tr("btn.save"), ImVec2(120.0f, 0.0f)))
        {
            auto it = std::find_if(m_savedTasks.begin(), m_savedTasks.end(),
                                   [this](const cppsqlx::Task &t)
                                   { return t.name() == m_targetTaskName; });
            if (it != m_savedTasks.end())
            {
                cppsqlx::Task updatedTask(m_targetTaskName);
                for (size_t idx = 0; idx < m_editTaskUnits.size(); ++idx)
                {
                    updatedTask.addStep(static_cast<int>(idx + 1), m_editTaskUnits[idx]);
                }
                *it = std::move(updatedTask);
            }
            m_showEditTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("btn.cancel"), ImVec2(110.0f, 0.0f)))
        {
            m_showEditTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 2. MODALE DE RENOMMAGE (AVEC MISE À JOUR DU WORKFLOW DESIGNER)
    // =========================================================================
    std::string renTaskTitle = std::string(tr("rename_task.modal_title")) + "###RenameTaskModal";
    if (m_showRenameTaskModal)
        ImGui::OpenPopup(renTaskTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(renTaskTitle.c_str(), &m_showRenameTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s '%s' :", tr("rename_task.new_name_for"), m_targetTaskName.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##RenameTaskBuf", m_editTaskNameBuf, sizeof(m_editTaskNameBuf));
        ImGui::Spacing();

        std::string newName(m_editTaskNameBuf);
        bool canRename = !newName.empty() &&
                         (newName == m_targetTaskName ||
                          std::none_of(m_savedTasks.begin(), m_savedTasks.end(),
                                       [&newName](const cppsqlx::Task &t)
                                       { return t.name() == newName; }));

        if (!canRename)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.confirm"), ImVec2(110.0f, 0.0f)))
        {
            if (newName != m_targetTaskName)
            {
                auto it = std::find_if(m_savedTasks.begin(), m_savedTasks.end(),
                                       [this](const cppsqlx::Task &t)
                                       { return t.name() == m_targetTaskName; });
                if (it != m_savedTasks.end())
                {
                    cppsqlx::Task renamedTask(newName);
                    for (const auto &[stepId, unit] : it->steps())
                    {
                        renamedTask.addStep(stepId, unit);
                    }
                    *it = std::move(renamedTask);

                    // Mise à jour automatique des nœuds du Workflow Designer qui référencent cette tâche
                    for (auto &wfNode : m_workflowNodes)
                    {
                        if (wfNode.taskName == m_targetTaskName)
                            wfNode.taskName = newName;
                    }
                }
            }
            m_showRenameTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canRename)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(tr("btn.cancel"), ImVec2(110.0f, 0.0f)))
        {
            m_showRenameTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 3. MODALE DE CONFIRMATION DE SUPPRESSION
    // =========================================================================
    std::string delTaskTitle = std::string(tr("delete_task.modal_title")) + "###DeleteTaskModal";
    if (m_showDeleteTaskModal)
        ImGui::OpenPopup(delTaskTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(420.0f, 155.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(delTaskTitle.c_str(), &m_showDeleteTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped("%s '%s' %s", tr("delete_task.confirm_prefix"), m_targetTaskName.c_str(), tr("delete_task.confirm_suffix"));
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button(tr("explorer.ctx_delete"), ImVec2(120.0f, 0.0f)))
        {
            std::erase_if(m_savedTasks, [this](const cppsqlx::Task &t)
                          { return t.name() == m_targetTaskName; });
            m_targetTaskName.clear();
            m_showDeleteTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button(tr("btn.cancel"), ImVec2(110.0f, 0.0f)))
        {
            m_targetTaskName.clear();
            m_showDeleteTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::renderQueryInspectAndDeleteModals()
{
    // =========================================================================
    // MODALE : INSPECTER LE SQL ET LE SCHÉMA D'UNE REQUÊTE ENREGISTRÉE
    // =========================================================================
    std::string inspectModalTitle = std::string(tr("inspect_query.modal_title")) + "###InspectQueryModal";
    if (m_showInspectQueryModal && m_inspectingQueryIndex >= 0 && m_inspectingQueryIndex < static_cast<int>(m_savedQueries.size()))
    {
        ImGui::OpenPopup(inspectModalTitle.c_str());
    }

    ImGui::SetNextWindowSize(ImVec2(920.0f, 620.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 420.0f), ImVec2(1920.0f, 1080.0f));

    if (ImGui::BeginPopupModal(inspectModalTitle.c_str(), &m_showInspectQueryModal, ImGuiWindowFlags_None))
    {
        if (m_inspectingQueryIndex >= 0 && m_inspectingQueryIndex < static_cast<int>(m_savedQueries.size()))
        {
            const auto &sq = m_savedQueries[m_inspectingQueryIndex];

            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s : %s", ICON_LC_FILE_TEXT, tr("explorer.query_prefix"), sq.name.c_str());
            if (!sq.description.empty())
            {
                ImGui::TextDisabled("%s", sq.description.c_str());
            }
            ImGui::Separator();
            ImGui::Spacing();

            float bodyHeight = ImGui::GetContentRegionAvail().y - 45.0f;
            if (bodyHeight < 200.0f)
                bodyHeight = 200.0f;

            // Panneau gauche (58%) : Requête SQL générée, formatée et colorée
            float leftWidth = ImGui::GetContentRegionAvail().x * 0.58f;
            ImGui::BeginChild("InspectSqlPane", ImVec2(leftWidth, bodyHeight), true);
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s", tr("inspect_query.sql_section"));
            ImGui::Separator();
            ImGui::Spacing();

            std::string formattedSql = dizui::sql::formatRawSqlQuery(sq.sql);

            // Rend le texte brut transparent tout en conservant la sélection et le défilement natifs
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::InputTextMultiline("##InspectQuerySqlText", formattedSql.data(), formattedSql.size() + 1,
                                      ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();

            ImVec2 editorMin = ImGui::GetItemRectMin();
            ImVec2 editorMax = ImGui::GetItemRectMax();
            ImVec2 framePad = ImGui::GetStyle().FramePadding;

            float scrollX = 0.0f;
            float scrollY = 0.0f;
            if (ImGuiWindow *childWin = ImGui::FindWindowByName((std::string(ImGui::GetCurrentWindow()->Name) + "/##InspectQuerySqlText").c_str()))
            {
                scrollX = childWin->Scroll.x;
                scrollY = childWin->Scroll.y;
            }

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(editorMin, editorMax, true);

            ImVec2 textOrigin(editorMin.x + framePad.x - scrollX, editorMin.y + framePad.y - scrollY);
            dizui::sql::renderSqlSyntaxHighlightOverlay(
                drawList,
                textOrigin,
                formattedSql.c_str(),
                ImGui::GetFont(),
                ImGui::GetFontSize());

            drawList->PopClipRect();

            ImGui::EndChild();

            ImGui::SameLine();

            // Panneau droit (42%) : Schéma de sortie déduit
            ImGui::BeginChild("InspectSchemaPane", ImVec2(0.0f, bodyHeight), true);
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "%s (%d)",
                               tr("inspect_query.schema_section"), static_cast<int>(sq.outputSchema.size()));
            ImGui::Separator();
            ImGui::Spacing();

            if (sq.outputSchema.empty())
            {
                ImGui::TextDisabled("%s", tr("inspect_query.empty_schema"));
            }
            else if (ImGui::BeginTable("InspectQuerySchemaTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
                ImGui::TableSetupColumn(tr("reverse.col_name"), ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableSetupColumn(tr("reverse.col_type"), ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < sq.outputSchema.size(); ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%d", static_cast<int>(i + 1));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(sq.outputSchema[i].name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.50f, 1.0f), "%s", sq.outputSchema[i].dataType.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Boutons d'action en bas
            std::string copyBtnLabel = std::string(ICON_LC_COPY) + "  " + tr("ai.btn_copy_sql") + "###InspectCopySqlBtn";
            if (ImGui::Button(copyBtnLabel.c_str(), ImVec2(210.0f, 0.0f)))
            {
                snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", sq.sql.c_str());
                addConsoleLog(tr("cte_stack.log_sql_copied"), LogLevel::Info);
            }

            ImGui::SameLine();
            float availX = ImGui::GetContentRegionAvail().x;
            if (availX > 120.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availX - 120.0f);

            if (ImGui::Button(tr("btn.close"), ImVec2(120.0f, 0.0f)))
            {
                m_showInspectQueryModal = false;
                m_inspectingQueryIndex = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE : CONFIRMATION DE SUPPRESSION D'UNE REQUÊTE
    // =========================================================================
    std::string delQueryTitle = std::string(tr("delete_query.modal_title")) + "###DeleteQueryModal";
    if (m_showDeleteQueryModal)
        ImGui::OpenPopup(delQueryTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(440.0f, 160.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(delQueryTitle.c_str(), &m_showDeleteQueryModal, ImGuiWindowFlags_NoResize))
    {
        if (m_deletingQueryIndex >= 0 && m_deletingQueryIndex < static_cast<int>(m_savedQueries.size()))
        {
            ImGui::TextWrapped("%s '%s' ?", tr("delete_query.confirm_msg"), m_savedQueries[m_deletingQueryIndex].name.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float availX = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availX - 220.0f);
        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showDeleteQueryModal = false;
            m_deletingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button(tr("explorer.ctx_delete"), ImVec2(110.0f, 0.0f)))
        {
            if (m_deletingQueryIndex >= 0 && m_deletingQueryIndex < static_cast<int>(m_savedQueries.size()))
            {
                m_savedQueries.erase(m_savedQueries.begin() + m_deletingQueryIndex);
            }
            m_showDeleteQueryModal = false;
            m_deletingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
}

void Application::renderQueryFolderModals()
{
    if (!m_activeProject)
        return;

    // =========================================================================
    // MODALE 1 : CRÉER UN DOSSIER LOGIQUE DE REQUÊTES
    // =========================================================================
    std::string newFolderModalTitle = std::string(tr("explorer.new_folder_modal_title")) + "###CreateQueryFolderModal";
    if (m_showCreateQueryFolderModal)
        ImGui::OpenPopup(newFolderModalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(380.0f, 145.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(newFolderModalTitle.c_str(), &m_showCreateQueryFolderModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("explorer.folder_name_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##NewQueryFolderInput", m_newQueryFolderNameBuf, IM_ARRAYSIZE(m_newQueryFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showCreateQueryFolderModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool valid = (strlen(m_newQueryFolderNameBuf) > 0);
        if (!valid)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.confirm"), ImVec2(110.0f, 0.0f)) || (enterPressed && valid))
        {
            m_activeProject->createFolder(std::string(m_newQueryFolderNameBuf));
            m_showCreateQueryFolderModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!valid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE 2 : RENOMMER UN DOSSIER LOGIQUE DE REQUÊTES
    // =========================================================================
    std::string renFolderModalTitle = std::string(tr("explorer.rename_folder_modal_title")) + "###RenameQueryFolderModal";
    if (m_showRenameQueryFolderModal)
        ImGui::OpenPopup(renFolderModalTitle.c_str());

    ImGui::SetNextWindowSize(ImVec2(380.0f, 145.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal(renFolderModalTitle.c_str(), &m_showRenameQueryFolderModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("%s", tr("explorer.folder_name_label"));
        ImGui::SetNextItemWidth(-1.0f);
        bool enterPressed = ImGui::InputText("##RenQueryFolderInput", m_renameQueryFolderBuf, IM_ARRAYSIZE(m_renameQueryFolderBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(tr("btn.cancel"), ImVec2(100.0f, 0.0f)))
        {
            m_showRenameQueryFolderModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool valid = (strlen(m_renameQueryFolderBuf) > 0);
        if (!valid)
            ImGui::BeginDisabled();
        if (ImGui::Button(tr("btn.confirm"), ImVec2(110.0f, 0.0f)) || (enterPressed && valid))
        {
            std::string newName(m_renameQueryFolderBuf);
            m_activeProject->renameFolder(m_renamingQueryFolderOldName, newName);
            for (auto &sq : m_savedQueries)
            {
                if (sq.folder == m_renamingQueryFolderOldName)
                    sq.folder = newName;
            }
            m_showRenameQueryFolderModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!valid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}
