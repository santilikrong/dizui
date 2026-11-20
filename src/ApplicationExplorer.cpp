#include "Application.hpp"
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
        drawLargeIconText(ICON_LC_BLOCKS, "Espace Projet", 14.0f);
        ImGui::Separator();
        ImGui::Spacing();
        if (m_activeProject)
        {
            std::string project_root_label = std::string(ICON_LC_LAYOUT_GRID) + "  " + m_activeProject->name() + "###ProjectRootNode";
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode(project_root_label.c_str()))
            {
                drawLargeIconText(ICON_LC_FOLDER_OPEN, ("Chemin : " + m_activeProject->path()).c_str(), 14.0f);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 1. Connexions Physiques
                ImGui::PushID("PhysConnsRoot");
                bool phys_open = ImGui::TreeNode("Connexions Physiques");
                if (ImGui::BeginPopupContextItem("PhysConnsRootContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_PLUS_CIRCLE) + "  Créer une connexion physique...").c_str()))
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
                            if (ImGui::MenuItem("Éditer la connexion"))
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

                            if (ImGui::MenuItem("Renommer la connexion"))
                            {
                                m_renamingPhysConnOldName = name;
                                strncpy(m_renamePhysConnBuf, name.c_str(), sizeof(m_renamePhysConnBuf) - 1);
                                m_showRenamePhysConnModal = true;
                            }

                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                            if (ImGui::MenuItem("Supprimer la connexion"))
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
                bool log_open = ImGui::TreeNode("Passerelles Logiques");
                if (ImGui::BeginPopupContextItem("LogConnsRootContext", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_PLUS_CIRCLE) + "  Créer une connexion logique...").c_str()))
                    {
                        m_isEditingLogConn = false;
                        m_originalLogConnName = "";
                        memset(m_logConnNameBuf, 0, sizeof(m_logConnNameBuf));
                        m_logConnDevTarget = "";
                        m_logConnTestTarget = "";
                        m_logConnProdTarget = "";
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
                            if (ImGui::MenuItem("Éditer la connexion"))
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

                            if (ImGui::MenuItem("Renommer la connexion"))
                            {
                                m_renamingLogConnOldName = name;
                                strncpy(m_renameLogConnBuf, name.c_str(), sizeof(m_renameLogConnBuf) - 1);
                                m_showRenameLogConnModal = true;
                            }

                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                            if (ImGui::MenuItem("Supprimer la connexion"))
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
                                    drawLargeIconText(ICON_LC_SERVER, ("Cluster : " + physConn.name_).c_str(), 12.0f);
                                    drawLargeIconText(ICON_LC_DATABASE, ("Base    : " + physConn.dbname_).c_str(), 12.0f);
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
                bool datasets_open = ImGui::TreeNode("Datasets (Tables)");
                if (ImGui::BeginPopupContextItem("DatasetsContextMenu", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_REFRESH_CW) + "  Reverse Datasets...").c_str()))
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
                if (ImGui::TreeNode("Opérateurs de flux"))
                {
                    ImGui::PushID("FilterDragRow");
                    ImGui::Selectable("##select_filter_node", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, ImGui::GetTextLineHeightWithSpacing()));
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        std::string payload_type = "WHERE_FILTER";
                        ImGui::SetDragDropPayload("DND_PREDICATE_NODE", payload_type.c_str(), payload_type.size() + 1);
                        drawLargeIconText(ICON_LC_FILTER, "Filtre (Clause WHERE)", 12.0f);
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y));
                    drawLargeIconText(ICON_LC_FILTER, "Filtre (Clause WHERE)", 12.0f);
                    ImGui::PopID();
                    ImGui::TreePop();
                }

                // 5. Requêtes Enregistrées
                if (ImGui::TreeNode("Requêtes Enregistrées"))
                {
                    if (m_savedQueries.empty())
                    {
                        ImGui::TextDisabled("   (Aucune requête)");
                    }
                    else
                    {
                        int queryIndexToDuplicate = -1;
                        int totalQueries = static_cast<int>(m_savedQueries.size());

                        for (int i = 0; i < totalQueries; ++i)
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
                                ImGui::TextDisabled("%s Requête : %s", ICON_LC_FILE_TEXT, saved.name.c_str());
                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_FOLDER_OPEN) + "  Éditer dans le designer").c_str()))
                                    loadQueryIntoVisualDesigner(saved);

                                if (ImGui::MenuItem((std::string(ICON_LC_SEARCH) + "  Inspecter le SQL et le Schéma...").c_str()))
                                {
                                    m_inspectingQueryIndex = i;
                                    m_showInspectQueryModal = true;
                                }

                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_TYPE) + "  Renommer...").c_str()))
                                {
                                    m_renamingQueryIndex = i;
                                    strncpy(m_renameQueryBuf, saved.name.c_str(), sizeof(m_renameQueryBuf) - 1);
                                    m_renameQueryBuf[sizeof(m_renameQueryBuf) - 1] = '\0';
                                    m_showRenameQueryModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_COPY) + "  Dupliquer").c_str()))
                                    queryIndexToDuplicate = i;

                                ImGui::Separator();

                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  Supprimer du projet...").c_str()))
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
                                    ImGui::SetItemTooltip("Double-cliquer pour ouvrir | Clic droit pour options\n%s", saved.description.c_str());
                                else
                                    ImGui::SetItemTooltip("Double-cliquer pour ouvrir | Clic droit pour options");
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
                        }

                        // Duplication différée hors de la boucle d'itération
                        if (queryIndexToDuplicate >= 0 && queryIndexToDuplicate < static_cast<int>(m_savedQueries.size()))
                        {
                            SavedQuery copy = m_savedQueries[queryIndexToDuplicate];
                            std::string baseName = copy.name + "_copie";
                            std::string finalName = baseName;
                            int suffix = 2;
                            while (std::any_of(m_savedQueries.begin(), m_savedQueries.end(),
                                               [&finalName](const SavedQuery &q)
                                               { return q.name == finalName; }))
                            {
                                finalName = baseName + "_" + std::to_string(suffix++);
                            }
                            copy.name = finalName;
                            m_savedQueries.push_back(std::move(copy));
                        }
                    }
                    ImGui::TreePop();
                }

                // 6. Tâches (Pipelines)
                ImGui::PushID("TasksHeaderNode");
                bool tasks_open = ImGui::TreeNode("Tâches (Pipelines)");
                if (ImGui::BeginPopupContextItem("TasksContextMenu", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::Selectable((std::string(ICON_LC_PLUS_CIRCLE) + "  Nouvelle tâche...").c_str()))
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
                        ImGui::TextDisabled("   (Aucune tâche créée)");
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
                                ImGui::TextDisabled("%s Pipeline : %s", ICON_LC_ACTIVITY, task.name().c_str());
                                ImGui::Separator();

                                if (ImGui::MenuItem((std::string(ICON_LC_EDIT) + "  Éditer les étapes...").c_str()))
                                {
                                    m_targetTaskName = task.name();
                                    m_editTaskUnits.clear();
                                    for (const auto &[stepId, unit] : task.steps())
                                        m_editTaskUnits.push_back(unit);
                                    m_showEditTaskModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_TYPE) + "  Renommer...").c_str()))
                                {
                                    m_targetTaskName = task.name();
                                    snprintf(m_editTaskNameBuf, sizeof(m_editTaskNameBuf), "%s", task.name().c_str());
                                    m_showRenameTaskModal = true;
                                }

                                if (ImGui::MenuItem((std::string(ICON_LC_COPY) + "  Dupliquer").c_str()))
                                {
                                    taskToDuplicate = task.name();
                                }

                                ImGui::Separator();

                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem((std::string(ICON_LC_TRASH) + "  Supprimer").c_str()))
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
                                ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s  Planifier : %s", ICON_LC_ACTIVITY, task.name().c_str());
                                ImGui::EndDragDropSource();
                            }

                            // 3. Info-bulle des étapes
                            if (ImGui::IsItemHovered())
                            {
                                std::string tooltip_txt = "Pipeline : " + task.name() + "\nÉtapes séquentielles :";
                                if (task.steps().empty())
                                    tooltip_txt += "\n   (Aucune étape)";
                                else
                                {
                                    for (const auto &[stepId, unit] : task.steps())
                                        tooltip_txt += "\n   [Step " + std::to_string(stepId) + "] -> " + unit.name;
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
                                std::string copyName = taskToDuplicate + "_copie";
                                int suffix = 2;
                                while (std::any_of(m_savedTasks.begin(), m_savedTasks.end(),
                                                   [&copyName](const cppsqlx::Task &t)
                                                   { return t.name() == copyName; }))
                                {
                                    copyName = taskToDuplicate + "_copie_" + std::to_string(suffix++);
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
                if (ImGui::TreeNode("Workflows Enregistrés###SavedWorkflowsTreeNode"))
                {
                    if (m_savedWorkflows.empty())
                    {
                        ImGui::TextDisabled("   (Aucun workflow)");
                    }
                    else
                    {
                        int workflowIndexToDelete = -1;
                        int totalWorkflows = (int)m_savedWorkflows.size();
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
                                if (ImGui::MenuItem("Éditer dans le designer"))
                                    loadWorkflowIntoVisualDesigner(wf);
                                if (ImGui::MenuItem("Renommer"))
                                {
                                    m_renamingWorkflowIndex = i;
                                    strncpy(m_renameWorkflowBuf, wf.name.c_str(), sizeof(m_renameWorkflowBuf) - 1);
                                    m_renameWorkflowBuf[sizeof(m_renameWorkflowBuf) - 1] = '\0';
                                    m_showRenameWorkflowModal = true;
                                }
                                ImGui::Separator();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                                if (ImGui::MenuItem("Supprimer du projet"))
                                    workflowIndexToDelete = i;
                                ImGui::PopStyleColor();
                                ImGui::EndPopup();
                            }
                            if (ImGui::IsItemHovered())
                            {
                                if (!wf.description.empty())
                                    ImGui::SetItemTooltip("Double-cliquer pour ouvrir | Clic droit pour options\n%s", wf.description.c_str());
                                else
                                    ImGui::SetItemTooltip("Double-cliquer pour ouvrir | Clic droit pour options");
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
        drawLargeIconText(ICON_LC_SEARCH, "Recherche Avancée", 14.0f);
        break;
    case ActiveTab::Git:
        drawLargeIconText(ICON_LC_GIT_BRANCH, "Gestion des Sources", 14.0f);
        break;
    case ActiveTab::Package:
        drawLargeIconText(ICON_LC_BOX, "Gestionnaire de Packages", 14.0f);
        break;
    case ActiveTab::Settings:
    {
        drawLargeIconText(ICON_LC_SETTINGS, "Paramètres de l'Application", 14.0f);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "APPARENCE & THÈME VISUEL");
        ImGui::Spacing();

        const char *themeNames[] = {
            "Sombre Moderne (Défaut)",
            "Clair (Studio Light)",
            "Nord (Arctique)",
            "Dracula (Nocturne)"};

        int currentThemeIdx = static_cast<int>(m_currentTheme);
        ImGui::Text("Palette de couleurs :");
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
            ImGui::ColorButton(("##swatch_" + std::string(label)).c_str(), swatchColor, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(18.0f, 18.0f));
            ImGui::SameLine();
            if (ImGui::Selectable(label, isSelected))
            {
                applyTheme(targetTheme);
            }
        };

        drawThemePresetCard("Sombre Moderne", AppTheme::DarkModern, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
        drawThemePresetCard("Clair", AppTheme::Light, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
        drawThemePresetCard("Nord", AppTheme::Nord, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
        drawThemePresetCard("Dracula", AppTheme::Dracula, ImVec4(0.16f, 0.16f, 0.21f, 1.0f));

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
    if (m_showEditTaskModal)
        ImGui::OpenPopup("Éditer les Étapes de la Tâche");

    ImGui::SetNextWindowSize(ImVec2(560.0f, 470.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Éditer les Étapes de la Tâche", &m_showEditTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  Pipeline : %s", ICON_LC_ACTIVITY, m_targetTaskName.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        // Zone d'ajout d'une nouvelle étape depuis les requêtes enregistrées
        ImGui::Text("Ajouter une nouvelle étape :");
        static int s_editTaskQueryIdx = 0;
        static std::array<char, 256> s_editTaskNewStepName{};

        if (m_savedQueries.empty())
        {
            ImGui::TextDisabled("  (Aucune requête enregistrée disponible dans le projet)");
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
            ImGui::InputTextWithHint("##EditTaskStepLabel", "Nom de l'étape...", s_editTaskNewStepName.data(), s_editTaskNewStepName.size());

            ImGui::SameLine();
            if (ImGui::Button((std::string(ICON_LC_PLUS) + " Ajouter une étape").c_str(), ImVec2(145.0f, 0.0f)))
            {
                cppsqlx::UnitSelect newUnit{};
                newUnit.name = (strlen(s_editTaskNewStepName.data()) > 0)
                                   ? std::string(s_editTaskNewStepName.data())
                                   : m_savedQueries[s_editTaskQueryIdx].name;
                // NOTE : Remplacez la ligne ci-dessous par l'initialisation exacte de votre modale m_showCreateTaskModal
                newUnit.query = cppsqlx::SelectQuery::fromJSON(m_savedQueries[s_editTaskQueryIdx].compiledSelectJson).value_or(cppsqlx::SelectQuery());

                m_editTaskUnits.push_back(std::move(newUnit));
                s_editTaskNewStepName.fill('\0');
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Liste ordonnée des étapes existantes
        ImGui::Text("Séquence d'exécution (%d étape(s)) :", static_cast<int>(m_editTaskUnits.size()));
        ImGui::BeginChild("EditTaskStepsZone", ImVec2(-1.0f, 230.0f), true);

        int moveUp = -1, moveDown = -1, removeIdx = -1;
        for (int i = 0; i < static_cast<int>(m_editTaskUnits.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::Text("[Step %d]  %s", i + 1, m_editTaskUnits[i].name.c_str());

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

        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
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
        if (ImGui::Button("Annuler", ImVec2(110.0f, 0.0f)))
        {
            m_showEditTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 2. MODALE DE RENOMMAGE (AVEC MISE À JOUR DU WORKFLOW DESIGNER)
    // =========================================================================
    if (m_showRenameTaskModal)
        ImGui::OpenPopup("Renommer la Tâche");

    ImGui::SetNextWindowSize(ImVec2(400.0f, 150.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Renommer la Tâche", &m_showRenameTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Nouveau nom pour '%s' :", m_targetTaskName.c_str());
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
        if (ImGui::Button("Valider", ImVec2(110.0f, 0.0f)))
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
        if (ImGui::Button("Annuler", ImVec2(110.0f, 0.0f)))
        {
            m_showRenameTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 3. MODALE DE CONFIRMATION DE SUPPRESSION
    // =========================================================================
    if (m_showDeleteTaskModal)
        ImGui::OpenPopup("Supprimer la Tâche");

    ImGui::SetNextWindowSize(ImVec2(420.0f, 155.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Supprimer la Tâche", &m_showDeleteTaskModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped("Supprimer définitivement la tâche '%s' du projet ?", m_targetTaskName.c_str());
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Supprimer", ImVec2(120.0f, 0.0f)))
        {
            std::erase_if(m_savedTasks, [this](const cppsqlx::Task &t)
                          { return t.name() == m_targetTaskName; });
            m_targetTaskName.clear();
            m_showDeleteTaskModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Annuler", ImVec2(110.0f, 0.0f)))
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
    // 1. MODALE D'INSPECTION DU SQL ET DU SCHÉMA DÉDUIT (LIMIT 0)
    // =========================================================================
    if (m_showInspectQueryModal)
        ImGui::OpenPopup("Inspecter la Requête");

    ImGui::SetNextWindowSize(ImVec2(620.0f, 440.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Inspecter la Requête", &m_showInspectQueryModal, ImGuiWindowFlags_NoResize))
    {
        if (m_inspectingQueryIndex >= 0 && m_inspectingQueryIndex < static_cast<int>(m_savedQueries.size()))
        {
            const auto &sq = m_savedQueries[m_inspectingQueryIndex];

            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  %s", ICON_LC_FILE_TEXT, sq.name.c_str());
            if (!sq.description.empty())
                ImGui::TextDisabled("%s", sq.description.c_str());

            ImGui::Separator();
            ImGui::Spacing();

            // Tableau du schéma de sortie (colonnes disponibles en CTE)
            ImGui::Text("Structure de sortie (%d colonne(s)) :", static_cast<int>(sq.outputSchema.size()));
            ImGui::BeginChild("InspectQuerySchemaZone", ImVec2(-1.0f, 140.0f), true);
            if (sq.outputSchema.empty())
            {
                ImGui::TextDisabled("Aucune métadonnée de colonne enregistrée.");
            }
            else if (ImGui::BeginTable("InspectQuerySchemaTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Colonne");
                ImGui::TableSetupColumn("Type PostgreSQL");
                ImGui::TableHeadersRow();
                for (const auto &col : sq.outputSchema)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(col.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.4f, 0.85f), "%s", col.dataType.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Text("Requête SQL générée :");
            std::string sqlBuf = sq.sql;
            ImGui::InputTextMultiline("##InspectQuerySqlText", sqlBuf.data(), sqlBuf.size() + 1,
                                      ImVec2(-1.0f, 150.0f), ImGuiInputTextFlags_ReadOnly);

            ImGui::Spacing();
            if (ImGui::Button((std::string(ICON_LC_COPY) + "  Copier le SQL").c_str(), ImVec2(140.0f, 0.0f)))
            {
                ImGui::SetClipboardText(sq.sql.c_str());
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Fermer", ImVec2(110.0f, 0.0f)))
        {
            m_showInspectQueryModal = false;
            m_inspectingQueryIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    // 2. MODALE DE CONFIRMATION DE SUPPRESSION
    // =========================================================================
    if (m_showDeleteQueryModal)
        ImGui::OpenPopup("Supprimer la Requête");

    ImGui::SetNextWindowSize(ImVec2(430.0f, 155.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Supprimer la Requête", &m_showDeleteQueryModal, ImGuiWindowFlags_NoResize))
    {
        if (m_deletingQueryIndex >= 0 && m_deletingQueryIndex < static_cast<int>(m_savedQueries.size()))
        {
            const std::string &qName = m_savedQueries[m_deletingQueryIndex].name;
            ImGui::TextWrapped("Voulez-vous vraiment supprimer la requête '%s' du projet ?", qName.c_str());
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.22f, 0.22f, 1.0f));
            if (ImGui::Button("Supprimer", ImVec2(120.0f, 0.0f)))
            {
                m_savedQueries.erase(m_savedQueries.begin() + m_deletingQueryIndex);
                m_deletingQueryIndex = -1;
                m_showDeleteQueryModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
        }

        if (ImGui::Button("Annuler", ImVec2(110.0f, 0.0f)))
        {
            m_deletingQueryIndex = -1;
            m_showDeleteQueryModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


