#include "Application.hpp"
#include "IconsLucide.hpp"
#include "SqlEditorHelpers.hpp"
#include <imgui_internal.h>
#include <sstream>
#include <algorithm>

void Application::renderQueryTextEditor()
{
    ImGui::Spacing();

    bool hasConnection = (m_activeProject &&
                          m_activeProject->logicalConnections().contains(m_selectedGateway) &&
                          m_activeProject->logicalConnections().at(m_selectedGateway).environments().contains(m_selectedContext));
    bool hasSql = (strlen(m_rawSqlEditorBuf.data()) > 0);

    if (!hasConnection || !hasSql)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.30f, 0.90f));
    if (ImGui::Button((std::string(ICON_LC_PLAY) + "  Exécuter SQL (Ctrl+Entrée)").c_str(), ImVec2(220.0f, 28.0f)))
    {
        executeRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
    }
    ImGui::PopStyleColor();

    if (!hasConnection || !hasSql)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_LC_TRASH) + "  Effacer").c_str(), ImVec2(100.0f, 28.0f)))
    {
        m_rawSqlEditorBuf.fill('\0');
        m_showSqlAutocomplete = false;
    }

    ImGui::SameLine();
    if (!hasSql)
        ImGui::BeginDisabled();
    if (ImGui::Button((std::string(ICON_LC_ALIGN_LEFT) + "  Formater").c_str(), ImVec2(115.0f, 28.0f)))
    {
        std::string formatted = dizui::sql::formatRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", formatted.c_str());
    }
    if (!hasSql)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_LC_COPY) + "  Copier depuis le Designer").c_str(), ImVec2(215.0f, 28.0f)))
    {
        std::string compiled = compileVisualGraphToSelectQuery().toSQL();
        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", compiled.c_str());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("  |  Cible : %s [%s]  |  Autocomplétion : Ctrl+Espace",
                        m_selectedGateway.empty() ? "Aucune" : m_selectedGateway.c_str(),
                        m_selectedContext.empty() ? "Aucun" : m_selectedContext.c_str());

    ImGui::Separator();
    ImGui::Spacing();

    // Zone d'édition plein écran
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    if (availSize.y < 100.0f)
        availSize.y = 100.0f;

    int prevCursorPos = m_sqlEditorCursorPos;
    dizui::sql::SqlEditorCallbackUserData cbData{
        &m_sqlEditorCursorPos,
        &m_sqlPendingInsertion,
        m_sqlReplaceStartPos,
        m_sqlReplaceEndPos};

    ImGuiInputTextFlags editorFlags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool textModified = ImGui::InputTextMultiline(
        "##RawSqlTextEditor",
        m_rawSqlEditorBuf.data(),
        m_rawSqlEditorBuf.size(),
        availSize,
        editorFlags,
        dizui::sql::sqlTextEditorCallback,
        &cbData);
    ImGui::PopStyleColor();

    bool isEditorFocused = ImGui::IsItemFocused();

    // Raccourci Ctrl + Entrée pour exécuter
    if (isEditorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
    {
        if (hasConnection && hasSql)
        {
            m_showSqlAutocomplete = false;
            executeRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
        }
    }

    // Raccourci Ctrl + Espace pour ouvrir manuellement l'autocomplétion
    if (isEditorFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        m_showSqlAutocomplete = true;
        m_sqlAutocompleteSelectedIdx = 0;
    }

    // Déclenchement automatique dès que l'utilisateur tape un caractère ou un point
    if (textModified && isEditorFocused)
    {
        m_showSqlAutocomplete = true;
        m_sqlAutocompleteSelectedIdx = 0;
    }

    // Superposition de la coloration syntaxique
    ImVec2 editorMin = ImGui::GetItemRectMin();
    ImVec2 editorMax = ImGui::GetItemRectMax();
    ImVec2 framePad = ImGui::GetStyle().FramePadding;

    float scrollX = 0.0f;
    float scrollY = 0.0f;
    if (ImGuiWindow *childWin = ImGui::FindWindowByName("##RawSqlTextEditor"))
    {
        scrollX = childWin->Scroll.x;
        scrollY = childWin->Scroll.y;
    }

    ImVec2 textStartPos(editorMin.x + framePad.x - scrollX, editorMin.y + framePad.y - scrollY);
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    drawList->PushClipRect(
        ImVec2(editorMin.x + framePad.x, editorMin.y + framePad.y),
        ImVec2(editorMax.x - framePad.x, editorMax.y - framePad.y),
        true);
    dizui::sql::renderSqlSyntaxHighlightOverlay(drawList, textStartPos, m_rawSqlEditorBuf.data(), ImGui::GetFont(), ImGui::GetFontSize());
    drawList->PopClipRect();

    // =========================================================================
    // LOGIQUE D'ANALYSE CONTEXTUELLE ET POPUP D'AUTOCOMPLÉTION
    // =========================================================================
    if (m_showSqlAutocomplete)
    {
        std::string fullText(m_rawSqlEditorBuf.data());
        int cursor = std::max(0, std::min(m_sqlEditorCursorPos, static_cast<int>(fullText.size())));

        // Extraction du jeton avant le curseur (ex: "emp.mat" -> qualifier="emp", prefix="mat")
        int tokenStart = cursor;
        while (tokenStart > 0 && (std::isalnum(static_cast<unsigned char>(fullText[tokenStart - 1])) ||
                                  fullText[tokenStart - 1] == '_' || fullText[tokenStart - 1] == '.'))
        {
            --tokenStart;
        }

        std::string currentToken = fullText.substr(tokenStart, cursor - tokenStart);
        std::string qualifier = "";
        std::string prefix = currentToken;
        int replaceStart = tokenStart;

        size_t dotPos = currentToken.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            qualifier = currentToken.substr(0, dotPos);
            prefix = currentToken.substr(dotPos + 1);
            replaceStart = tokenStart + static_cast<int>(dotPos) + 1;
        }

        auto toLower = [](const std::string &s)
        {
            std::string low = s;
            for (char &c : low)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return low;
        };

        std::string lowPrefix = toLower(prefix);
        std::string lowQual = toLower(qualifier);

        // Si aucun point et préfixe vide (sauf ouverture manuelle Ctrl+Espace), on ferme
        if (dotPos == std::string::npos && prefix.empty() && !(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)))
        {
            m_showSqlAutocomplete = false;
        }
        else
        {
            // Résolution des alias présents dans la requête SQL (ex: "t_employes AS emp" -> emp => t_employes)
            std::string resolvedSourceName = qualifier;
            if (!qualifier.empty())
            {
                std::istringstream iss(fullText);
                std::vector<std::string> words;
                std::string w;
                while (iss >> w)
                {
                    // Nettoyage ponctuation basique
                    while (!w.empty() && (w.back() == ',' || w.back() == ')' || w.back() == ';'))
                        w.pop_back();
                    words.push_back(w);
                }

                for (size_t i = 0; i + 1 < words.size(); ++i)
                {
                    if (toLower(words[i]) == "as" && i > 0 && toLower(words[i + 1]) == lowQual)
                    {
                        resolvedSourceName = words[i - 1];
                        break;
                    }
                    if (i + 1 < words.size() && toLower(words[i + 1]) == lowQual &&
                        toLower(words[i]) != "from" && toLower(words[i]) != "join" && toLower(words[i]) != "select")
                    {
                        resolvedSourceName = words[i];
                        break;
                    }
                }
            }

            // Suppression éventuelle du schéma "public." dans resolvedSourceName
            std::string shortSourceName = resolvedSourceName;
            size_t schemaDot = shortSourceName.find_last_of('.');
            if (schemaDot != std::string::npos)
                shortSourceName = shortSourceName.substr(schemaDot + 1);

            std::vector<SqlCompletionCandidate> candidates;

            // Cas A : Un qualificateur est tapé (ex: "t_employes." ou "emp.") -> Colonnes de cette source uniquement
            if (!qualifier.empty())
            {
                // 1. Recherche dans les Tables physiques
                if (m_activeProject)
                {
                    for (const auto &[dsName, dsObj] : m_activeProject->datasets())
                    {
                        if (toLower(dsName) == toLower(resolvedSourceName) || toLower(dsName) == toLower(shortSourceName))
                        {
                            for (const auto &[idx, col] : dsObj.columns())
                            {
                                if (lowPrefix.empty() || toLower(col.name()).find(lowPrefix) != std::string::npos)
                                {
                                    candidates.push_back({col.name(), dsName + " (" + col.type() + ")", false});
                                }
                            }
                        }
                    }
                }

                // 2. Recherche dans les Requêtes enregistrées (CTE)
                for (const auto &sq : m_savedQueries)
                {
                    if (toLower(sq.name) == toLower(resolvedSourceName))
                    {
                        for (const auto &col : sq.outputSchema)
                        {
                            if (lowPrefix.empty() || toLower(col.name).find(lowPrefix) != std::string::npos)
                            {
                                candidates.push_back({col.name, "CTE " + sq.name + " (" + col.dataType + ")", false});
                            }
                        }
                    }
                }
            }
            // Cas B : Pas de point -> Tables, CTE et Colonnes correspondantes
            else
            {
                if (m_activeProject)
                {
                    for (const auto &[dsName, dsObj] : m_activeProject->datasets())
                    {
                        if (lowPrefix.empty() || toLower(dsName).find(lowPrefix) != std::string::npos)
                        {
                            candidates.push_back({dsName, "Table", true});
                        }
                        for (const auto &[idx, col] : dsObj.columns())
                        {
                            if (!lowPrefix.empty() && toLower(col.name()).find(lowPrefix) != std::string::npos)
                            {
                                candidates.push_back({col.name(), dsName + " (" + col.type() + ")", false});
                            }
                        }
                    }
                }
                for (const auto &sq : m_savedQueries)
                {
                    if (lowPrefix.empty() || toLower(sq.name).find(lowPrefix) != std::string::npos)
                    {
                        candidates.push_back({sq.name, "CTE / Requête", true});
                    }
                }
            }
            if (candidates.empty())
            {
                m_showSqlAutocomplete = false;
            }
            else
            {
                int totalCandidates = static_cast<int>(candidates.size());
                if (m_sqlAutocompleteSelectedIdx >= totalCandidates)
                    m_sqlAutocompleteSelectedIdx = 0;

                // Calcul de la position écran du curseur texte pour ancrer le popup juste dessous
                std::string textBeforeCursor = fullText.substr(0, cursor);
                int lineCount = 0;
                size_t lastNewline = textBeforeCursor.find_last_of('\n');
                for (char c : textBeforeCursor)
                {
                    if (c == '\n')
                        ++lineCount;
                }
                std::string currentLineText = (lastNewline == std::string::npos)
                                                  ? textBeforeCursor
                                                  : textBeforeCursor.substr(lastNewline + 1);

                float cursorOffsetX = ImGui::CalcTextSize(currentLineText.c_str()).x;
                float cursorOffsetY = (lineCount + 1) * ImGui::GetTextLineHeight();

                ImVec2 popupPos(textStartPos.x + cursorOffsetX, textStartPos.y + cursorOffsetY + 4.0f);
                ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(340.0f, std::min(190.0f, totalCandidates * 24.0f + 16.0f)));

                ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing;

                if (ImGui::Begin("##SqlAutocompletePopup", nullptr, popupFlags))
                {
                    // Navigation clavier Haut / Bas / Échap / Validation par Entrée
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                        m_sqlAutocompleteSelectedIdx = (m_sqlAutocompleteSelectedIdx + 1) % totalCandidates;
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                        m_sqlAutocompleteSelectedIdx = (m_sqlAutocompleteSelectedIdx - 1 + totalCandidates) % totalCandidates;
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                        m_showSqlAutocomplete = false;

                    bool acceptCurrent = (!ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false));

                    for (int idx = 0; idx < totalCandidates; ++idx)
                    {
                        const auto &cand = candidates[idx];
                        bool isSel = (idx == m_sqlAutocompleteSelectedIdx);

                        ImGui::PushID(idx);
                        const char *icon = cand.isTableOrCte ? ICON_LC_DATABASE : ICON_LC_COLUMNS;
                        ImVec4 iconCol = cand.isTableOrCte ? ImVec4(0.35f, 0.75f, 1.0f, 1.0f) : ImVec4(0.45f, 0.85f, 0.55f, 1.0f);

                        ImGui::TextColored(iconCol, "%s", icon);
                        ImGui::SameLine();

                        if (ImGui::Selectable(cand.label.c_str(), isSel, ImGuiSelectableFlags_AllowDoubleClick) || (isSel && acceptCurrent))
                        {
                            m_sqlPendingInsertion = cand.label;
                            m_sqlReplaceStartPos = replaceStart;
                            m_sqlReplaceEndPos = cursor;
                            m_showSqlAutocomplete = false;
                        }

                        if (isSel)
                            ImGui::SetScrollHereY();

                        ImGui::SameLine(190.0f);
                        ImGui::TextDisabled("%s", cand.detail.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::End();
            }
        }
    }
}

void Application::executeRawSqlQuery(const std::string &sqlText)
{
    m_executionError.clear();
    m_queryResultHeaders.clear();
    m_queryResultRows.clear();

    if (!m_activeProject || !m_activeProject->logicalConnections().contains(m_selectedGateway))
    {
        m_executionError = "Aucune passerelle logique sélectionnée.";
        m_showQueryResultsPanel = true;
        m_focusQueryResultsTab = true;
        return;
    }

    const auto &gw = m_activeProject->logicalConnections().at(m_selectedGateway);
    if (!gw.environments().contains(m_selectedContext))
    {
        m_executionError = "Environnement cible introuvable dans la passerelle.";
        m_showQueryResultsPanel = true;
        m_focusQueryResultsTab = true;
        return;
    }

    const auto &phys = gw.environments().at(m_selectedContext);
    cppsqlx::Connection conn(phys.toConnectionString());
    auto connRes = conn.connect();
    if (!connRes.has_value())
    {
        m_executionError = "Échec de connexion PostgreSQL : " + connRes.error();
        addConsoleLog(m_executionError, LogLevel::Error);
        m_showQueryResultsPanel = true;
        m_focusQueryResultsTab = true;
        return;
    }

    addConsoleLog("Exécution SQL directe : " + sqlText, LogLevel::Info);

    PGresult *res = PQexec(conn.get(), sqlText.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status == PGRES_TUPLES_OK)
    {
        int nFields = PQnfields(res);
        int nTuples = PQntuples(res);

        m_queryResultHeaders.reserve(nFields);
        for (int c = 0; c < nFields; ++c)
        {
            m_queryResultHeaders.push_back(PQfname(res, c));
        }

        m_queryResultRows.reserve(nTuples);
        for (int r = 0; r < nTuples; ++r)
        {
            std::vector<std::string> row;
            row.reserve(nFields);
            for (int c = 0; c < nFields; ++c)
            {
                if (PQgetisnull(res, r, c))
                    row.push_back("NULL");
                else
                    row.push_back(PQgetvalue(res, r, c));
            }
            m_queryResultRows.push_back(std::move(row));
        }

        addConsoleLog("Requête exécutée avec succès (" + std::to_string(nTuples) + " ligne(s)).", LogLevel::Success);
    }
    else if (status == PGRES_COMMAND_OK)
    {
        std::string affected = PQcmdTuples(res);
        m_queryResultHeaders = {"Statut", "Lignes affectées"};
        m_queryResultRows = {{"OK", affected.empty() ? "0" : affected}};
        addConsoleLog("Commande SQL exécutée avec succès (" + (affected.empty() ? "0" : affected) + " ligne(s) affectée(s)).", LogLevel::Success);
    }
    else
    {
        m_executionError = PQerrorMessage(conn.get());
        addConsoleLog("Erreur SQL : " + m_executionError, LogLevel::Error);
    }

    PQclear(res);

    // Bascule automatiquement sur l'onglet de résultats dans le dock inférieur
    m_showQueryResultsPanel = true;
    m_focusQueryResultsTab = true;
}

// Explore récursivement les dépendances d'une SavedQuery en parcourant son graphe ou son SQL
void Application::collectCteDependenciesRecursive(
    const std::string &cteName,
    std::unordered_set<std::string> &visited,
    std::vector<CteChainStep> &outSteps,
    int currentDepth) const
{
    if (visited.contains(cteName))
        return;
    visited.insert(cteName);

    const SavedQuery *targetSq = nullptr;
    for (const auto &sq : m_savedQueries)
    {
        if (sq.name == cteName)
        {
            targetSq = &sq;
            break;
        }
    }

    if (!targetSq)
        return;

    for (const auto &candidate : m_savedQueries)
    {
        if (candidate.name == cteName)
            continue;

        size_t pos = targetSq->sql.find(candidate.name);
        while (pos != std::string::npos)
        {
            bool leftOk = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(targetSq->sql[pos - 1])) && targetSq->sql[pos - 1] != '_');
            size_t endPos = pos + candidate.name.size();
            bool rightOk = (endPos >= targetSq->sql.size()) || (!std::isalnum(static_cast<unsigned char>(targetSq->sql[endPos])) && targetSq->sql[endPos] != '_');

            if (leftOk && rightOk)
            {
                collectCteDependenciesRecursive(candidate.name, visited, outSteps, currentDepth + 1);
                break;
            }
            pos = targetSq->sql.find(candidate.name, pos + 1);
        }
    }

    outSteps.push_back(CteChainStep{cteName, cteName, false, currentDepth, nullptr});
}

void Application::renderCteStackEditor()
{
    ImGui::Spacing();

    // Barre d'outils supérieure
    bool hasNodes = !m_visualQueryNodes.empty();
    bool hasConnection = (m_activeProject &&
                          m_activeProject->logicalConnections().contains(m_selectedGateway) &&
                          m_activeProject->logicalConnections().at(m_selectedGateway).environments().contains(m_selectedContext));

    if (!hasNodes || !hasConnection)
        ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.30f, 0.90f));
    if (ImGui::Button((std::string(ICON_LC_PLAY) + "  Exécuter le Pipeline Complet").c_str(), ImVec2(230.0f, 28.0f)))
    {
        std::string fullSql = compileVisualGraphToSelectQuery().toSQL();
        executeRawSqlQuery(fullSql);
    }
    ImGui::PopStyleColor();

    if (!hasNodes || !hasConnection)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (!hasNodes)
        ImGui::BeginDisabled();
    if (ImGui::Button((std::string(ICON_LC_FILE_TEXT) + "  Voir le SQL Compilé").c_str(), ImVec2(185.0f, 28.0f)))
    {
        std::string fullSql = compileVisualGraphToSelectQuery().toSQL();
        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", fullSql.c_str());
        addConsoleLog("SQL compilé copié dans l'éditeur de texte.", LogLevel::Info);
    }
    if (!hasNodes)
        ImGui::EndDisabled();

    // Sélecteur d'orientation : Horizontal (Gauche -> Droite) / Vertical (Haut -> Bas)
    // Sélecteur d'orientation : Horizontal / Vertical
    ImGui::SameLine();
    ImGui::TextDisabled("  |  Disposition :");
    ImGui::SameLine();

    bool wasHoriz = m_cteStackHorizontal;

    if (wasHoriz)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.52f, 0.85f, 0.90f));
    if (ImGui::Button("Horizontale", ImVec2(105.0f, 28.0f)))
        m_cteStackHorizontal = true;
    if (wasHoriz)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    if (!wasHoriz)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.52f, 0.85f, 0.90f));
    if (ImGui::Button("Verticale", ImVec2(95.0f, 28.0f)))
        m_cteStackHorizontal = false;
    if (!wasHoriz)
        ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    // Zone défilante principale
    const char *scrollChildName = m_cteStackHorizontal ? "CteStackScrollRegion_H" : "CteStackScrollRegion_V";
    ImGuiWindowFlags scrollFlags = m_cteStackHorizontal ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None;
    ImGui::BeginChild(scrollChildName, ImVec2(-1.0f, -1.0f), false, scrollFlags);

    // Réception Drag & Drop directe
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_CTE_QUERY_NODE"))
        {
            std::string cteName(static_cast<const char *>(payload->Data));
            uintptr_t newId = 100;
            for (const auto &n : m_visualQueryNodes)
                if (n.id >= newId)
                    newId = n.id + 1;

            std::string alias = "cte_" + std::to_string(newId);
            m_visualQueryNodes.push_back(VisualQueryNode{
                newId, cteName, ImVec2(150.0f, 150.0f), true, alias, {}, true});
        }
        else if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DND_DATASET_NODE"))
        {
            std::string dsName(static_cast<const char *>(payload->Data));
            uintptr_t newId = 100;
            for (const auto &n : m_visualQueryNodes)
                if (n.id >= newId)
                    newId = n.id + 1;

            std::string alias = dsName;
            int suffix = 2;
            while (std::any_of(m_visualQueryNodes.begin(), m_visualQueryNodes.end(),
                               [&alias](const VisualQueryNode &n)
                               { return n.alias == alias; }))
            {
                alias = dsName + "_" + std::to_string(suffix++);
            }
            m_visualQueryNodes.push_back(VisualQueryNode{
                newId, dsName, ImVec2(150.0f, 150.0f), true, alias, {}, false});
        }
        ImGui::EndDragDropTarget();
    }

    if (m_visualQueryNodes.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("   Aucune CTE ou table dans le pipeline.");
        ImGui::TextDisabled("   Glissez-déposez des requêtes enregistrées ou des tables ici (ou depuis le Query Visual Designer).");
        ImGui::EndChild();
        return;
    }

    // =========================================================================
    // 1. CONSTRUCTION DE LA CHAÎNE COMPLÈTE (CTE AMONT -> CTE FINALES -> TABLES)
    // =========================================================================
    std::vector<CteChainStep> fullPipeline;
    std::unordered_set<std::string> visitedCtes;

    for (const auto &node : m_visualQueryNodes)
    {
        if (node.isCTE)
        {
            size_t beforeCount = fullPipeline.size();
            collectCteDependenciesRecursive(node.datasetName, visitedCtes, fullPipeline, 0);

            if (fullPipeline.size() > beforeCount)
            {
                fullPipeline.back().isRootNode = true;
                fullPipeline.back().alias = node.alias;
                fullPipeline.back().canvasNode = &node;
            }
        }
    }

    for (const auto &node : m_visualQueryNodes)
    {
        if (!node.isCTE)
        {
            fullPipeline.push_back(CteChainStep{node.datasetName, node.alias, true, 0, &node});
        }
    }

    float availW = ImGui::GetContentRegionAvail().x;
    float cardWidth = m_cteStackHorizontal ? 245.0f : std::min(680.0f, availW - 20.0f);
    float leftMargin = m_cteStackHorizontal ? 8.0f : std::max(0.0f, (availW - cardWidth) * 0.5f);
    float startTopY = ImGui::GetCursorPosY();

    // =========================================================================
    // 2. RENDU DE L'EMPILEMENT COMPACT (HAUTEUR AJUSTÉE AU NOMBRE DE CHAMPS)
    // =========================================================================
    for (int stepIdx = 0; stepIdx < int(fullPipeline.size()); ++stepIdx)
    {
        const auto &step = fullPipeline[stepIdx];
        bool isCteStep = (step.canvasNode == nullptr || step.canvasNode->isCTE);

        if (m_cteStackHorizontal)
        {
            ImGui::SetCursorPosY(startTopY);
            if (stepIdx == 0)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftMargin);
        }
        else
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftMargin);
        }

        ImVec4 borderCol = isCteStep
                               ? (step.isRootNode ? ImVec4(0.95f, 0.70f, 0.25f, 0.90f) : ImVec4(0.75f, 0.55f, 0.25f, 0.65f))
                               : ImVec4(0.30f, 0.75f, 1.0f, 0.85f);
        ImVec4 headerCol = isCteStep ? ImVec4(0.95f, 0.70f, 0.25f, 1.0f) : ImVec4(0.30f, 0.75f, 1.0f, 1.0f);

        const SavedQuery *cteRef = nullptr;
        size_t colCount = 0;
        if (isCteStep)
        {
            for (const auto &sq : m_savedQueries)
            {
                if (sq.name == step.queryName)
                {
                    cteRef = &sq;
                    colCount = sq.outputSchema.size();
                    break;
                }
            }
        }
        else if (m_activeProject && m_activeProject->datasets().contains(step.queryName))
        {
            colCount = m_activeProject->datasets().at(step.queryName).columns().size();
        }

        // Calcul dynamique de la hauteur selon le nombre exact de colonnes
        size_t visibleCols = std::min<size_t>(colCount, 14);
        float rowStep = (step.isRootNode || !isCteStep) ? 24.0f : 20.0f;
        float cardHeight = (colCount == 0) ? 85.0f : (72.0f + float(visibleCols) * rowStep);

        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, step.isRootNode ? 1.8f : 1.2f);

        std::string cardChildId = "PipelineStepCard_" + std::to_string(stepIdx);
        ImGui::BeginChild(cardChildId.c_str(), ImVec2(cardWidth, cardHeight), true);

        // --- En-tête compact ---
        std::string shortTitle = step.queryName;
        if (m_cteStackHorizontal && shortTitle.length() > 16)
            shortTitle = shortTitle.substr(0, 14) + "..";

        if (isCteStep)
            ImGui::TextColored(headerCol, "%s %s", ICON_LC_FILE_TEXT, shortTitle.c_str());
        else
            ImGui::TextColored(headerCol, "%s %s", ICON_LC_DATABASE, shortTitle.c_str());

        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("%s : %s", isCteStep ? "CTE" : "Table", step.queryName.c_str());

        // Boutons d'action en haut à droite de la carte
        if (isCteStep && cteRef != nullptr)
        {
            ImGui::SameLine(cardWidth - 62.0f);
            ImGui::PushID(("eye_" + std::to_string(stepIdx)).c_str());
            if (ImGui::SmallButton(ICON_LC_EYE))
            {
                m_pendingCtePreviewName = step.queryName;
            }
            ImGui::SetItemTooltip("Aperçu graphique de '%s'", step.queryName.c_str());
            ImGui::PopID();

            ImGui::SameLine();

            if (!hasConnection)
                ImGui::BeginDisabled();
            ImGui::PushID(("play_" + std::to_string(stepIdx)).c_str());
            if (ImGui::SmallButton(ICON_LC_PLAY))
            {
                executeRawSqlQuery(cteRef->sql);
            }
            ImGui::SetItemTooltip("Exécuter '%s' seule", step.queryName.c_str());
            ImGui::PopID();
            if (!hasConnection)
                ImGui::EndDisabled();
        }

        ImGui::TextDisabled("Alias : %s", step.alias.c_str());
        ImGui::Separator();

        // --- Liste des colonnes ---
        if (isCteStep && cteRef != nullptr)
        {
            for (size_t cIdx = 0; cIdx < cteRef->outputSchema.size(); ++cIdx)
            {
                const auto &col = cteRef->outputSchema[cIdx];
                if (step.isRootNode && step.canvasNode != nullptr)
                {
                    VisualQueryNode *mutableNode = const_cast<VisualQueryNode *>(step.canvasNode);
                    ImGui::PushID(int(cIdx));
                    bool isSelected = mutableNode->selectedColumns.contains(cIdx);
                    if (ImGui::Checkbox("##colChk", &isSelected))
                    {
                        std::string qualExpr = mutableNode->alias.empty() ? col.name : (mutableNode->alias + "." + col.name);
                        std::string defAlias = mutableNode->alias.empty() ? col.name : (mutableNode->alias + "_" + col.name);
                        if (isSelected)
                        {
                            mutableNode->selectedColumns.insert(cIdx);
                            auto it = std::find_if(m_projectionItems.begin(), m_projectionItems.end(),
                                                   [&qualExpr](const VisualProjectionItem &p)
                                                   { return !p.isCustomExpression && p.sqlExpression == qualExpr; });
                            if (it == m_projectionItems.end())
                            {
                                uintptr_t nextId = m_projectionItems.size() + 70000;
                                m_projectionItems.push_back(VisualProjectionItem{
                                    nextId, defAlias, qualExpr, false, false, mutableNode->id});
                            }
                            m_hasProjectionNode = true;
                        }
                        else
                        {
                            mutableNode->selectedColumns.erase(cIdx);
                            std::erase_if(m_projectionItems,
                                          [&qualExpr](const VisualProjectionItem &p)
                                          { return !p.isCustomExpression && p.sqlExpression == qualExpr; });
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(col.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetItemTooltip("Type : %s", col.dataType.c_str());
                    ImGui::PopID();
                }
                else
                {
                    ImGui::TextDisabled(" - %s", col.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetItemTooltip("Type : %s", col.dataType.c_str());
                }
            }
        }
        else if (!isCteStep && m_activeProject && m_activeProject->datasets().contains(step.queryName))
        {
            const auto &ds = m_activeProject->datasets().at(step.queryName);
            VisualQueryNode *mutableNode = const_cast<VisualQueryNode *>(step.canvasNode);
            for (const auto &[cIdx, colObj] : ds.columns())
            {
                ImGui::PushID(int(cIdx));
                bool isSelected = mutableNode && mutableNode->selectedColumns.contains(cIdx);
                if (mutableNode && ImGui::Checkbox("##colChkTbl", &isSelected))
                {
                    std::string qualExpr = mutableNode->alias.empty() ? colObj.name() : (mutableNode->alias + "." + colObj.name());
                    std::string defAlias = mutableNode->alias.empty() ? colObj.name() : (mutableNode->alias + "_" + colObj.name());
                    if (isSelected)
                    {
                        mutableNode->selectedColumns.insert(cIdx);
                        m_projectionItems.push_back(VisualProjectionItem{
                            m_projectionItems.size() + 70000, defAlias, qualExpr, false, false, mutableNode->id});
                        m_hasProjectionNode = true;
                    }
                    else
                    {
                        mutableNode->selectedColumns.erase(cIdx);
                        std::erase_if(m_projectionItems,
                                      [&qualExpr](const VisualProjectionItem &p)
                                      { return !p.isCustomExpression && p.sqlExpression == qualExpr; });
                    }
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(colObj.name().c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("Type : %s", colObj.type().c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); // Fin de PipelineStepCard
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        // Connecteur vers l'étape suivante
        if (m_cteStackHorizontal)
        {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::SetCursorPosY(startTopY + (cardHeight * 0.5f) - 10.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.80f, 0.90f), "%s", ICON_LC_CHEVRON_RIGHT);
            ImGui::SameLine(0.0f, 8.0f);
        }
        else
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftMargin + (cardWidth * 0.5f) - 10.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.80f, 0.90f), "%s", ICON_LC_ARROW_DOWN);
        }
    }
    // =========================================================================
    // 3. BLOC PROJECTION FINALE COMPACT (HAUTEUR AJUSTÉE AUX COLONNES C1..Cn)
    // =========================================================================
    float projWidth = m_cteStackHorizontal ? 310.0f : cardWidth;
    size_t visibleProjs = std::min<size_t>(m_projectionItems.size(), 14);
    float projHeight = m_projectionItems.empty() ? 85.0f : (68.0f + float(visibleProjs) * 26.0f);
    if (m_cteStackHorizontal)
    {
        ImGui::SetCursorPosY(startTopY);
    }
    else
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftMargin);
    }
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.85f, 0.45f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.8f);
    ImGui::BeginChild("FinalProjectionCard", ImVec2(projWidth, projHeight), true);
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s  PROJECTION (%d)",
                       ICON_LC_LAYERS, int(m_projectionItems.size()));
    ImGui::Separator();
    if (m_projectionItems.empty())
    {
        ImGui::TextDisabled("Cochez des colonnes pour C1, C2...");
    }
    else
    {
        int projRemoveIdx = -1;
        for (int pIdx = 0; pIdx < int(m_projectionItems.size()); ++pIdx)
        {
            auto &item = m_projectionItems[pIdx];
            ImGui::PushID(pIdx + 50000);
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "C%d", pIdx + 1);
            ImGui::SameLine(34.0f);
            if (m_cteStackHorizontal)
            {
                std::string shortExpr = item.sqlExpression;
                if (shortExpr.length() > 16)
                    shortExpr = shortExpr.substr(0, 14) + "..";
                ImGui::TextUnformatted(shortExpr.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", item.sqlExpression.c_str());
                ImGui::SameLine(155.0f);
                ImGui::SetNextItemWidth(105.0f);
            }
            else
            {
                ImGui::TextUnformatted(item.sqlExpression.c_str());
                ImGui::SameLine(320.0f);
                ImGui::TextDisabled("AS");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180.0f);
            }
            std::array<char, 128> outAliasBuf{};
            snprintf(outAliasBuf.data(), outAliasBuf.size(), "%s", item.alias.c_str());
            if (ImGui::InputText("##ProjOutAlias", outAliasBuf.data(), outAliasBuf.size()))
            {
                item.alias = outAliasBuf.data();
            }
            ImGui::SameLine(projWidth - 34.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
            if (ImGui::SmallButton(ICON_LC_TRASH))
                projRemoveIdx = pIdx;
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (projRemoveIdx >= 0)
        {
            m_projectionItems.erase(m_projectionItems.begin() + projRemoveIdx);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    if (m_cteStackHorizontal)
    {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::Dummy(ImVec2(8.0f, 1.0f));
    }
    else
    {
        ImGui::Spacing();
    }
    ImGui::EndChild(); // Fin de CteStackScrollRegion
    if (!m_pendingCtePreviewName.empty())
    {
        std::string target = m_pendingCtePreviewName;
        m_pendingCtePreviewName.clear();
        openCtePreviewModal(target);
    }
}


