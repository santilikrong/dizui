#include "Application.hpp"
#include "IconsLucide.hpp"
#include <cppsqlx/sqlliterals.hpp>
#include <cppsqlx/sqljoinexpressionbuilder.hpp>
#include <imgui_node_editor.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <GLFW/glfw3.h>

namespace ed = ax::NodeEditor;
using namespace cppsqlx::literals;


void Application::renderQueryEditor()
{
    float total_available_height = ImGui::GetContentRegionAvail().y - 25.0f;
    if (m_queryVisualHeight <= 10.0f || m_queryVisualHeight > total_available_height)
    {
        m_queryVisualHeight = total_available_height * 0.333f;
    }
    float canvas_height = total_available_height - m_queryVisualHeight;
    float minimum_zone_height = 100.0f;

    // ---------------------------------------------------------------------
    // BARRE DE COMMANDE HAUTE : ALIGNEMENT COMPACT À 4 BOUTONS ACTIONS
    // ---------------------------------------------------------------------
    ImGui::BeginChild("QueryEditorHeaderBandeau", ImVec2(-1.0f, 45.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosY((45.0f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextDisabled("CONCEPTEUR GRAPHIQUE DE REQUÊTE");

    // RECALIBRAGE GÉOMÉTRIQUE : 5 boutons carrés de 32px + 4 intervalles de 8px
    float available_width = ImGui::GetContentRegionAvail().x;
    float actions_zone_width = (32.0f * 5) + 32.0f;
    float actions_x = available_width - actions_zone_width;
    float target_buttons_y = (45.0f - 28.0f) * 0.5f;

    // Configuration de la pile de style commune
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.50f, 0.50f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // BOUTON 0 : Recentrer la caméra sur l'ensemble des nœuds
    ImGui::SetCursorPosX(actions_x);
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_MAXIMIZE_2, ImVec2(32.0f, 28.0f)))
    {
        ed::SetCurrentEditor(m_queryContext);
        ed::NavigateToContent();
        ed::SetCurrentEditor(nullptr);
    }
    ImGui::SetItemTooltip("Recentrer la vue sur le graphe (Zoom / Pan)");

    // BOUTON 1 : Recherche / Afficher SQL textuel (enchaîné avec SameLine au lieu de SetCursorPosX)
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_SEARCH, ImVec2(32.0f, 28.0f)))
    {
        m_compiledSQLPreview = compileVisualGraphToSQL();
        m_showSQLPreviewModal = true;
    }
    ImGui::SetItemTooltip("Visualiser le code SQL textuel généré");

    // BOUTON 2 : Ajouter au Projet (Plus)
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_PLUS_CIRCLE, ImVec2(32.0f, 28.0f)))
    {
        m_showSaveQueryModal = true;
    }
    ImGui::SetItemTooltip("Enregistrer cette requête dans le catalogue du projet");

    // BOUTON 3 : Nettoyer le Canevas (Nouvelle Icône Poubelle / Balai)
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);

    // Vous pouvez utiliser ICON_LC_TRASH ou ICON_LC_ERASER selon vos icônes chargées
    if (ImGui::Button(ICON_LC_TRASH, ImVec2(32.0f, 28.0f)))
    {
        // NETTOYAGE INTÉGRAL SÉCURISÉ DES STRUCTURES DE DONNÉES
        m_visualQueryNodes.clear();
        m_visualQueryLinks.clear();
        m_visualQueryPredicates.clear();

        // Réinitialisation complète du tableau bas de l'UI
        m_queryResultHeaders.clear();
        m_queryResultRows.clear();
        m_executionError = "";

        // Vidage des métadonnées de la requête en cours d'édition
        memset(m_saveQueryNameBuf, 0, sizeof(m_saveQueryNameBuf));
        memset(m_saveQueryDescBuf, 0, sizeof(m_saveQueryDescBuf));

        m_projectionItems.clear();
        m_hasProjectionNode = false;
    }
    ImGui::SetItemTooltip("Effacer intégralement le canevas et réinitialiser l'espace");

    // BOUTON 4 : Exécution réelle Play
    ImGui::SameLine();
    ImGui::SetCursorPosY(target_buttons_y);
    if (ImGui::Button(ICON_LC_PLAY, ImVec2(32.0f, 28.0f)))
    {
        std::string sql_code = compileVisualGraphToSQL();
        executeVisualQuery(sql_code);
    }
    ImGui::SetItemTooltip("Exécuter la requête (Interroger PostgreSQL)");

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    ImGui::EndChild();
    ImGui::Separator();

    // Prend 100 % de la hauteur disponible restante
    // Réaffectation directe de la variable existante pour occuper 100 % de la hauteur
    canvas_height = ImGui::GetContentRegionAvail().y;
    if (canvas_height < 100.0f)
        canvas_height = 100.0f;

    renderQueryCanvas(canvas_height);

    // [ Le reste de renderQueryEditor reste parfaitement identique ]
    // renderQueryCanvas(canvas_height);

    // 3. Zone Intermédiaire : Le Splitter Horizontal Rédimensionnable
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.75f, 1.0f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.75f, 1.0f, 1.0f));
    ImGui::Button("##VisualSplitterBar", ImVec2(-1.0f, 4.0f));
    if (ImGui::IsItemActive())
    {
        m_queryVisualHeight -= ImGui::GetIO().MouseDelta.y;
        m_queryVisualHeight = ImClamp(m_queryVisualHeight, minimum_zone_height, total_available_height - minimum_zone_height);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    // 4. Zone B : La table basse de résultats PostgreSQL
    /*
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::TextDisabled("%s  RÉSULTAT DU GRAPHE VISUEL", ICON_LC_ACTIVITY);
    ImGui::Separator();

    float bottom_zone_height = ImGui::GetContentRegionAvail().y - 10.0f;
    renderQueryResultTable(bottom_zone_height);

    ImGui::PopStyleVar(1);
    */

    // 5. Zone C : Les Popups modaux d'UI (Jointures, Filtres WHERE)
    renderQueryModals();

    // =========================================================================
    // MODALE DE PRÉVISUALISATION DU CODE SQL TEXTUEL
    // =========================================================================
    if (m_showSQLPreviewModal)
    {
        ImGui::OpenPopup("Aperçu de la Requête Compilée");
    }

    ImGui::SetNextWindowSize(ImVec2(750.0f, 400.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 250.0f), ImVec2(1600.0f, 900.0f));

    if (ImGui::BeginPopupModal("Aperçu de la Requête Compilée", &m_showSQLPreviewModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Code SQL natif généré via cppsqlx", ICON_LC_BOX);
        ImGui::Separator();
        ImGui::Spacing();

        if (m_compiledSQLPreview.empty())
        {
            ImGui::TextDisabled("Le graphe visuel actuel est vide ou n'a généré aucune instruction valide.");
        }
        else
        {
            float text_area_height = ImGui::GetContentRegionAvail().y - 45.0f;
            ImGui::InputTextMultiline("##SQLCodePreviewArea",
                                      const_cast<char *>(m_compiledSQLPreview.c_str()), m_compiledSQLPreview.length(),
                                      ImVec2(-1.0f, text_area_height), ImGuiInputTextFlags_ReadOnly);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - 120.0f);
        if (ImGui::Button("Fermer", ImVec2(120.0f, 0.0f)))
        {
            m_showSQLPreviewModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // =========================================================================
    //  ENREGISTREMENT DE LA QUERY DANS LE PROJET
    // =========================================================================
    if (m_showSaveQueryModal)
    {
        ImGui::OpenPopup("Enregistrer la requête dans le Projet");
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 240.0f), ImGuiCond_Once);

    if (ImGui::BeginPopupModal("Enregistrer la requête dans le Projet", &m_showSaveQueryModal, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Ajout au catalogue des requêtes", ICON_LC_FOLDER_OPEN);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Nom de la requête :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##QueryNameInput", m_saveQueryNameBuf, IM_ARRAYSIZE(m_saveQueryNameBuf));

        ImGui::Spacing();

        ImGui::Text("Description / Métadonnées :");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##QueryDescInput", m_saveQueryDescBuf, IM_ARRAYSIZE(m_saveQueryDescBuf), ImVec2(-1.0f, 60.0f));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);

        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showSaveQueryModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool canSaveQuery = (strlen(m_saveQueryNameBuf) > 0);
        if (!canSaveQuery)
            ImGui::BeginDisabled();

        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
        {
            m_executionError.clear();

            std::string qName(m_saveQueryNameBuf);
            std::string qDesc(m_saveQueryDescBuf);

            cppsqlx::SelectQuery compiledSelect = compileVisualGraphToSelectQuery();
            std::string compiledSql = compiledSelect.toSQL();
            nlohmann::json compiledJson = compiledSelect.toJSON();

            // 1. Déduction de la structure de sortie (Introspection PostgreSQL LIMIT 0 + repli local)
            std::vector<cppsqlx::ColumnMetadata> deducedSchema;
            bool introspectedFromDb = false;

            if (m_activeProject && m_activeProject->logicalConnections().contains(m_selectedGateway))
            {
                const auto &gw = m_activeProject->logicalConnections().at(m_selectedGateway);
                if (gw.environments().contains(m_selectedContext))
                {
                    const auto &phys = gw.environments().at(m_selectedContext);
                    auto probeConn = std::make_shared<cppsqlx::Connection>(phys.toConnectionString());
                    if (probeConn->connect().has_value())
                    {
                        cppsqlx::DatabaseMetadata meta(probeConn);
                        auto structRes = meta.getQueryStructure(compiledSql);
                        if (structRes.has_value())
                        {
                            deducedSchema = std::move(structRes.value());
                            introspectedFromDb = true;
                        }
                        else
                        {
                            m_executionError = structRes.error();
                        }
                    }
                }
            }

            if (!introspectedFromDb)
            {
                for (const auto &proj : m_projectionItems)
                {
                    cppsqlx::ColumnMetadata cm;
                    cm.name = proj.alias.empty() ? proj.sqlExpression : proj.alias;
                    cm.dataType = proj.isCustomExpression ? "expr" : "any";
                    cm.isNullable = true;
                    cm.isPrimaryKey = false;
                    deducedSchema.push_back(std::move(cm));
                }
            }

            // 2. Capture complète du graphe visuel (Nœuds, Liens de jointure, Filtres WHERE, Projections)
            nlohmann::json vState;
            ed::SetCurrentEditor(m_queryContext);

            // 2a. Nœuds (Tables et CTE)
            vState["nodes"] = nlohmann::json::array();
            for (const auto &n : m_visualQueryNodes)
            {
                ImVec2 pos = ed::GetNodePosition(n.id);
                vState["nodes"].push_back({{"id", n.id},
                                           {"datasetName", n.datasetName},
                                           {"x", pos.x},
                                           {"y", pos.y},
                                           {"alias", n.alias},
                                           {"selectedColumns", n.selectedColumns},
                                           {"isCTE", n.isCTE}});
            }

            // 2b. Liens et opérateurs de jointure (JOIN)
            vState["links"] = nlohmann::json::array();
            for (const auto &l : m_visualQueryLinks)
            {
                ImVec2 joinPos = ed::GetNodePosition(l.centralNodeId);
                vState["links"].push_back({{"id", l.id},
                                           {"startPinId", l.startPinId},
                                           {"endPinId", l.endPinId},
                                           {"centralNodeId", l.centralNodeId},
                                           {"leftPinId", l.leftPinId},
                                           {"rightPinId", l.rightPinId},
                                           {"type", static_cast<int>(l.type)},
                                           {"leftFieldName", l.leftFieldName},
                                           {"rightFieldName", l.rightFieldName},
                                           {"op", static_cast<int>(l.op)},
                                           {"joinX", joinPos.x},
                                           {"joinY", joinPos.y},
                                           {"isSetOperation", l.isSetOperation},
                                           {"setOp", static_cast<int>(l.setOp)}});
            }

            // 2c. Filtres (WHERE)
            vState["predicates"] = nlohmann::json::array();
            for (const auto &p : m_visualQueryPredicates)
            {
                ImVec2 pPos = ed::GetNodePosition(p.id);
                vState["predicates"].push_back({{"id", p.id},
                                                {"x", pPos.x},
                                                {"y", pPos.y},
                                                {"inputPinId", p.inputPinId},
                                                {"outputPinId", p.outputPinId},
                                                {"targetColumn", p.targetColumn},
                                                {"op", static_cast<int>(p.op)},
                                                {"value", p.value},
                                                {"connectedSrcPinId", p.connectedSrcPinId},
                                                {"isCustomExpression", p.isCustomExpression},
                                                {"customSqlCondition", p.customSqlCondition}});
            }

            // 2d. Projection unifiée (SELECT / Expressions)
            vState["projections"] = nlohmann::json::array();
            for (const auto &proj : m_projectionItems)
            {
                vState["projections"].push_back({{"id", proj.id},
                                                 {"alias", proj.alias},
                                                 {"sqlExpression", proj.sqlExpression},
                                                 {"isAggregate", proj.isAggregate},
                                                 {"isCustomExpression", proj.isCustomExpression},
                                                 {"sourceNodeId", proj.sourceNodeId}});
            }

            vState["hasProjectionNode"] = m_hasProjectionNode;
            if (m_hasProjectionNode)
            {
                ImVec2 projPos = ed::GetNodePosition(99000);
                vState["projectionX"] = projPos.x;
                vState["projectionY"] = projPos.y;
            }

            ed::SetCurrentEditor(nullptr);

            // 3. Ajout ou mise à jour dans m_savedQueries
            SavedQuery updatedQuery{
                qName,
                qDesc,
                compiledSql,
                std::move(vState),
                std::move(compiledJson),
                std::move(deducedSchema)};

            auto it = std::find_if(m_savedQueries.begin(), m_savedQueries.end(),
                                   [&qName](const SavedQuery &q)
                                   { return q.name == qName; });
            if (it != m_savedQueries.end())
                *it = std::move(updatedQuery);
            else
                m_savedQueries.push_back(std::move(updatedQuery));

            m_showSaveQueryModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canSaveQuery)
            ImGui::EndDisabled();

        ImGui::EndPopup();
    }
}


void Application::renderQueryCanvas(float canvas_height)
{
    ImGui::BeginChild("QueryNodeCanvasZone", ImVec2(-1.0f, canvas_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

    // 1. Passe A : Intercepteur de Drag & Drop de nœuds depuis la sidebar
    handleCanvasInteraction();

    // 2. Initialisation et ouverture de la grille graphique
    ed::SetCurrentEditor(m_queryContext);
    ed::Begin("QueryCanvas");

    // Passe B : Rendu polymorphe des différents composants graphiques
    renderCanvasDatasetNodes();
    renderCanvasFilterNodes();
    renderCanvasJoinNodes();
    renderCanvasProjectionNode();

    // Passe C : Intercepteurs d'actions de l'éditeur (Création de liens, Suppressions)
    handleCanvasActions();

    if (m_needsQueryCentering && !m_visualQueryNodes.empty())
    {
        ed::NavigateToContent(0.0f); // 0.0f = cadrage instantané sans animation lente
        m_needsQueryCentering = false;
    }

    // Fermeture hermétique et étanche du canevas
    ed::End();
    ed::SetCurrentEditor(nullptr);

    if (!m_pendingCtePreviewName.empty())
    {
        std::string target = m_pendingCtePreviewName;
        m_pendingCtePreviewName.clear();
        openCtePreviewModal(target);
    }

    ImGui::EndChild();
}


void Application::renderQueryResultTable(float bottom_zone_height)
{
    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    ImGui::BeginChild("VisualResultTableZone", ImVec2(-1.0f, bottom_zone_height), false);

    if (!m_executionError.empty())
    {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s %s", ICON_LC_ALERT_CIRCLE, m_executionError.c_str());
    }
    else if (m_queryResultRows.empty())
    {
        ImGui::TextDisabled("Aucun résultat d'exécution disponible. Cliquez sur Play pour lancer la requête.");
    }
    else if (ImGui::BeginTable("VisualSQLResultTable", (int)m_queryResultHeaders.size(), table_flags))
    {
        for (const auto &header : m_queryResultHeaders)
        {
            ImGui::TableSetupColumn(header.c_str());
        }
        ImGui::TableHeadersRow();

        for (const auto &rowData : m_queryResultRows)
        {
            ImGui::TableNextRow();
            for (int col = 0; col < (int)rowData.size(); ++col)
            {
                ImGui::TableSetColumnIndex(col);
                ImGui::Text("%s", rowData[col].c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}


void Application::renderQueryResultsPanel()
{
    if (!m_showQueryResultsPanel)
        return;

    // Rattache automatiquement l'onglet au même dock que la console
    if (ImGuiWindow *consoleWin = ImGui::FindWindowByName("Console & Journaux d'Exécution"))
    {
        if (consoleWin->DockId != 0)
        {
            ImGui::SetNextWindowDockID(consoleWin->DockId, ImGuiCond_FirstUseEver);
        }
    }

    if (m_focusQueryResultsTab)
    {
        ImGui::SetNextWindowFocus();
        m_focusQueryResultsTab = false;
    }

    ImGui::SetNextWindowSize(ImVec2(800.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Résultat du Graphe Visuel", nullptr))
    {
        if (!m_executionError.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s  Erreur SQL : %s", ICON_LC_ALERT_TRIANGLE, m_executionError.c_str());
        }
        else if (m_queryResultHeaders.empty() || m_queryResultRows.empty())
        {
            ImGui::TextDisabled("Aucun résultat d'exécution disponible. Cliquez sur Play pour lancer la requête.");
        }
        else
        {
            ImGui::TextDisabled("%d ligne(s) retournée(s)", static_cast<int>(m_queryResultRows.size()));
            ImGui::Separator();

            ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

            if (ImGui::BeginTable("VisualQueryResultsTable", static_cast<int>(m_queryResultHeaders.size()), tableFlags))
            {
                for (const auto &header : m_queryResultHeaders)
                {
                    ImGui::TableSetupColumn(header.c_str());
                }
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(m_queryResultRows.size()));
                while (clipper.Step())
                {
                    for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx)
                    {
                        ImGui::TableNextRow();
                        const auto &row = m_queryResultRows[rowIdx];
                        for (size_t colIdx = 0; colIdx < m_queryResultHeaders.size(); ++colIdx)
                        {
                            ImGui::TableSetColumnIndex(static_cast<int>(colIdx));
                            if (colIdx < row.size())
                                ImGui::TextUnformatted(row[colIdx].c_str());
                            else
                                ImGui::TextDisabled("NULL");
                        }
                    }
                }
                clipper.End();
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}


void Application::handleCanvasInteraction()
{
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (const ImGuiPayload *payload = ImGui::GetDragDropPayload())
        {
            ed::SetCurrentEditor(m_queryContext);
            ImVec2 canvas_space_pos = ed::ScreenToCanvas(ImGui::GetMousePos());

            if (strcmp(payload->DataType, "DND_DATASET_NODE") == 0)
            {
                std::string dataset_name = (const char *)payload->Data;
                uintptr_t new_id = m_visualQueryNodes.size() + 100;
                VisualQueryNode newNode{new_id, dataset_name, canvas_space_pos, true, dataset_name, {}, false};
                m_visualQueryNodes.push_back(std::move(newNode));
            }
            else if (strcmp(payload->DataType, "DND_CTE_QUERY_NODE") == 0)
            {
                std::string cte_name = (const char *)payload->Data;
                uintptr_t new_id = m_visualQueryNodes.size() + 100;
                std::string default_alias = "cte_" + std::to_string(new_id);
                VisualQueryNode newNode{new_id, cte_name, canvas_space_pos, true, default_alias, {}, true};
                m_visualQueryNodes.push_back(std::move(newNode));
            }
            else if (strcmp(payload->DataType, "DND_PREDICATE_NODE") == 0)
            {
                uintptr_t new_pred_id = m_visualQueryPredicates.size() + 200;
                VisualQueryPredicate newPred{
                    new_pred_id, canvas_space_pos, true,
                    new_pred_id + 500000, new_pred_id + 550000,
                    "Choisir...", cppsqlx::SQLOperator::Equal, "", 0};
                m_visualQueryPredicates.push_back(std::move(newPred));
            }
            ed::SetCurrentEditor(nullptr);
        }
    }
}


void Application::handleCanvasActions()
{

    // Helper interne pour résoudre "alias.nom_colonne" depuis n'importe quel pin_id (Table OU CTE)
    auto resolvePinQualifiedName = [&](uintptr_t pinId) -> std::string
    {
        uintptr_t nodeId = pinId / 1000;
        size_t rawOffset = pinId % 1000;
        size_t colIdx = (rawOffset >= 500) ? (rawOffset - 500) : rawOffset;

        for (const auto &node : m_visualQueryNodes)
        {
            if (node.id != nodeId)
                continue;

            std::string prefix = node.alias.empty() ? "" : (node.alias + ".");

            // Cas 1 : Le nœud est une CTE (SavedQuery)
            if (node.isCTE)
            {
                for (const auto &sq : m_savedQueries)
                {
                    if (sq.name == node.datasetName && colIdx < sq.outputSchema.size())
                    {
                        return prefix + sq.outputSchema[colIdx].name;
                    }
                }
            }
            // Cas 2 : Le nœud est une Table physique (Dataset)
            else if (m_activeProject && m_activeProject->datasets().contains(node.datasetName))
            {
                const auto &ds = m_activeProject->datasets().at(node.datasetName);
                if (ds.columns().contains(colIdx))
                {
                    return prefix + ds.columns().at(colIdx).name();
                }
            }
        }
        return "";
    };

    if (ed::BeginCreate())
    {
        ed::PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId))
        {
            uintptr_t pin_src = startPinId.Get();
            uintptr_t pin_dst = endPinId.Get();
            bool src_is_filter = (pin_src >= 500000);
            bool dst_is_filter = (pin_dst >= 500000);
            ImVec4 wire_color = (src_is_filter || dst_is_filter) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f);

            // Helper interne pour résoudre "alias.colonne" depuis une Table OU une CTE
            auto resolvePinQualifiedName = [&](uintptr_t pin_id, const std::string &fallback) -> std::string
            {
                uintptr_t node_id = pin_id / 1000;
                uintptr_t col_idx = pin_id % 1000;
                if (col_idx >= 500)
                    col_idx -= 500;

                for (const auto &n : m_visualQueryNodes)
                {
                    if (n.id != node_id)
                        continue;

                    std::string prefix = n.alias.empty() ? "" : (n.alias + ".");

                    // Cas 1 : Nœud CTE (SavedQuery)
                    if (n.isCTE)
                    {
                        for (const auto &sq : m_savedQueries)
                        {
                            if (sq.name == n.datasetName && col_idx < sq.outputSchema.size())
                            {
                                return prefix + sq.outputSchema[col_idx].name;
                            }
                        }
                    }
                    // Cas 2 : Nœud Table physique (Dataset)
                    else if (m_activeProject && m_activeProject->datasets().contains(n.datasetName))
                    {
                        const auto &ds = m_activeProject->datasets().at(n.datasetName);
                        if (ds.columns().contains(col_idx))
                        {
                            return prefix + ds.columns().at(col_idx).name();
                        }
                    }
                }
                return fallback;
            };

            if (ed::AcceptNewItem(wire_color, 2.5f))
            {
                uintptr_t table_pin = src_is_filter ? pin_dst : pin_src;
                uintptr_t filter_pin = src_is_filter ? pin_src : pin_dst;

                if (src_is_filter || dst_is_filter)
                {
                    uintptr_t filter_node_id = (filter_pin >= 550000) ? (filter_pin - 550000) : (filter_pin - 500000);
                    std::string column_name = resolvePinQualifiedName(table_pin, "colonne");

                    for (auto &pred : m_visualQueryPredicates)
                    {
                        if (pred.id == filter_node_id)
                        {
                            pred.connectedSrcPinId = table_pin;
                            pred.targetColumn = column_name;
                        }
                    }
                }
                else
                {
                    uintptr_t new_id = m_visualQueryLinks.size() + 1;
                    std::string src_name = resolvePinQualifiedName(pin_src, "col_A");
                    std::string dst_name = resolvePinQualifiedName(pin_dst, "col_B");

                    VisualQueryLink newLink{new_id, pin_src, pin_dst, new_id + 5000, new_id + 10000, new_id + 15000, cppsqlx::JoinType::INNER, src_name, dst_name, cppsqlx::SQLOperator::Equal};

                    // Initialisation de la coordonnée à 0 pour forcer le premier calcul d'alignement
                    ed::SetNodePosition(newLink.centralNodeId, ImVec2(0.0f, 0.0f));
                    m_visualQueryLinks.push_back(std::move(newLink));
                }
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
                std::erase_if(m_visualQueryNodes, [deletedNodeId](const VisualQueryNode &node)
                              { return node.id == deletedNodeId.Get(); });
                std::erase_if(m_visualQueryPredicates, [deletedNodeId](const VisualQueryPredicate &pred)
                              { return pred.id == deletedNodeId.Get(); });
                std::erase_if(m_visualQueryLinks, [deletedNodeId](const VisualQueryLink &l)
                              { return (l.startPinId / 1000 == deletedNodeId.Get()) || (l.endPinId / 1000 == deletedNodeId.Get()) || (l.centralNodeId == deletedNodeId.Get()); });
            }
        }
    }
    ed::EndDelete();
}


void Application::renderCanvasDatasetNodes()
{
    for (auto &visualNode : m_visualQueryNodes)
    {
        if (visualNode.needsPositionInitialization)
        {
            visualNode.needsPositionInitialization = false;
            ed::SetNodePosition(visualNode.id, visualNode.position);
        }

        // Bordure légèrement dorée pour distinguer visuellement une CTE d'une table physique
        if (visualNode.isCTE)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.95f, 0.70f, 0.25f, 0.90f));
        }

        ed::BeginNode(visualNode.id);
        ImGui::BeginGroup();

        if (visualNode.isCTE)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "%s  CTE: %s", ICON_LC_FILE_TEXT, visualNode.datasetName.c_str());
            ImGui::SameLine(250.0f);
            ImGui::PushID(("preview_cte_" + std::to_string(visualNode.id)).c_str());
            if (ImGui::SmallButton(ICON_LC_EYE))
            {
                m_pendingCtePreviewName = visualNode.datasetName;
            }

            ImGui::SetItemTooltip("Visualiser le graphe source de la CTE '%s'", visualNode.datasetName.c_str());
            ImGui::PopID();
        }
        else
        {
            ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Table: %s", ICON_LC_DATABASE, visualNode.datasetName.c_str());
        }

        ImGui::TextDisabled("  Alias :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);

        std::array<char, 256> alias_buf{};
        snprintf(alias_buf.data(), alias_buf.size(), "%s", visualNode.alias.c_str());
        if (ImGui::InputText(("##alias_" + std::to_string(visualNode.id)).c_str(), alias_buf.data(), alias_buf.size()))
        {
            std::string oldAlias = visualNode.alias;
            std::string newAlias = alias_buf.data();
            visualNode.alias = newAlias;

            // Mise à jour en direct des projections rattachées à ce nœud
            if (!oldAlias.empty() && !newAlias.empty() && oldAlias != newAlias)
            {
                std::string oldPrefix = oldAlias + ".";
                std::string newPrefix = newAlias + ".";
                for (auto &item : m_projectionItems)
                {
                    if (!item.isCustomExpression && item.sourceNodeId == visualNode.id)
                    {
                        if (item.sqlExpression.starts_with(oldPrefix))
                        {
                            item.sqlExpression.replace(0, oldPrefix.length(), newPrefix);
                        }
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Helper interne pour dessiner une ligne de colonne (commune aux Tables et aux CTE)
        auto drawColumnRow = [&](size_t index, const std::string &raw_col_name, const std::string &raw_col_type)
        {
            std::string field_name = raw_col_name;
            if (field_name.length() > 14)
                field_name = field_name.substr(0, 11) + "...";

            uintptr_t input_pin_id = (visualNode.id * 1000) + index;
            ed::BeginPin(input_pin_id, ed::PinKind::Input);

            ImGui::PushID(static_cast<int>((visualNode.id * 1000) + index));
            bool is_selected = visualNode.selectedColumns.contains(index);
            if (ImGui::Checkbox("##select_col", &is_selected))
            {
                std::string qual_expr = visualNode.alias.empty() ? raw_col_name : (visualNode.alias + "." + raw_col_name);
                std::string default_alias = visualNode.alias.empty() ? raw_col_name : (visualNode.alias + "_" + raw_col_name);

                if (is_selected)
                {
                    visualNode.selectedColumns.insert(index);

                    auto it = std::find_if(m_projectionItems.begin(), m_projectionItems.end(),
                                           [&qual_expr](const VisualProjectionItem &item)
                                           { return !item.isCustomExpression && item.sqlExpression == qual_expr; });
                    if (it == m_projectionItems.end())
                    {
                        uintptr_t nextId = m_projectionItems.size() + 70000;
                        m_projectionItems.push_back(VisualProjectionItem{
                            nextId, default_alias, qual_expr, false, false, visualNode.id});
                    }

                    if (!m_hasProjectionNode)
                    {
                        m_hasProjectionNode = true;
                        m_needsProjectionPosInit = true;
                    }
                }
                else
                {
                    visualNode.selectedColumns.erase(index);
                    std::erase_if(m_projectionItems,
                                  [&qual_expr](const VisualProjectionItem &item)
                                  { return !item.isCustomExpression && item.sqlExpression == qual_expr; });
                }
            }
            ImGui::PopID();

            ImGui::SameLine();
            ImGui::TextDisabled("%s", field_name.c_str());
            ed::EndPin();

            float type_local_offset = 140.0f;
            float right_pin_offset = 280.0f;

            ImGui::SameLine(type_local_offset);
            std::string field_type = raw_col_type;
            if (field_type.length() > 16)
                field_type = field_type.substr(0, 13) + "...";
            ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.4f, 0.75f), ":  %s", field_type.c_str());

            ImGui::SameLine(right_pin_offset);
            uintptr_t output_pin_id = (visualNode.id * 1000) + index + 500;
            ed::BeginPin(output_pin_id, ed::PinKind::Output);
            ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 0.5f), ICON_LC_CHEVRON_RIGHT);
            ed::EndPin();
        };

        // Branche A : Nœud CTE (lit le schéma déduit via LIMIT 0 de la SavedQuery)
        if (visualNode.isCTE)
        {
            const SavedQuery *foundQuery = nullptr;
            for (const auto &sq : m_savedQueries)
            {
                if (sq.name == visualNode.datasetName)
                {
                    foundQuery = &sq;
                    break;
                }
            }

            if (foundQuery)
            {
                if (foundQuery->outputSchema.empty())
                {
                    ImGui::TextDisabled("  (Schéma vide - réenregistrez la requête)");
                }
                else
                {
                    for (size_t idx = 0; idx < foundQuery->outputSchema.size(); ++idx)
                    {
                        drawColumnRow(idx, foundQuery->outputSchema[idx].name, foundQuery->outputSchema[idx].dataType);
                    }
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "  Requête introuvable !");
            }
        }
        // Branche B : Dataset physique classique
        else if (m_activeProject && m_activeProject->datasets().contains(visualNode.datasetName))
        {
            const auto &dataset = m_activeProject->datasets().at(visualNode.datasetName);
            for (const auto &[index, colObj] : dataset.columns())
            {
                drawColumnRow(index, colObj.name(), colObj.type());
            }
        }

        ImGui::Spacing();
        ImGui::EndGroup();
        ed::EndNode();

        if (visualNode.isCTE)
        {
            ed::PopStyleColor();
        }
    }
}


void Application::renderCanvasFilterNodes()
{
    for (auto &predicateNode : m_visualQueryPredicates)
    {
        if (predicateNode.needsPositionInitialization)
        {
            predicateNode.needsPositionInitialization = false;
            ed::SetNodePosition(predicateNode.id, predicateNode.position);
        }

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.22f, 0.15f, 0.32f, 0.85f));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.55f, 0.35f, 0.9f, 1.0f));

        ed::BeginNode(predicateNode.id);
        ImGui::BeginGroup();

        ImGui::TextColored(ImVec4(0.75f, 0.55f, 1.0f, 1.0f), "%s  Filtre [WHERE]", ICON_LC_FILTER);
        ImGui::Spacing();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        ed::BeginPin(predicateNode.inputPinId, ed::PinKind::Input);
        ImGui::TextDisabled(" ▪ In");
        ed::EndPin();
        ImGui::SameLine();

        std::string live_target_column = predicateNode.targetColumn;
        if (predicateNode.connectedSrcPinId != 0)
        {
            uintptr_t table_node_id = predicateNode.connectedSrcPinId / 1000;
            for (const auto &n : m_visualQueryNodes)
            {
                if (n.id == table_node_id)
                {
                    std::string raw_col = predicateNode.targetColumn;
                    if (auto dot = raw_col.find('.'); dot != std::string::npos)
                        raw_col = raw_col.substr(dot + 1);
                    live_target_column = n.alias + "." + raw_col;
                }
            }
        }

        std::string op_symbol = std::string(cppsqlx::toSQL(predicateNode.op));
        std::string summary;
        if (predicateNode.isCustomExpression)
        {
            summary = predicateNode.customSqlCondition.empty() ? "Définir expression WHERE..." : predicateNode.customSqlCondition;
        }
        else
        {
            std::string op_symbol = std::string(cppsqlx::toSQL(predicateNode.op));
            summary = live_target_column + " " + op_symbol + " " + (predicateNode.value.empty() ? "?" : predicateNode.value);
        }

        if (summary.length() > 28)
            summary = summary.substr(0, 25) + "...";

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.9f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.35f, 0.9f, 0.45f));
        if (ImGui::Button(summary.c_str(), ImVec2(130.0f, 24.0f)))
        {
            m_editingPredicate = &predicateNode;
            m_showPredicateModal = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("Cliquez pour éditer la condition du prédicat");
        ImGui::SameLine();

        ed::BeginPin(predicateNode.outputPinId, ed::PinKind::Output);
        ImGui::TextColored(ImVec4(0.55f, 0.35f, 0.9f, 0.6f), " %s", ICON_LC_CHEVRON_RIGHT);
        ed::EndPin();

        ImGui::EndGroup();
        ed::EndNode();
        ed::PopStyleColor(2);

        if (predicateNode.connectedSrcPinId != 0)
        {
            ed::Link(predicateNode.id + 90000, predicateNode.connectedSrcPinId, predicateNode.inputPinId, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 2.5f);
        }
    }
}


void Application::renderCanvasJoinNodes()
{
    for (auto &link : m_visualQueryLinks)
    {
        ImGui::PushID(static_cast<int>(link.id));
        bool connects_to_filter = (link.startPinId >= 500000);

        if (connects_to_filter)
        {
            ed::Link(link.id * 10, link.startPinId, link.endPinId, ImVec4(1.0f, 1.0f, 1.0f, 0.8f), 2.0f);
        }
        else
        {
            uintptr_t src_node_id = link.startPinId / 1000;
            uintptr_t dst_node_id = link.endPinId / 1000;

            ImVec2 p_start = ed::GetNodePosition(src_node_id);

            // 1. Initialisation unique au moment du tracé ou du rechargement du lien
            if (link.needsJoinPosInit)
            {
                link.needsJoinPosInit = false;
                ed::SetNodePosition(link.centralNodeId, link.joinPosition);
            }
            else
            {
                ImVec2 current_pos = ed::GetNodePosition(link.centralNodeId);
                if (current_pos.x == 0.0f && current_pos.y == 0.0f)
                {
                    uintptr_t src_col_idx = link.startPinId % 1000;
                    if (src_col_idx >= 500)
                        src_col_idx -= 500;
                    float src_pin_y = p_start.y + 53.0f + ((float)src_col_idx * 23.5f) + 11.0f;
                    ed::SetNodePosition(link.centralNodeId, ImVec2(p_start.x + 302.0f + 45.0f, src_pin_y - 20.0f));
                }
            }

            // 2. Dessin du gros nœud rectangulaire vert
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.12f, 0.28f, 0.18f, 0.85f));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.25f, 0.75f, 0.45f, 1.0f));

            ed::BeginNode(link.centralNodeId);
            ImVec2 local_cursor = ImGui::GetCursorPos();

            ImGui::BeginGroup();
            std::string join_label = "INNER JOIN";
            ImVec4 icon_color = ImVec4(0.25f, 0.75f, 0.45f, 1.0f);

            if (link.isSetOperation)
            {
                join_label = cppsqlx::toString(link.setOp);     // "UNION ALL", "UNION", etc.
                icon_color = ImVec4(0.75f, 0.45f, 0.95f, 1.0f); // Violet distinctif
            }
            else
            {
                if (link.type == cppsqlx::JoinType::LEFT)
                {
                    join_label = "LEFT JOIN";
                    icon_color = ImVec4(0.40f, 0.80f, 0.40f, 1.0f);
                }
                else if (link.type == cppsqlx::JoinType::RIGHT)
                {
                    join_label = "RIGHT JOIN";
                    icon_color = ImVec4(0.85f, 0.65f, 0.20f, 1.0f);
                }
                else if (link.type == cppsqlx::JoinType::FULL)
                {
                    join_label = "FULL JOIN";
                    icon_color = ImVec4(0.90f, 0.40f, 0.90f, 1.0f);
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(icon_color.x, icon_color.y, icon_color.z, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(icon_color.x, icon_color.y, icon_color.z, 0.40f));
            if (ImGui::Button(join_label.c_str(), ImVec2(140.0f, 24.0f)))
            {
                m_editingJoinLink = &link;
                m_showJoinModal = true;
            }
            ImGui::PopStyleColor(2);
            ImGui::Spacing();

            // Formule textuelle réactive sous le bouton
            std::string src_alias = "", dst_alias = "";
            for (const auto &n : m_visualQueryNodes)
            {
                if (n.id == src_node_id)
                    src_alias = n.alias;
                if (n.id == dst_node_id)
                    dst_alias = n.alias;
            }
            std::string left_pure = link.leftFieldName;
            if (auto dot_pos = left_pure.find('.'); dot_pos != std::string::npos)
                left_pure = left_pure.substr(dot_pos + 1);
            std::string right_pure = link.rightFieldName;
            if (auto dot_pos = right_pure.find('.'); dot_pos != std::string::npos)
                right_pure = right_pure.substr(dot_pos + 1);
            std::string op_symbol = std::string(cppsqlx::toSQL(link.op));
            std::string display_expr;
            if (link.isSetOperation)
            {
                display_expr = src_alias + " " + join_label + " " + dst_alias;
            }
            else
            {
                std::string left_pure = link.leftFieldName;
                if (auto dot_pos = left_pure.find('.'); dot_pos != std::string::npos)
                    left_pure = left_pure.substr(dot_pos + 1);
                std::string right_pure = link.rightFieldName;
                if (auto dot_pos = right_pure.find('.'); dot_pos != std::string::npos)
                    right_pure = right_pure.substr(dot_pos + 1);
                std::string op_symbol = std::string(cppsqlx::toSQL(link.op));
                display_expr = src_alias + "." + left_pure + " " + op_symbol + " " + dst_alias + "." + right_pure;
            }

            if (display_expr.length() > 24)
                display_expr = display_expr.substr(0, 21) + "...";
            float text_width = ImGui::CalcTextSize(display_expr.c_str()).x;
            if (text_width < 140.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (140.0f - text_width) * 0.5f);
            ImGui::TextDisabled("%s", display_expr.c_str());
            ImGui::EndGroup();

            // Superposition logique des Pins pour l'effet de câble continu traversant
            ImGui::SetCursorPos(ImVec2(local_cursor.x + 70.0f, local_cursor.y + 12.0f));
            ed::BeginPin(link.leftPinId, ed::PinKind::Input);
            ed::EndPin();
            ImGui::SameLine(0.0f, 0.0f);
            ed::BeginPin(link.rightPinId, ed::PinKind::Output);
            ed::EndPin();

            ed::EndNode();
            ed::PopStyleColor(2);

            // Rendu des doubles segments raccordés au point central
            ed::Link(link.id * 10, link.startPinId, link.leftPinId, ImVec4(0.7f, 0.7f, 0.7f, 0.6f), 2.0f);
            ed::Link(link.id * 10 + 1, link.rightPinId, link.endPinId, icon_color, 2.5f);
        }
        ImGui::PopID();
    }
}


void Application::renderCanvasProjectionNode()
{
    if (!m_hasProjectionNode && m_projectionItems.empty())
        return;

    const uintptr_t PROJECTION_NODE_ID = 99000;
    if (m_needsProjectionPosInit)
    {
        m_needsProjectionPosInit = false;
        ed::SetNodePosition(PROJECTION_NODE_ID, m_projectionNodePos);
    }

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.14f, 0.20f, 0.28f, 0.92f));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.25f, 0.70f, 0.95f, 1.0f));
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(10.0f, 10.0f, 10.0f, 10.0f));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);

    ed::BeginNode(PROJECTION_NODE_ID);
    ImGui::BeginGroup();

    // En-tête du nœud (sans chevauchement)
    ImGui::TextColored(ImVec4(0.35f, 0.80f, 1.0f, 1.0f), "%s  Projection [SELECT]", ICON_LC_LAYERS);
    ImGui::SameLine(250.0f);
    if (ImGui::SmallButton((std::string(ICON_LC_PLUS_CIRCLE) + " Expr").c_str()))
    {
        m_editingProjectionIndex = -1;
        memset(m_exprAliasBuf, 0, sizeof(m_exprAliasBuf));
        memset(m_exprFormulaBuf, 0, sizeof(m_exprFormulaBuf));
        m_exprIsAggregate = false;
        m_showExpressionModal = true;
    }
    ImGui::SetItemTooltip("Créer une nouvelle colonne calculée (Expression SQL)");

    // Remplacement de ImGui::Separator() (qui débordait sur tout le canevas) par un espace net
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    ImGui::Spacing();

    if (m_projectionItems.empty())
    {
        ImGui::TextDisabled("Cochez des colonnes ou cliquez sur '+ Expr'");
    }
    else
    {
        int indexToDelete = -1;
        int indexMoveUp = -1;
        int indexMoveDown = -1;

        for (int i = 0; i < static_cast<int>(m_projectionItems.size()); ++i)
        {
            auto &item = m_projectionItems[i];
            ImGui::PushID(static_cast<int>(item.id) + i);

            // Badge visuel : [fx] pour une expression calculée, [col] pour un champ brut
            if (item.isCustomExpression)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "[fx]");
            else
                ImGui::TextDisabled("[col]");

            ImGui::SameLine();

            // Résumé cliquable pour ouvrir l'éditeur d'expression / renommer l'alias
            std::string displayLabel = item.alias + " = " + item.sqlExpression;
            if (displayLabel.length() > 34)
                displayLabel = displayLabel.substr(0, 31) + "...";

            if (ImGui::Selectable(displayLabel.c_str(), false, ImGuiSelectableFlags_None, ImVec2(210.0f, 18.0f)))
            {
                m_editingProjectionIndex = i;
                strncpy(m_exprAliasBuf, item.alias.c_str(), sizeof(m_exprAliasBuf) - 1);
                m_exprAliasBuf[sizeof(m_exprAliasBuf) - 1] = '\0';
                strncpy(m_exprFormulaBuf, item.sqlExpression.c_str(), sizeof(m_exprFormulaBuf) - 1);
                m_exprFormulaBuf[sizeof(m_exprFormulaBuf) - 1] = '\0';
                m_exprIsAggregate = item.isAggregate;
                m_showExpressionModal = true;
            }
            ImGui::SetItemTooltip("Cliquer pour éditer l'alias ou la formule SQL :\n%s AS %s", item.sqlExpression.c_str(), item.alias.c_str());

            // Boutons compacts d'ordonnancement et de suppression
            ImGui::SameLine();
            if (i > 0 && ImGui::SmallButton("^"))
                indexMoveUp = i;
            ImGui::SameLine();
            if (i + 1 < static_cast<int>(m_projectionItems.size()) && ImGui::SmallButton("v"))
                indexMoveDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                indexToDelete = i;

            ImGui::PopID();
        }

        if (indexMoveUp > 0)
            std::swap(m_projectionItems[indexMoveUp], m_projectionItems[indexMoveUp - 1]);
        if (indexMoveDown >= 0)
            std::swap(m_projectionItems[indexMoveDown], m_projectionItems[indexMoveDown + 1]);
        if (indexToDelete >= 0)
            m_projectionItems.erase(m_projectionItems.begin() + indexToDelete);
    }

    ImGui::EndGroup();
    ed::EndNode();

    ed::PopStyleVar(2);
    ed::PopStyleColor(2);
}


void Application::renderQueryModals()
{
    // =========================================================================
    // MODALE 1 : ÉDITION DE LA JOINTURE (JOIN)
    // =========================================================================
    if (m_showJoinModal && m_editingJoinLink != nullptr)
    {
        ImGui::OpenPopup("Éditeur de Clause de Jointure");
    }
    if (ImGui::BeginPopupModal("Éditeur de Clause de Jointure", &m_showJoinModal, ImGuiWindowFlags_None))
    {
        uintptr_t s_node = m_editingJoinLink->startPinId / 1000;
        uintptr_t d_node = m_editingJoinLink->endPinId / 1000;
        std::string current_src_alias = "";
        std::string current_dst_alias = "";
        bool src_is_cte = false;
        bool dst_is_cte = false;

        for (const auto &n : m_visualQueryNodes)
        {
            if (n.id == s_node)
            {
                current_src_alias = n.alias;
                src_is_cte = n.isCTE;
            }
            if (n.id == d_node)
            {
                current_dst_alias = n.alias;
                dst_is_cte = n.isCTE;
            }
        }

        bool canBeSetOp = (src_is_cte && dst_is_cte);
        if (!canBeSetOp)
        {
            m_editingJoinLink->isSetOperation = false;
        }

        if (m_editingJoinLink->isSetOperation)
            ImGui::TextColored(ImVec4(0.75f, 0.45f, 0.95f, 1.0f), "%s  Configuration de l'Assemblage Ensembliste (UNION)", ICON_LC_SETTINGS);
        else
            ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Configuration de l'Opérateur JOIN", ICON_LC_SETTINGS);

        ImGui::Separator();
        ImGui::Spacing();

        // Option disponible uniquement lorsque deux CTE sont reliées entre elles
        canBeSetOp = true;
        if (canBeSetOp)
        {
            ImGui::Checkbox("Mode Assemblage Ensembliste (UNION ALL / UNION / INTERSECT / EXCEPT)", &m_editingJoinLink->isSetOperation);
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (m_editingJoinLink->isSetOperation)
        {
            ImGui::TextDisabled("1. OPÉRATEUR ENSEMBLISTE ENTRE LES DEUX REQUÊTES");
            ImGui::Spacing();

            int current_set_op = static_cast<int>(m_editingJoinLink->setOp);
            ImGui::RadioButton("UNION ALL (Empilement complet sans dédoublonnage)", &current_set_op, static_cast<int>(cppsqlx::SetOperator::UNION_ALL));
            ImGui::RadioButton("UNION (Empilement avec dédoublonnage DISTINCT)", &current_set_op, static_cast<int>(cppsqlx::SetOperator::UNION));
            ImGui::RadioButton("INTERSECT (Lignes communes aux deux requêtes)", &current_set_op, static_cast<int>(cppsqlx::SetOperator::INTERSECT));
            ImGui::RadioButton("EXCEPT (Lignes de la première absentes de la seconde)", &current_set_op, static_cast<int>(cppsqlx::SetOperator::EXCEPT));
            m_editingJoinLink->setOp = static_cast<cppsqlx::SetOperator>(current_set_op);

            ImGui::Spacing();
            ImGui::TextDisabled("Flux assemblé : %s  %s  %s",
                                current_src_alias.c_str(),
                                cppsqlx::toString(m_editingJoinLink->setOp).c_str(),
                                current_dst_alias.c_str());
            ImGui::TextDisabled("Les deux requêtes doivent projeter le même nombre de colonnes.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float avail_x = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - 120.0f);
            if (ImGui::Button("Enregistrer", ImVec2(120, 0)))
            {
                m_showJoinModal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::TextDisabled("1. TYPE DE JOINTURE");
            int current_type = (int)m_editingJoinLink->type;
            ImGui::RadioButton("INNER JOIN (Intersection)", &current_type, (int)cppsqlx::JoinType::INNER);
            ImGui::RadioButton("LEFT JOIN (Priorité Table Gauche)", &current_type, (int)cppsqlx::JoinType::LEFT);
            ImGui::RadioButton("RIGHT JOIN (Priorité Table Droite)", &current_type, (int)cppsqlx::JoinType::RIGHT);
            ImGui::RadioButton("FULL JOIN (Union Complète)", &current_type, (int)cppsqlx::JoinType::FULL);
            m_editingJoinLink->type = (cppsqlx::JoinType)current_type;
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("2. CONDITION DE JOINTURE STRUCTUREE (SQLJoinExpression)");
            ImGui::Spacing();

            std::string left_col_raw = m_editingJoinLink->leftFieldName;
            if (auto dot = left_col_raw.find('.'); dot != std::string::npos)
                left_col_raw = left_col_raw.substr(dot + 1);
            std::string right_col_raw = m_editingJoinLink->rightFieldName;
            if (auto dot = right_col_raw.find('.'); dot != std::string::npos)
                right_col_raw = right_col_raw.substr(dot + 1);
            std::string live_left_display = current_src_alias + "." + left_col_raw;
            std::string live_right_display = current_dst_alias + "." + right_col_raw;

            if (ImGui::BeginTable("##join_expr_table", 3, ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Champ Gauche", ImGuiTableColumnFlags_WidthStretch, 0.43f);
                ImGui::TableSetupColumn("Op", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                ImGui::TableSetupColumn("Champ Droite", ImGuiTableColumnFlags_WidthStretch, 0.43f);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##left_field_combo", live_left_display.c_str()))
                {
                    if (ImGui::Selectable(live_left_display.c_str(), true))
                    {
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                std::string_view current_op_symbol = cppsqlx::toSQL(m_editingJoinLink->op);
                if (ImGui::BeginCombo("##op_combo", current_op_symbol.data()))
                {
                    for (auto const &item : cppsqlx::operatorMap)
                    {
                        if (cppsqlx::isLogical(item.op))
                            continue;
                        if (ImGui::Selectable(item.symbol.data(), m_editingJoinLink->op == item.op))
                        {
                            m_editingJoinLink->op = item.op;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##right_field_combo", live_right_display.c_str()))
                {
                    if (ImGui::Selectable(live_right_display.c_str(), true))
                    {
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            float avail_x = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - 120.0f);
            if (ImGui::Button("Enregistrer", ImVec2(120, 0)))
            {
                m_editingJoinLink->leftFieldName = live_left_display;
                m_editingJoinLink->rightFieldName = live_right_display;
                m_showJoinModal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    // =========================================================================
    // MODALE 2 : ÉDITION DE LA CLAUSE WHERE (SIMPLE OU EXPRESSION SQL AVANCÉE)
    // =========================================================================
    if (m_showPredicateModal && m_editingPredicate != nullptr)
    {
        ImGui::OpenPopup("Configuration du Filtre WHERE");
    }

    if (m_editingPredicate != nullptr && m_editingPredicate->isCustomExpression)
        ImGui::SetNextWindowSize(ImVec2(860.0f, 440.0f), ImGuiCond_Appearing);
    else
        ImGui::SetNextWindowSize(ImVec2(520.0f, 240.0f), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Configuration du Filtre WHERE", &m_showPredicateModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.75f, 0.55f, 1.0f, 1.0f), "%s  Configuration de la Condition de Filtrage (WHERE)", ICON_LC_FILTER);
        ImGui::Separator();
        ImGui::Spacing();

        std::string modal_target_display = m_editingPredicate->targetColumn;
        if (m_editingPredicate->connectedSrcPinId != 0)
        {
            uintptr_t table_node_id = m_editingPredicate->connectedSrcPinId / 1000;
            for (const auto &n : m_visualQueryNodes)
            {
                if (n.id == table_node_id)
                {
                    std::string raw_col = m_editingPredicate->targetColumn;
                    if (auto dot = raw_col.find('.'); dot != std::string::npos)
                        raw_col = raw_col.substr(dot + 1);
                    modal_target_display = n.alias + "." + raw_col;
                }
            }
        }

        // Case à cocher pour basculer entre Condition Simple et Expression SQL libre
        if (ImGui::Checkbox("Mode Expression SQL Avancée (Condition libre, fonctions, LIKE, IN, AND/OR...)", &m_editingPredicate->isCustomExpression))
        {
            if (m_editingPredicate->isCustomExpression)
            {
                ImGui::SetWindowSize(ImVec2(860.0f, 440.0f));
                if (m_editingPredicate->customSqlCondition.empty() && m_editingPredicate->connectedSrcPinId != 0)
                {
                    std::string initExpr = modal_target_display + " " +
                                           std::string(cppsqlx::toSQL(m_editingPredicate->op)) + " " +
                                           m_editingPredicate->value;
                    strncpy(m_whereExprFormulaBuf, initExpr.c_str(), sizeof(m_whereExprFormulaBuf) - 1);
                    m_whereExprFormulaBuf[sizeof(m_whereExprFormulaBuf) - 1] = '\0';
                }
            }
            else
            {
                ImGui::SetWindowSize(ImVec2(520.0f, 240.0f));
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!m_editingPredicate->isCustomExpression)
        {
            // -----------------------------------------------------------------
            // MODE 1 : CONDITION SIMPLE (Colonne liée | Opérateur | Valeur)
            // -----------------------------------------------------------------
            ImGui::TextDisabled("Champ cible :");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", modal_target_display.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("ÉDITION DE L'EXPRESSION DE FILTRE");
            ImGui::Spacing();

            if (ImGui::BeginTable("##predicate_expr_table", 2, ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Opérateur", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("Valeur / Littéral", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                std::string_view current_op_symbol = cppsqlx::toSQL(m_editingPredicate->op);
                if (ImGui::BeginCombo("##pred_op_combo", current_op_symbol.data()))
                {
                    for (auto const &item : cppsqlx::operatorMap)
                    {
                        if (cppsqlx::isLogical(item.op))
                            continue;
                        if (ImGui::Selectable(item.symbol.data(), m_editingPredicate->op == item.op))
                        {
                            m_editingPredicate->op = item.op;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                char val_buf[256];
                snprintf(val_buf, sizeof(val_buf), "%s", m_editingPredicate->value.c_str());
                if (ImGui::InputText("##pred_value_input", val_buf, sizeof(val_buf)))
                {
                    m_editingPredicate->value = val_buf;
                }
                ImGui::EndTable();
            }
        }
        else
        {
            // -----------------------------------------------------------------
            // MODE 2 : EXPRESSION SQL AVANCÉE (3 COLONNES)
            // -----------------------------------------------------------------
            if (strlen(m_whereExprFormulaBuf) == 0 && !m_editingPredicate->customSqlCondition.empty())
            {
                strncpy(m_whereExprFormulaBuf, m_editingPredicate->customSqlCondition.c_str(), sizeof(m_whereExprFormulaBuf) - 1);
                m_whereExprFormulaBuf[sizeof(m_whereExprFormulaBuf) - 1] = '\0';
            }
            float bodyHeight = ImGui::GetContentRegionAvail().y - 45.0f;
            if (bodyHeight < 180.0f)
                bodyHeight = 180.0f;
            auto appendToWhere = [&](const std::string &snippet)
            {
                size_t len = strlen(m_whereExprFormulaBuf);
                if (len + snippet.length() + 1 < sizeof(m_whereExprFormulaBuf))
                {
                    strcat(m_whereExprFormulaBuf, snippet.c_str());
                }
            };
            // Colonne gauche : Champs du graphe
            ImGui::BeginChild("WhereFieldsPalette", ImVec2(220.0f, bodyHeight), true);
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "CHAMPS DU GRAPHE");
            ImGui::Separator();
            if (m_activeProject)
            {
                for (const auto &node : m_visualQueryNodes)
                {
                    if (!m_activeProject->datasets().contains(node.datasetName))
                        continue;
                    const auto &ds = m_activeProject->datasets().at(node.datasetName);
                    if (ImGui::TreeNodeEx((node.alias + " (" + node.datasetName + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (const auto &[idx, col] : ds.columns())
                        {
                            std::string qual = node.alias + "." + col.name();
                            if (ImGui::Selectable(qual.c_str()))
                            {
                                appendToWhere(qual);
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            // Colonne centrale : Zone de saisie de la condition SQL
            float centerWidth = ImGui::GetContentRegionAvail().x - 225.0f;
            ImGui::BeginChild("WhereFormulaZone", ImVec2(centerWidth, bodyHeight), true);
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "CONDITION SQL (EX: UPPER(emp.nom) LIKE 'M%')");
            ImGui::Separator();
            ImGui::InputTextMultiline("##WhereCustomMultiline", m_whereExprFormulaBuf, IM_ARRAYSIZE(m_whereExprFormulaBuf),
                                      ImVec2(-1.0f, bodyHeight - 35.0f), ImGuiInputTextFlags_AllowTabInput);
            ImGui::EndChild();
            ImGui::SameLine();
            // Colonne droite : Opérateurs et fonctions SQL
            ImGui::BeginChild("WhereOperatorsPalette", ImVec2(220.0f, bodyHeight), true);
            ImGui::TextColored(ImVec4(0.75f, 0.55f, 1.0f, 1.0f), "OPÉRATEURS & FONCTIONS");
            ImGui::Separator();
            const char *whereSnippets[] = {
                " AND ", " OR ", " NOT ",
                " = ", " <> ", " > ", " < ", " >= ", " <= ",
                " LIKE '%'", " ILIKE '%'",
                " IN (, )", " NOT IN (, )",
                " BETWEEN  AND ",
                " IS NULL", " IS NOT NULL",
                "UPPER()", "LOWER()", "TRIM()",
                "COALESCE(, 0)", "EXTRACT(YEAR FROM )"};
            for (const char *snip : whereSnippets)
            {
                if (ImGui::Selectable(snip))
                {
                    appendToWhere(snip);
                }
            }
            ImGui::EndChild();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_x - 230.0f);
        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            memset(m_whereExprFormulaBuf, 0, sizeof(m_whereExprFormulaBuf));
            m_showPredicateModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Enregistrer", ImVec2(120.0f, 0.0f)))
        {
            if (m_editingPredicate->isCustomExpression)
            {
                m_editingPredicate->customSqlCondition = std::string(m_whereExprFormulaBuf);
                memset(m_whereExprFormulaBuf, 0, sizeof(m_whereExprFormulaBuf));
            }
            else
            {
                m_editingPredicate->targetColumn = modal_target_display;
            }
            m_showPredicateModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // =========================================================================
    // MODALE 3 : ÉDITEUR AVANCÉ D'EXPRESSION SQL (SELECT / TRANSFORMATIONS)
    // =========================================================================
    renderExpressionEditorModal();
}


void Application::renderExpressionEditorModal()
{
    if (m_showExpressionModal)
        ImGui::OpenPopup("Éditeur Avancé d'Expression SQL");

    ImGui::SetNextWindowSize(ImVec2(880.0f, 520.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints(ImVec2(680.0f, 400.0f), ImVec2(1600.0f, 900.0f));

    if (ImGui::BeginPopupModal("Éditeur Avancé d'Expression SQL", &m_showExpressionModal, ImGuiWindowFlags_None))
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "%s  Concepteur d'Expression & Transformation SQL (PostgreSQL)", ICON_LC_CPU);
        ImGui::Separator();
        ImGui::Spacing();

        // Nom de la colonne de sortie (Alias) + Case Agrégat
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Alias de sortie (AS) :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##ExprAliasInput", "ex: nom_complet, total_ttc...", m_exprAliasBuf, IM_ARRAYSIZE(m_exprAliasBuf));

        ImGui::SameLine(0.0f, 25.0f);
        ImGui::Checkbox("Fonction d'agrégation (active le GROUP BY auto)", &m_exprIsAggregate);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float bodyHeight = ImGui::GetContentRegionAvail().y - 45.0f;

        // COLONNE 1 (GAUCHE) : Colonnes du graphe disponibles pour insertion rapide
        ImGui::BeginChild("ExprFieldsPalette", ImVec2(240.0f, bodyHeight), true);
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "CHAMPS DU GRAPHE");
        ImGui::Separator();
        ImGui::Spacing();

        auto appendSnippetToFormula = [&](const std::string &snippet)
        {
            size_t curLen = strlen(m_exprFormulaBuf);
            if (curLen + snippet.length() + 1 < sizeof(m_exprFormulaBuf))
            {
                strcat(m_exprFormulaBuf, snippet.c_str());
            }
        };

        if (m_visualQueryNodes.empty())
        {
            ImGui::TextDisabled("Aucune table ou CTE sur le canevas.");
        }
        else
        {
            for (const auto &node : m_visualQueryNodes)
            {
                std::string prefix = node.alias.empty() ? "" : (node.alias + ".");
                std::string header = (node.isCTE ? "[CTE] " : "") + node.alias + " (" + node.datasetName + ")";

                // Cas 1 : Le nœud est une requête enregistrée (CTE)
                if (node.isCTE)
                {
                    if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        bool foundCte = false;
                        for (const auto &sq : m_savedQueries)
                        {
                            if (sq.name == node.datasetName)
                            {
                                foundCte = true;
                                for (const auto &col : sq.outputSchema)
                                {
                                    std::string qual = prefix + col.name;
                                    if (ImGui::Selectable(qual.c_str()))
                                    {
                                        appendSnippetToFormula(qual);
                                    }
                                    ImGui::SetItemTooltip("Cliquer pour insérer '%s' (%s)", qual.c_str(), col.dataType.c_str());
                                }
                                break;
                            }
                        }
                        if (!foundCte)
                            ImGui::TextDisabled("(Schéma CTE introuvable)");
                        ImGui::TreePop();
                    }
                }
                // Cas 2 : Le nœud est une table physique (Dataset)
                else if (m_activeProject && m_activeProject->datasets().contains(node.datasetName))
                {
                    const auto &ds = m_activeProject->datasets().at(node.datasetName);
                    if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (const auto &[idx, col] : ds.columns())
                        {
                            std::string qual = prefix + col.name();
                            if (ImGui::Selectable(qual.c_str()))
                            {
                                appendSnippetToFormula(qual);
                            }
                            ImGui::SetItemTooltip("Cliquer pour insérer '%s' (%s)", qual.c_str(), col.type().c_str());
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }
        ImGui::EndChild(); // <-- Fermeture obligatoire de ExprFieldsPalette

        ImGui::SameLine();

        // COLONNE 2 (CENTRE) : Zone d'édition de la formule SQL
        float centerWidth = ImGui::GetContentRegionAvail().x - 245.0f;
        ImGui::BeginChild("ExprEditorZone", ImVec2(centerWidth, bodyHeight), true);
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "FORMULE SQL DE TRANSFORMATION");
        ImGui::Separator();
        ImGui::Spacing();

        float textAreaHeight = ImGui::GetContentRegionAvail().y - 35.0f;
        ImGui::InputTextMultiline("##ExprFormulaMultiline", m_exprFormulaBuf, IM_ARRAYSIZE(m_exprFormulaBuf),
                                  ImVec2(-1.0f, textAreaHeight), ImGuiInputTextFlags_AllowTabInput);

        ImGui::TextDisabled("Astuce : Cliquez sur un champ à gauche ou une fonction à droite pour l'insérer.");
        ImGui::EndChild();

        ImGui::SameLine();

        // COLONNE 3 (DROITE) : Catalogue de fonctions SQL prêtes à l'emploi
        ImGui::BeginChild("ExprFunctionsCatalog", ImVec2(235.0f, bodyHeight), true);
        ImGui::TextColored(ImVec4(0.75f, 0.55f, 1.0f, 1.0f), "FONCTIONS SQL");
        ImGui::Separator();
        ImGui::Spacing();

        struct SqlFuncTemplate
        {
            const char *label;
            const char *snippet;
            bool isAgg;
        };
        const SqlFuncTemplate funcs[] = {
            {"CONCAT(a, ' ', b)", "CONCAT(, ' ', )", false},
            {"UPPER(str)", "UPPER()", false},
            {"LOWER(str)", "LOWER()", false},
            {"TRIM(str)", "TRIM()", false},
            {"SUBSTRING(str, 1, 3)", "SUBSTRING(, 1, 3)", false},
            {"COALESCE(val, 0)", "COALESCE(, 0)", false},
            {"CASE WHEN ... END", "CASE WHEN  THEN  ELSE  END", false},
            {"ROUND(num, 2)", "ROUND(, 2)", false},
            {"CAST(... AS NUMERIC)", "CAST( AS NUMERIC)", false},
            {"EXTRACT(YEAR FROM d)", "EXTRACT(YEAR FROM )", false},
            {"TO_CHAR(d, 'YYYY-MM')", "TO_CHAR(, 'YYYY-MM')", false},
            {"SUM(col)", "SUM()", true},
            {"COUNT(col)", "COUNT()", true},
            {"COUNT(DISTINCT col)", "COUNT(DISTINCT )", true},
            {"AVG(col)", "AVG()", true},
            {"MIN(col)", "MIN()", true},
            {"MAX(col)", "MAX()", true}};

        for (const auto &fn : funcs)
        {
            if (ImGui::Selectable(fn.label))
            {
                appendSnippetToFormula(fn.snippet);
                if (fn.isAgg)
                    m_exprIsAggregate = true;
            }
            ImGui::SetItemTooltip("Insérer : %s", fn.snippet);
        }
        ImGui::EndChild();

        // PIED DE PAGE : Annuler / Valider
        ImGui::Separator();
        float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(avail_x - 230.0f);

        if (ImGui::Button("Annuler", ImVec2(100.0f, 0.0f)))
        {
            m_showExpressionModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        bool isValid = (strlen(m_exprAliasBuf) > 0) && (strlen(m_exprFormulaBuf) > 0);
        if (!isValid)
            ImGui::BeginDisabled();

        if (ImGui::Button("Valider", ImVec2(120.0f, 0.0f)))
        {
            std::string aliasStr(m_exprAliasBuf);
            std::string formulaStr(m_exprFormulaBuf);

            if (m_editingProjectionIndex >= 0 && m_editingProjectionIndex < static_cast<int>(m_projectionItems.size()))
            {
                auto &target = m_projectionItems[m_editingProjectionIndex];
                target.alias = aliasStr;
                target.sqlExpression = formulaStr;
                target.isAggregate = m_exprIsAggregate;
                target.isCustomExpression = true;
            }
            else
            {
                uintptr_t nextId = m_projectionItems.size() + 70000;
                m_projectionItems.push_back(VisualProjectionItem{
                    nextId, aliasStr, formulaStr, m_exprIsAggregate, true});
                m_hasProjectionNode = true;
            }

            m_showExpressionModal = false;
            ImGui::CloseCurrentPopup();
        }

        if (!isValid)
            ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}


void Application::openCtePreviewModal(const std::string &cteQueryName)
{
    m_previewCteName = cteQueryName;
    m_previewCteSql.clear();
    m_previewCteNodes.clear();
    m_previewCteLinks.clear();
    m_previewCtePredicates.clear();
    m_previewCteProjections.clear();

    if (m_ctePreviewContext)
    {
        ed::DestroyEditor(m_ctePreviewContext);
        m_ctePreviewContext = nullptr;
    }

    ed::Config cfg;
    cfg.SettingsFile = nullptr;
    cfg.NavigateButtonIndex = 1; // Clic droit maintenu pour déplacer le graphe
    cfg.ContextMenuButtonIndex = 2;
    cfg.EnableSmoothZoom = true;
    m_ctePreviewContext = ed::CreateEditor(&cfg);

    // Rend les lignes d'alignement (NodeSnap) totalement transparentes

    // Copie exacte du style du canevas principal
    ed::SetCurrentEditor(m_queryContext);
    ed::Style mainStyle = ed::GetStyle();

    ed::SetCurrentEditor(m_ctePreviewContext);
    ed::GetStyle() = mainStyle;
    ed::SetCurrentEditor(nullptr);

    auto it = std::find_if(m_savedQueries.begin(), m_savedQueries.end(),
                           [&cteQueryName](const SavedQuery &q)
                           { return q.name == cteQueryName; });

    if (it != m_savedQueries.end())
    {
        m_previewCteSql = it->sql;

        // 1. Sauvegarde temporaire de l'état du canevas principal
        auto backupNodes = m_visualQueryNodes;
        auto backupLinks = m_visualQueryLinks;
        auto backupPreds = m_visualQueryPredicates;
        auto backupProjs = m_projectionItems;
        bool backupHasProj = m_hasProjectionNode;
        bool backupCentering = m_needsQueryCentering;

        // 2. Chargement de la CTE via votre désérialiseur existant
        ed::SetCurrentEditor(m_ctePreviewContext);
        loadQueryIntoVisualDesigner(*it);
        ed::SetCurrentEditor(nullptr);

        // 3. Copie vers les vecteurs d'aperçu en avant-plan
        m_previewCteNodes = m_visualQueryNodes;
        m_previewCteLinks = m_visualQueryLinks;
        m_previewCtePredicates = m_visualQueryPredicates;
        m_previewCteProjections = m_projectionItems;

        for (auto &n : m_previewCteNodes)
            n.needsPositionInitialization = true;
        for (auto &p : m_previewCtePredicates)
            p.needsPositionInitialization = true;

        // 4. Restauration immédiate du canevas principal
        m_visualQueryNodes = std::move(backupNodes);
        m_visualQueryLinks = std::move(backupLinks);
        m_visualQueryPredicates = std::move(backupPreds);
        m_projectionItems = std::move(backupProjs);
        m_hasProjectionNode = backupHasProj;
        m_needsQueryCentering = backupCentering;
    }

    m_needsCtePreviewCentering = true;
    m_showCtePreviewModal = true;
}

void Application::renderCtePreviewModal()
{
    if (m_showCtePreviewModal)
    {
        ImGui::OpenPopup("Aperçu Graphique de la CTE");
        m_showCtePreviewModal = false; // Ouvre la popup une fois ; BeginPopupModal reste ouvert tant qu'on n'appelle pas CloseCurrentPopup()
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(980.0f, 640.0f), ImGuiCond_Appearing);

    bool isOpen = true;
    if (ImGui::BeginPopupModal("Aperçu Graphique de la CTE", &isOpen))
    {
        if (!isOpen)
        {
            ImGui::CloseCurrentPopup();
        }
        // Barre d'en-tête avec titre et bouton Fermer
        ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "%s  Sous-requête CTE : %s", ICON_LC_FILE_TEXT, m_previewCteName.c_str());

        float rightBtnWidth = 295.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - rightBtnWidth + 16.0f);
        if (ImGui::Button((std::string(ICON_LC_FOLDER_OPEN) + "  Ouvrir pour éditer").c_str(), ImVec2(185.0f, 26.0f)))
        {
            auto it = std::find_if(m_savedQueries.begin(), m_savedQueries.end(),
                                   [this](const SavedQuery &q)
                                   { return q.name == m_previewCteName; });
            if (it != m_savedQueries.end())
            {
                loadQueryIntoVisualDesigner(*it);
            }
            m_showCtePreviewModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 0.90f));
        if (ImGui::Button("Fermer", ImVec2(95.0f, 26.0f)))
        {
            m_showCtePreviewModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        if (m_previewCteNodes.empty())
        {
            ImGui::TextDisabled("Cette requête ne possède pas de graphe visuel enregistré. Aperçu SQL :");
            std::string sqlBuf = m_previewCteSql;
            ImGui::InputTextMultiline("##CtePreviewSqlOnly", sqlBuf.data(), sqlBuf.size() + 1, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
        }
        else
        {
            // Canevas isolé en avant-plan
            ImGui::BeginChild("CtePreviewCanvasChild", ImVec2(-1.0f, -1.0f), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

            ed::SetCurrentEditor(m_ctePreviewContext);
            ed::Begin("CtePreviewGraph");

            // 1. Rendu des nœuds Sources (Tables ou sous-CTE)
            for (auto &node : m_previewCteNodes)
            {
                if (node.needsPositionInitialization)
                {
                    node.needsPositionInitialization = false;
                    ed::SetNodePosition(node.id, node.position);
                }

                if (node.isCTE)
                    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.95f, 0.70f, 0.25f, 0.90f));

                ed::BeginNode(node.id);
                ImGui::BeginGroup();
                if (node.isCTE)
                    ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "%s  CTE: %s", ICON_LC_FILE_TEXT, node.datasetName.c_str());
                else
                    ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Table: %s", ICON_LC_DATABASE, node.datasetName.c_str());

                ImGui::TextDisabled("  Alias : %s", node.alias.c_str());
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                auto drawPreviewCol = [&](size_t idx, const std::string &cName, const std::string &cType)
                {
                    uintptr_t inPin = (node.id * 1000) + idx;
                    uintptr_t outPin = (node.id * 1000) + idx + 500;

                    ed::BeginPin(inPin, ed::PinKind::Input);
                    bool checked = node.selectedColumns.contains(idx);
                    ImGui::TextColored(checked ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(0.55f, 0.58f, 0.65f, 1.0f),
                                       "%s %s", checked ? "[x]" : "[ ]", cName.c_str());
                    ed::EndPin();

                    ImGui::SameLine(170.0f);
                    ImGui::TextDisabled(": %s", cType.c_str());

                    ImGui::SameLine(275.0f);
                    ed::BeginPin(outPin, ed::PinKind::Output);
                    ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 0.5f), ICON_LC_CHEVRON_RIGHT);
                    ed::EndPin();
                };

                if (node.isCTE)
                {
                    for (const auto &sq : m_savedQueries)
                    {
                        if (sq.name == node.datasetName)
                        {
                            for (size_t idx = 0; idx < sq.outputSchema.size(); ++idx)
                                drawPreviewCol(idx, sq.outputSchema[idx].name, sq.outputSchema[idx].dataType);
                            break;
                        }
                    }
                }
                else if (m_activeProject && m_activeProject->datasets().contains(node.datasetName))
                {
                    const auto &ds = m_activeProject->datasets().at(node.datasetName);
                    for (const auto &[idx, colObj] : ds.columns())
                        drawPreviewCol(idx, colObj.name(), colObj.type());
                }

                ImGui::EndGroup();
                ed::EndNode();
                if (node.isCTE)
                    ed::PopStyleColor();
            }

            // 2. Rendu des nœuds Filtres WHERE
            for (auto &pred : m_previewCtePredicates)
            {
                if (pred.needsPositionInitialization)
                {
                    pred.needsPositionInitialization = false;
                    ed::SetNodePosition(pred.id, pred.position);
                }
                ed::BeginNode(pred.id);
                ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "%s  Filtre WHERE", ICON_LC_FILTER);
                uintptr_t predInPin = pred.id + 500000;
                ed::BeginPin(predInPin, ed::PinKind::Input);
                ImGui::Text("%s  '%s'", pred.targetColumn.c_str(), pred.value.c_str());
                ed::EndPin();
                ed::EndNode();

                if (pred.connectedSrcPinId != 0)
                {
                    ed::Link(pred.id + 80000, pred.connectedSrcPinId, predInPin, ImVec4(1.0f, 1.0f, 1.0f, 0.8f), 2.0f);
                }
            }

            // 3. Rendu des Jointures avec badge central (positionné une seule fois)
            static uintptr_t s_lastCenteredPreviewId = 0;
            for (const auto &link : m_previewCteLinks)
            {
                const char *joinLabel = "INNER JOIN";
                ImVec4 joinColor = ImVec4(1.0f, 0.80f, 0.20f, 1.0f);

                if (link.type == cppsqlx::JoinType::LEFT)
                {
                    joinLabel = "LEFT JOIN";
                    joinColor = ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
                }
                else if (link.type == cppsqlx::JoinType::RIGHT)
                {
                    joinLabel = "RIGHT JOIN";
                    joinColor = ImVec4(0.75f, 0.55f, 0.95f, 1.0f);
                }
                else if (link.type == cppsqlx::JoinType::FULL)
                {
                    joinLabel = "FULL JOIN";
                    joinColor = ImVec4(0.95f, 0.55f, 0.35f, 1.0f);
                }

                // Positionnement calculé uniquement lors du centrage initial
                if (m_needsCtePreviewCentering)
                {
                    ImVec2 posA = ed::GetNodePosition(link.startPinId / 1000);
                    ImVec2 posB = ed::GetNodePosition(link.endPinId / 1000);
                    ImVec2 midPos((posA.x + posB.x) * 0.5f + 110.0f, (posA.y + posB.y) * 0.5f + 40.0f);
                    ed::SetNodePosition(link.centralNodeId, midPos);
                }

                ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.14f, 0.17f, 0.22f, 0.95f));
                ed::PushStyleColor(ed::StyleColor_NodeBorder, joinColor);

                ed::BeginNode(link.centralNodeId);
                ed::BeginPin(link.leftPinId, ed::PinKind::Input);
                ImGui::TextColored(joinColor, "%s", ICON_LC_GIT_MERGE);
                ed::EndPin();

                ImGui::SameLine();
                ImGui::TextColored(joinColor, "%s", joinLabel);

                ImGui::SameLine();
                ed::BeginPin(link.rightPinId, ed::PinKind::Output);
                ImGui::TextColored(joinColor, "%s", ICON_LC_CHEVRON_RIGHT);
                ed::EndPin();
                ed::EndNode();

                ed::PopStyleColor(2);

                ed::Link(link.id, link.startPinId, link.leftPinId, joinColor, 2.5f);
                ed::Link(link.id + 50000, link.rightPinId, link.endPinId, joinColor, 2.5f);
            }

            // 4. Rendu du nœud Projection SELECT (positionné à droite des tables)
            if (!m_previewCteProjections.empty())
            {
                if (m_needsCtePreviewCentering)
                {
                    float maxX = 400.0f;
                    float avgY = 100.0f;
                    for (const auto &n : m_previewCteNodes)
                    {
                        if (n.position.x + 350.0f > maxX)
                            maxX = n.position.x + 350.0f;
                        avgY = n.position.y;
                    }
                    ed::SetNodePosition(999999, ImVec2(maxX, avgY));
                }

                ed::BeginNode(999999);
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s  Projection SELECT", ICON_LC_LAYERS);
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                for (const auto &proj : m_previewCteProjections)
                {
                    ImGui::Text("%s", proj.sqlExpression.c_str());
                }
                ed::EndNode();
            }

            if (m_needsCtePreviewCentering)
            {
                ed::NavigateToContent(0.0f);
                m_needsCtePreviewCentering = false;
            }

            ed::End();
            ed::SetCurrentEditor(nullptr);
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
}


