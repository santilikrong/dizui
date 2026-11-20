#include "Application.hpp"
#include "IconsLucide.hpp"
#include <cppsqlx/task.hpp>
#include <cppsqlx/job.hpp>
#include <cppsqlx/workflow.hpp>
#include <cppsqlx/logger.hpp>
#include <imgui_node_editor.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <chrono>

namespace ed = ax::NodeEditor;


void Application::renderWorkflowEditor()
{
    renderWorkflowToolbar();
    ImGui::Separator();
    renderWorkflowCanvas();
    renderWorkflowModals();
}


void Application::renderWorkflowToolbar()
{
    ImGui::BeginChild("WorkflowHeaderBandeau", ImVec2(-1.0f, 45.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::SetCursorPosY((45.0f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextDisabled("%s  WORKFLOW :", ICON_LC_ACTIVITY);

    ImGui::SameLine();
    ImGui::SetCursorPosY((45.0f - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##ActiveWorkflowNameInput", m_activeWorkflowNameBuf, IM_ARRAYSIZE(m_activeWorkflowNameBuf));

    float actions_zone_width = (32.0f * 5) + 32.0f;
    float actions_x = ImGui::GetWindowWidth() - actions_zone_width;
    float target_buttons_y = (45.0f - 28.0f) * 0.5f;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.50f, 0.50f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // BOUTON 0 : Recentrer la vue
    ImGui::SetCursorPosX(actions_x);
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_MAXIMIZE_2, ImVec2(32.0f, 28.0f)))
    {
        ed::SetCurrentEditor(m_workflowContext);
        ed::NavigateToContent();
        ed::SetCurrentEditor(nullptr);
    }
    ImGui::SetItemTooltip("Recentrer la vue sur le workflow");

    // BOUTON 1 : Visualiser le JSON
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_SEARCH, ImVec2(32.0f, 28.0f)))
    {
        m_compiledWorkflowJson = compileWorkflowToJSON(std::string(m_activeWorkflowNameBuf));
        m_showWorkflowJsonModal = true;
    }
    ImGui::SetItemTooltip("Visualiser le JSON généré par ce workflow");

    // BOUTON 2 : Ajouter au Projet
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_PLUS_CIRCLE, ImVec2(32.0f, 28.0f)))
    {
        strncpy(m_saveWorkflowNameBuf, m_activeWorkflowNameBuf, sizeof(m_saveWorkflowNameBuf) - 1);
        strncpy(m_saveWorkflowDescBuf, m_activeWorkflowDescBuf, sizeof(m_saveWorkflowDescBuf) - 1);
        m_showSaveWorkflowModal = true;
    }
    ImGui::SetItemTooltip("Enregistrer ce workflow dans le projet");

    // BOUTON 3 : Effacer le canevas
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_TRASH, ImVec2(32.0f, 28.0f)))
    {
        m_workflowNodes.clear();
        m_workflowLinks.clear();
        std::lock_guard<std::mutex> lock(m_workflowStatusMutex);
        m_liveJobStatuses.clear();
        m_liveJobMessages.clear();
    }
    ImGui::SetItemTooltip("Nettoyer intégralement le canevas de workflow");

    // BOUTON 4 : Exécuter le workflow
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    bool isRunning = m_isWorkflowRunning.load();
    if (isRunning)
        ImGui::BeginDisabled();

    if (ImGui::Button(ICON_LC_PLAY, ImVec2(32.0f, 28.0f)))
    {
        if (!m_workflowNodes.empty())
        {
            m_showRunWorkflowModal = true;
        }
    }
    if (isRunning)
        ImGui::EndDisabled();
    ImGui::SetItemTooltip(isRunning ? "Exécution en cours..." : "Configurer et exécuter ce workflow");

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    ImGui::EndChild();
}


void Application::renderWorkflowCanvas()
{
    ImGui::BeginChild("WorkflowNodeCanvasZone", ImVec2(-1.0f, -1.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

    ed::SetCurrentEditor(m_workflowContext);
    ed::Begin("WorkflowCanvas");

    ed::GetStyle().LinkStrength = 130.0f;
    ed::GetStyle().PinRadius = 5.0f;
    ed::GetStyle().PivotAlignment = ImVec2(0.0f, 0.5f);
    ed::GetStyle().SourceDirection = ImVec2(1.0f, 0.0f);
    ed::GetStyle().TargetDirection = ImVec2(-1.0f, 0.0f);
    ed::GetStyle().PinArrowSize = 0.0f;

    // 1. Gestion du Drag & Drop asynchrone
    static std::string pending_task_name = "";
    static ImVec2 pending_mouse_pos;
    static bool has_pending_drop = false;

    if (has_pending_drop)
    {
        has_pending_drop = false;
        ImVec2 canvas_pos = ed::ScreenToCanvas(pending_mouse_pos);

        uintptr_t maxNodeId = 2000;
        for (const auto &existing : m_workflowNodes)
        {
            if (existing.id >= maxNodeId)
                maxNodeId = existing.id + 1;
        }

        m_workflowNodes.push_back(WorkflowJobNode{
            maxNodeId,
            pending_task_name,
            canvas_pos,
            true,
            maxNodeId * 10,
            (maxNodeId * 10) + 1});
    }

    // 2. Synchronisation thread-safe des statuts d'exécution
    {
        std::lock_guard<std::mutex> lock(m_workflowStatusMutex);
        for (auto &n : m_workflowNodes)
        {
            if (m_liveJobStatuses.contains(n.taskName))
                n.executionStatus = m_liveJobStatuses[n.taskName];
            if (m_liveJobMessages.contains(n.taskName))
                n.statusMessage = m_liveJobMessages[n.taskName];
        }
    }

    // 3. Rendu des nœuds de jobs
    for (auto &node : m_workflowNodes)
    {
        if (node.needsPositionInitialization)
        {
            node.needsPositionInitialization = false;
            ed::SetNodePosition(node.id, node.position);
        }

        ImVec4 nodeBgColor = ImVec4(0.11f, 0.16f, 0.25f, 0.95f);
        ImVec4 borderColor = ImVec4(0.25f, 0.45f, 0.75f, 1.0f);
        ImVec4 statusTextColor = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
        const char *statusTag = "Job (En attente)";

        if (node.executionStatus == VisualJobStatus::Running)
        {
            nodeBgColor = ImVec4(0.22f, 0.12f, 0.32f, 0.95f);
            borderColor = ImVec4(0.65f, 0.30f, 0.95f, 1.0f);
            statusTextColor = ImVec4(0.80f, 0.50f, 1.0f, 1.0f);
            statusTag = "Job (En cours...)";
        }
        else if (node.executionStatus == VisualJobStatus::Success)
        {
            nodeBgColor = ImVec4(0.10f, 0.24f, 0.16f, 0.95f);
            borderColor = ImVec4(0.20f, 0.80f, 0.40f, 1.0f);
            statusTextColor = ImVec4(0.30f, 0.90f, 0.50f, 1.0f);
            statusTag = "Job (OK)";
        }
        else if (node.executionStatus == VisualJobStatus::Failed)
        {
            nodeBgColor = ImVec4(0.28f, 0.10f, 0.12f, 0.95f);
            borderColor = ImVec4(0.90f, 0.25f, 0.25f, 1.0f);
            statusTextColor = ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
            statusTag = "Job (KO)";
        }

        ed::PushStyleColor(ed::StyleColor_NodeBg, nodeBgColor);
        ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 10.0f, 0.0f, 10.0f));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);

        ed::BeginNode(node.id);
        ImGui::BeginGroup();

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
        ImGui::TextColored(statusTextColor, "%s  %s", ICON_LC_ACTIVITY, statusTag);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.85f), "%s", node.taskName.c_str());

        ImGui::Spacing();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("##WorkflowPinsTable", 2, ImGuiTableFlags_None, ImVec2(160.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("InCol", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn("OutCol", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 4.0f);
            ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(-1.0f, 0.0f));
            ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
            ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 6.0f);

            ed::BeginPin(node.inputPinId, ed::PinKind::Input);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(8.0f, 8.0f));
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + 4.0f, pos.y + 4.0f), 3.5f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.65f, 0.95f, 0.9f)));
            ed::EndPin();
            ed::PopStyleVar(3);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4.0f);
            ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(1.0f, 0.0f));
            ed::PushStyleVar(ed::StyleVar_PinArrowSize, 0.0f);

            ed::BeginPin(node.outputPinId, ed::PinKind::Output);
            ImVec2 pos_out = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(8.0f, 8.0f));
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos_out.x + 4.0f, pos_out.y + 4.0f), 3.5f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.65f, 0.95f, 0.9f)));
            ed::EndPin();
            ed::PopStyleVar(2);

            ImGui::EndTable();
        }

        ImGui::EndGroup();
        ed::EndNode();

        ed::PopStyleVar(2);
        ed::PopStyleColor(2);

        if (ImGui::IsItemHovered() && !node.statusMessage.empty())
        {
            ImGui::SetItemTooltip("Statut : %s", node.statusMessage.c_str());
        }
    }

    // 4. Rendu des liens de dépendances
    for (const auto &link : m_workflowLinks)
    {
        ed::Link(link.id, link.startPinId, link.endPinId, ImVec4(0.90f, 0.90f, 0.90f, 0.85f), 2.0f);
    }

    // 5. Création et suppression de liens / nœuds
    if (ed::BeginCreate())
    {
        ed::PinId startPin, endPin;
        if (ed::QueryNewLink(&startPin, &endPin))
        {
            if (ed::AcceptNewItem(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), 2.5f))
            {
                uintptr_t maxLinkId = 5000;
                for (const auto &existingLink : m_workflowLinks)
                {
                    if (existingLink.id >= maxLinkId)
                        maxLinkId = existingLink.id + 1;
                }
                m_workflowLinks.push_back(WorkflowJobLink{maxLinkId, startPin.Get(), endPin.Get()});
            }
        }
    }
    ed::EndCreate();
    if (ed::BeginDelete())
    {
        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                std::erase_if(m_workflowNodes, [deletedNodeId](const WorkflowJobNode &n)
                              { return n.id == deletedNodeId.Get(); });
                std::erase_if(m_workflowLinks, [deletedNodeId](const WorkflowJobLink &l)
                              { return (l.startPinId / 10 == deletedNodeId.Get()) || (l.endPinId / 10 == deletedNodeId.Get()); });
            }
        }
    }
    ed::EndDelete();
    if (m_needsWorkflowCentering && !m_workflowNodes.empty())
    {
        ed::NavigateToContent(0.0f);
        m_needsWorkflowCentering = false;
    }
    ed::End();
    ed::SetCurrentEditor(nullptr);
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_WORKFLOW_TASK_NODE"))
        {
            pending_task_name = (const char *)payload->Data;
            pending_mouse_pos = ImGui::GetMousePos();
            has_pending_drop = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::EndChild();
}


void Application::renderWorkflowModals()
{
    // MODALE 1 : APERÇU DU JSON DU WORKFLOW
    if (m_showWorkflowJsonModal)
        ImGui::OpenPopup("Aperçu JSON du Workflow");
    ImGui::SetNextWindowSize(ImVec2(800.0f, 550.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(450.0f, 250.0f), ImVec2(1600.0f, 900.0f));
    if (ImGui::BeginPopupModal("Aperçu JSON du Workflow", &m_showWorkflowJsonModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Structure JSON d'orchestration", ICON_LC_BOX);
        ImGui::Separator();
        ImGui::Spacing();
        float text_area_height = ImGui::GetContentRegionAvail().y - 45.0f;
        ImGui::InputTextMultiline("##WorkflowJsonPreviewArea",
                                  const_cast<char *>(m_compiledWorkflowJson.c_str()), m_compiledWorkflowJson.length() + 1,
                                  ImVec2(-1.0f, text_area_height), ImGuiInputTextFlags_ReadOnly);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - 120.0f);
        if (ImGui::Button("Fermer", ImVec2(120.0f, 0.0f)))
        {
            m_showWorkflowJsonModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // MODALE 2 : ENREGISTREMENT DU WORKFLOW DANS LE PROJET
    if (m_showSaveWorkflowModal)
        ImGui::OpenPopup("Enregistrer le Workflow dans le Projet");
    ImGui::SetNextWindowSize(ImVec2(460.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Enregistrer le Workflow dans le Projet", &m_showSaveWorkflowModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Ajout au catalogue des workflows", ICON_LC_FOLDER_OPEN);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Nom du Workflow :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##WorkflowNameInput", m_saveWorkflowNameBuf, IM_ARRAYSIZE(m_saveWorkflowNameBuf));
        ImGui::Spacing();
        ImGui::Text("Description :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##WorkflowDescInput", m_saveWorkflowDescBuf, IM_ARRAYSIZE(m_saveWorkflowDescBuf), ImVec2(-1.0f, 60.0f));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showSaveWorkflowModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
        {
            if (strlen(m_saveWorkflowNameBuf) > 0)
            {
                strncpy(m_activeWorkflowNameBuf, m_saveWorkflowNameBuf, sizeof(m_activeWorkflowNameBuf) - 1);
                m_activeWorkflowNameBuf[sizeof(m_activeWorkflowNameBuf) - 1] = '\0';
                strncpy(m_activeWorkflowDescBuf, m_saveWorkflowDescBuf, sizeof(m_activeWorkflowDescBuf) - 1);
                m_activeWorkflowDescBuf[sizeof(m_activeWorkflowDescBuf) - 1] = '\0';
                std::string wfName(m_saveWorkflowNameBuf);
                std::string wfDesc(m_saveWorkflowDescBuf);
                std::string wfJson = compileWorkflowToJSON(wfName);
                nlohmann::json wfVisualState;
                wfVisualState["nodes"] = nlohmann::json::array();
                ed::SetCurrentEditor(m_workflowContext);
                for (const auto &n : m_workflowNodes)
                {
                    ImVec2 pos = ed::GetNodePosition(n.id);
                    wfVisualState["nodes"].push_back({{"id", n.id},
                                                      {"taskName", n.taskName},
                                                      {"x", pos.x},
                                                      {"y", pos.y},
                                                      {"inputPinId", n.inputPinId},
                                                      {"outputPinId", n.outputPinId}});
                }
                ed::SetCurrentEditor(nullptr);
                wfVisualState["links"] = nlohmann::json::array();
                for (const auto &l : m_workflowLinks)
                {
                    wfVisualState["links"].push_back({{"id", l.id},
                                                      {"startPinId", l.startPinId},
                                                      {"endPinId", l.endPinId}});
                }
                SavedWorkflow updatedWf{wfName, wfDesc, wfJson, wfVisualState};
                auto it = std::find_if(m_savedWorkflows.begin(), m_savedWorkflows.end(),
                                       [&wfName](const SavedWorkflow &w)
                                       { return w.name == wfName; });
                if (it != m_savedWorkflows.end())
                    *it = std::move(updatedWf);
                else
                    m_savedWorkflows.push_back(std::move(updatedWf));
                m_showSaveWorkflowModal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    // MODALE 3 : PARAMÈTRES D'EXÉCUTION DU WORKFLOW
    if (m_showRunWorkflowModal)
        ImGui::OpenPopup("Exécuter le Workflow");
    ImGui::SetNextWindowSize(ImVec2(450.0f, 230.0f), ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Exécuter le Workflow", &m_showRunWorkflowModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.45f, 1.0f), "%s  Configuration du contexte d'exécution", ICON_LC_PLAY);
        ImGui::Separator();
        ImGui::Spacing();

        std::vector<std::string> gatewayNames;

        if (m_activeProject)
        {
            for (const auto &[gwName, gwObj] : m_activeProject->logicalConnections())
            {
                if (!gwName.empty())
                    gatewayNames.push_back(gwName);
            }
        }
        ImGui::Text("Connexion logique (Gateway) :");
        ImGui::SetNextItemWidth(-1.0f);
        if (gatewayNames.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Aucune connexion logique disponible dans le projet.");
        }
        else
        {
            if (m_selectedRunGatewayIndex >= static_cast<int>(gatewayNames.size()))

                m_selectedRunGatewayIndex = 0;
            if (ImGui::BeginCombo("##RunGatewayCombo", gatewayNames[m_selectedRunGatewayIndex].c_str()))
            {
                for (int i = 0; i < static_cast<int>(gatewayNames.size()); ++i)
                {
                    if (ImGui::Selectable(gatewayNames[i].c_str(), m_selectedRunGatewayIndex == i))
                        m_selectedRunGatewayIndex = i;
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Spacing();
        const char *envContexts[] = {"DEV", "TEST", "PROD"};
        ImGui::Text("Environnement cible :");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##RunEnvContextCombo", envContexts[m_selectedRunContextIndex]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ImGui::Selectable(envContexts[i], m_selectedRunContextIndex == i))
                    m_selectedRunContextIndex = i;
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showRunWorkflowModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        bool canLaunch = !gatewayNames.empty();
        if (!canLaunch)
            ImGui::BeginDisabled();
        if (ImGui::Button("Lancer", ImVec2(120.0f, 0.0f)))
        {
            launchWorkflowExecutionAsync(
                gatewayNames[m_selectedRunGatewayIndex],
                envContexts[m_selectedRunContextIndex]);
            m_showRunWorkflowModal = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canLaunch)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}


std::string Application::compileWorkflowToJSON(const std::string &workflowName)
{
    // Priorité au paramètre passé, sinon lecture du champ actif de l'interface
    std::string effectiveName = workflowName.empty() ? std::string(m_activeWorkflowNameBuf) : workflowName;
    if (effectiveName.empty())
        effectiveName = "Workflow_Sans_Nom";

    cppsqlx::Workflow workflow(effectiveName);

    cppsqlx::ConnectionContext ctx;
    ctx.name_ = effectiveName;
    ctx.description_ = std::string(m_activeWorkflowDescBuf);
    workflow.setContext(std::move(ctx));

    std::map<uintptr_t, std::shared_ptr<cppsqlx::Job>> nodeIdToJobMap;

    // 1. Création des Jobs avec leurs coordonnées et leur Task associée
    for (const auto &node : m_workflowNodes)
    {
        auto job = std::make_shared<cppsqlx::Job>(node.taskName);

        ed::SetCurrentEditor(m_workflowContext);
        ImVec2 pos = ed::GetNodePosition(node.id);
        ed::SetCurrentEditor(nullptr);

        job->setX(static_cast<int>(pos.x));
        job->setY(static_cast<int>(pos.y));

        for (const auto &savedTask : m_savedTasks)
        {
            if (savedTask.name() == node.taskName)
            {
                job->addTask(1, std::make_shared<cppsqlx::Task>(savedTask));
                break;
            }
        }

        nodeIdToJobMap[node.id] = job;
        workflow.addJob(job);
    }

    // 2. Conversion des câbles en Conditions de dépendance
    for (const auto &link : m_workflowLinks)
    {
        uintptr_t srcNodeId = link.startPinId / 10;
        uintptr_t dstNodeId = link.endPinId / 10;

        if (nodeIdToJobMap.contains(srcNodeId) && nodeIdToJobMap.contains(dstNodeId))
        {
            auto parentJob = nodeIdToJobMap[srcNodeId];
            auto childJob = nodeIdToJobMap[dstNodeId];

            cppsqlx::Condition depCondition(parentJob->name(), cppsqlx::JobStatus::Success);
            childJob->addCondition(std::move(depCondition));
        }
    }

    return workflow.toJSON().dump(4);
}


void Application::launchWorkflowExecutionAsync(const std::string &logicalConnName, const std::string &envContext)
{
    if (m_isWorkflowRunning.load() || !m_activeProject)
        return;

    // 1. Récupération et assainissement de la passerelle logique
    auto &gateways = m_activeProject->logicalConnections();
    auto it = gateways.find(logicalConnName);
    if (it == gateways.end())
    {
        std::cerr << "[dizui Erreur] Passerelle logique introuvable : '" << logicalConnName << "'" << std::endl;
        return;
    }

    if (it->second.name().empty())
    {
        it->second.setName(logicalConnName);
    }

    // 2. Vérification de la présence d'une configuration physique pour l'environnement cible (DEV/TEST/PROD)
    auto configCheck = it->second.getConfiguration(envContext);
    if (!configCheck.has_value())
    {
        std::cerr << "[dizui Erreur] Impossible de lancer le workflow : " << configCheck.error() << std::endl;
        return;
    }

    // 3. Compilation et validation du Workflow
    std::string wfName(m_activeWorkflowNameBuf);
    std::string wfJsonStr = compileWorkflowToJSON(wfName);
    auto wfRes = cppsqlx::Workflow::fromJSON(nlohmann::json::parse(wfJsonStr));

    if (!wfRes.has_value())
    {
        std::cerr << "[dizui Erreur] Impossible d'initialiser le Workflow : " << wfRes.error() << std::endl;
        return;
    }

    // 1. Initialisation de tous les jobs en statut "En attente" (Bleu foncé par défaut)
    {
        std::lock_guard<std::mutex> lock(m_workflowStatusMutex);
        m_liveJobStatuses.clear();
        m_liveJobMessages.clear();
        for (const auto &node : m_workflowNodes)
        {
            m_liveJobStatuses[node.taskName] = VisualJobStatus::Idle;
            m_liveJobMessages[node.taskName] = "En attente d'exécution";
        }
    }

    m_isWorkflowRunning.store(true);
    auto workflowPtr = std::make_shared<cppsqlx::Workflow>(std::move(wfRes.value()));

    m_activeProject->resolveDependencies();
    cppsqlx::LogicalConnection targetGateway = m_activeProject->logicalConnections().at(logicalConnName);

    std::thread([this, workflowPtr, targetGateway, envContext]()
                {
        try
        {
            // Passage du callback temps réel à Workflow::run
            workflowPtr->run(targetGateway, envContext, [this](const std::string& jobName, cppsqlx::JobStatus status, const std::string& msg) {
                std::lock_guard<std::mutex> lock(m_workflowStatusMutex);
                m_liveJobMessages[jobName] = msg;

                if (status == cppsqlx::JobStatus::Running)
                    m_liveJobStatuses[jobName] = VisualJobStatus::Running;
                else if (status == cppsqlx::JobStatus::Success)
                    m_liveJobStatuses[jobName] = VisualJobStatus::Success;
                else if (status == cppsqlx::JobStatus::Failed)
                    m_liveJobStatuses[jobName] = VisualJobStatus::Failed;
                else
                    m_liveJobStatuses[jobName] = VisualJobStatus::Idle;
            });
        }
        catch (const std::exception& e)
        {
            std::cerr << "[dizui Workflow Erreur] Exception critique : " << e.what() << std::endl;
        }

        m_isWorkflowRunning.store(false); })
        .detach();
}


void Application::loadWorkflowIntoVisualDesigner(const SavedWorkflow &savedWorkflow)
{
    m_workflowNodes.clear();
    m_workflowLinks.clear();

    strncpy(m_activeWorkflowNameBuf, savedWorkflow.name.c_str(), sizeof(m_activeWorkflowNameBuf) - 1);
    m_activeWorkflowNameBuf[sizeof(m_activeWorkflowNameBuf) - 1] = '\0';

    strncpy(m_activeWorkflowDescBuf, savedWorkflow.description.c_str(), sizeof(m_activeWorkflowDescBuf) - 1);
    m_activeWorkflowDescBuf[sizeof(m_activeWorkflowDescBuf) - 1] = '\0';

    if (!savedWorkflow.visualState.is_null() && !savedWorkflow.visualState.empty())
    {
        const auto &vs = savedWorkflow.visualState;
        if (vs.contains("nodes") && vs["nodes"].is_array())
        {
            for (const auto &nJson : vs["nodes"])
            {
                m_workflowNodes.push_back(WorkflowJobNode{
                    nJson.value("id", uintptr_t(0)),
                    nJson.value("taskName", ""),
                    ImVec2(nJson.value("x", 100.0f), nJson.value("y", 100.0f)),
                    true,
                    nJson.value("inputPinId", uintptr_t(0)),
                    nJson.value("outputPinId", uintptr_t(0))});
            }
        }

        if (vs.contains("links") && vs["links"].is_array())
        {
            for (const auto &lJson : vs["links"])
            {
                m_workflowLinks.push_back(WorkflowJobLink{
                    lJson.value("id", uintptr_t(0)),
                    lJson.value("startPinId", uintptr_t(0)),
                    lJson.value("endPinId", uintptr_t(0))});
            }
        }
    }

    m_needsWorkflowCentering = true;
}


void Application::addConsoleLog(const std::string &msg, LogLevel level)
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &now_c);
#else
    localtime_r(&now_c, &tm_now);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%H:%M:%S");

    std::lock_guard<std::mutex> lock(m_consoleMutex);
    m_consoleLogs.push_back(ConsoleLogEntry{oss.str(), level, msg});

    // Conservation des 1000 dernières lignes pour garder une interface fluide
    if (m_consoleLogs.size() > 1000)
    {
        m_consoleLogs.pop_front();
    }
}


void Application::renderConsolePanel()
{
    if (!m_showConsolePanel)
        return;

    if (m_consoleDockId != 0)
    {
        ImGui::SetNextWindowDockID(m_consoleDockId, ImGuiCond_FirstUseEver);
    }

    ImGui::SetNextWindowSize(ImVec2(800.0f, 180.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Console & Journaux d'Exécution", &m_showConsolePanel))
    {
        // Mémorise en continu le DockID du panneau inférieur tant qu'il est docké
        ImGuiID currentDockId = ImGui::GetWindowDockID();
        if (currentDockId != 0)
        {
            m_consoleDockId = currentDockId;
        }

        // Barre d'outils supérieure de la console
        if (ImGui::SmallButton((std::string(ICON_LC_TRASH) + "  Effacer").c_str()))
        {
            std::lock_guard<std::mutex> lock(m_consoleMutex);
            m_consoleLogs.clear();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Défilement auto", &m_consoleAutoScroll);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##ConsoleFilter", "Filtrer les logs...", m_consoleFilterBuf, IM_ARRAYSIZE(m_consoleFilterBuf));

        ImGui::Separator();

        // Zone défilante des journaux
        ImGui::BeginChild("ConsoleScrollingRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(m_consoleMutex);
            std::string filterStr(m_consoleFilterBuf);

            for (const auto &entry : m_consoleLogs)
            {
                if (!filterStr.empty() && entry.message.find(filterStr) == std::string::npos)
                    continue;

                ImVec4 textColor = ImVec4(0.88f, 0.90f, 0.94f, 1.0f); // Info (Blanc cassé)
                if (entry.level == LogLevel::Success)
                    textColor = ImVec4(0.35f, 0.85f, 0.45f, 1.0f); // Succès (Vert)
                else if (entry.level == LogLevel::Warning)
                    textColor = ImVec4(0.95f, 0.75f, 0.25f, 1.0f); // Avertissement (Jaune/Ambre)
                else if (entry.level == LogLevel::Error)
                    textColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f); // Erreur (Rouge)

                ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }

            if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}



