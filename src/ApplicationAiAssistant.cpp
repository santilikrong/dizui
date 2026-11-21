#include "Application.hpp"
#include "IconsLucide.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace
{
    size_t curlWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        size_t totalBytes = size * nmemb;
        auto *str = static_cast<std::string *>(userp);
        str->append(static_cast<char *>(contents), totalBytes);
        return totalBytes;
    }

    // Extrait le premier bloc ```sql ... ``` ou ``` ... ``` de la réponse de l'IA
    std::string extractSqlCodeBlock(const std::string &markdownText)
    {
        size_t startFence = markdownText.find("```");
        if (startFence == std::string::npos)
            return "";

        size_t lineEnd = markdownText.find('\n', startFence);
        if (lineEnd == std::string::npos)
            return "";

        size_t endFence = markdownText.find("```", lineEnd + 1);
        if (endFence == std::string::npos)
            return "";

        std::string code = markdownText.substr(lineEnd + 1, endFence - (lineEnd + 1));
        while (!code.empty() && (code.back() == '\n' || code.back() == '\r' || code.back() == ' '))
            code.pop_back();
        return code;
    }
}

std::string Application::buildProjectContextForAi() const
{
    std::ostringstream ctx;
    ctx << "Tu es un assistant expert PostgreSQL et intégration de données intégré dans dizui Studio. "
        << "Réponds de façon concise en français. Lorsque tu proposes une requête SQL, encadre-la toujours dans un bloc ```sql ... ```.\n\n";

    if (m_aiIncludeSchemaContext && m_activeProject)
    {
        const auto &datasets = m_activeProject->datasets();
        if (!datasets.empty())
        {
            ctx << "### Tables disponibles dans le projet :\n";
            int count = 0;
            for (const auto &[dsName, ds] : datasets)
            {
                if (++count > 35) // Limite raisonnable pour ne pas saturer le contexte local
                {
                    ctx << "- ... (" << (datasets.size() - 35) << " autres tables)\n";
                    break;
                }
                ctx << "- " << dsName << " (";
                bool firstCol = true;
                for (const auto &[idx, col] : ds.columns())
                {
                    if (!firstCol)
                        ctx << ", ";
                    ctx << col.name() << ": " << col.type();
                    firstCol = false;
                }
                ctx << ")\n";
            }
            ctx << "\n";
        }

        if (!m_savedQueries.empty())
        {
            ctx << "### Requêtes enregistrées (utilisables comme CTE) :\n";
            for (const auto &sq : m_savedQueries)
            {
                ctx << "- " << sq.name << " (";
                for (size_t i = 0; i < sq.outputSchema.size(); ++i)
                {
                    if (i > 0)
                        ctx << ", ";
                    ctx << sq.outputSchema[i].name;
                }
                ctx << ")\n";
            }
            ctx << "\n";
        }
    }

    if (m_aiIncludeActiveSqlContext)
    {
        std::string activeSql(m_rawSqlEditorBuf.data());
        if (!activeSql.empty())
        {
            ctx << "### Requête SQL actuellement ouverte dans l'éditeur :\n```sql\n"
                << activeSql << "\n```\n\n";
        }
        if (!m_executionError.empty())
        {
            ctx << "### Dernière erreur d'exécution PostgreSQL :\n"
                << m_executionError << "\n\n";
        }
    }

    return ctx.str();
}

void Application::sendPromptToOllamaAsync(const std::string &userPrompt)
{
    if (userPrompt.empty() || m_isAiThinking.load())
        return;

    std::string systemContext = buildProjectContextForAi();
    std::string endpointUrl(m_aiOllamaUrlBuf.data());
    std::string modelName(m_aiModelNameBuf.data());

    // Construction du payload JSON conforme à l'API /api/chat d'Ollama
    nlohmann::json messagesArr = nlohmann::json::array();
    messagesArr.push_back({{"role", "system"}, {"content", systemContext}});

    {
        std::lock_guard<std::mutex> lock(m_aiChatMutex);
        m_aiChatHistory.push_back(AiChatMessage{true, userPrompt, ""});
        m_aiScrollToBottom = true;

        // Conserve les 6 derniers échanges pour maintenir le fil de discussion
        size_t startIdx = (m_aiChatHistory.size() > 7) ? (m_aiChatHistory.size() - 7) : 0;
        for (size_t i = startIdx; i < m_aiChatHistory.size(); ++i)
        {
            messagesArr.push_back({{"role", m_aiChatHistory[i].isUser ? "user" : "assistant"},
                                   {"content", m_aiChatHistory[i].text}});
        }
    }

    nlohmann::json payload = {
        {"model", modelName},
        {"messages", std::move(messagesArr)},
        {"stream", false}};
    std::string payloadStr = payload.dump();

    m_isAiThinking.store(true);

    std::thread([this, endpointUrl, payloadStr]()
                {
        std::string responseBody;
        std::string errorMsg;

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            errorMsg = "Impossible d'initialiser libcurl.";
        }
        else
        {
            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, endpointUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payloadStr.size()));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                errorMsg = std::string("Erreur de connexion Ollama : ") + curl_easy_strerror(res) +
                           " (Vérifiez qu'Ollama est démarré sur " + endpointUrl + ")";
            }
            else
            {
                long httpCode = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                if (httpCode >= 400)
                {
                    errorMsg = "Erreur HTTP Ollama (" + std::to_string(httpCode) + ") : " + responseBody;
                }
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        std::string assistantReply;
        if (!errorMsg.empty())
        {
            assistantReply = errorMsg;
        }
        else
        {
            try
            {
                auto respJson = nlohmann::json::parse(responseBody);
                if (respJson.contains("message") && respJson["message"].contains("content"))
                    assistantReply = respJson["message"]["content"].get<std::string>();
                else if (respJson.contains("response"))
                    assistantReply = respJson["response"].get<std::string>();
                else
                    assistantReply = "Réponse inattendue d'Ollama : " + responseBody;
            }
            catch (const std::exception &e)
            {
                assistantReply = std::string("Erreur de lecture JSON Ollama : ") + e.what();
            }
        }

        std::string extractedSql = extractSqlCodeBlock(assistantReply);

        {
            std::lock_guard<std::mutex> lock(m_aiChatMutex);
            m_aiChatHistory.push_back(AiChatMessage{false, std::move(assistantReply), std::move(extractedSql)});
            m_aiScrollToBottom = true;
        }

        m_isAiThinking.store(false); })
        .detach();
}

void Application::renderAiAssistantPanel()
{
    ImGui::Begin("Assistant IA (Ollama)");

    // 1. Barre de configuration compacte en haut
    ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.95f, 1.0f), "%s  Ollama", ICON_LC_CPU);
    ImGui::SameLine();
    ImGui::TextDisabled("Modèle :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(165.0f);
    ImGui::InputText("##OllamaModel", m_aiModelNameBuf.data(), m_aiModelNameBuf.size());

    ImGui::SameLine();
    ImGui::TextDisabled("URL :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##OllamaUrl", m_aiOllamaUrlBuf.data(), m_aiOllamaUrlBuf.size());

    ImGui::SameLine();
    ImGui::Checkbox("Schémas & CTE", &m_aiIncludeSchemaContext);
    ImGui::SameLine();
    ImGui::Checkbox("SQL & Erreurs actifs", &m_aiIncludeActiveSqlContext);

    ImGui::SameLine();
    if (ImGui::SmallButton((std::string(ICON_LC_TRASH) + " Vider").c_str()))
    {
        std::lock_guard<std::mutex> lock(m_aiChatMutex);
        m_aiChatHistory.clear();
    }

    ImGui::Separator();

    // 2. Zone défilante de l'historique des messages
    float inputBarHeight = 36.0f;
    float historyHeight = ImGui::GetContentRegionAvail().y - inputBarHeight - 6.0f;
    if (historyHeight < 60.0f)
        historyHeight = 60.0f;

    ImGui::BeginChild("AiChatHistoryScroll", ImVec2(-1.0f, historyHeight), true);
    {
        std::lock_guard<std::mutex> lock(m_aiChatMutex);

        if (m_aiChatHistory.empty())
        {
            ImGui::TextDisabled("Posez une question SQL (ex: 'Écris une requête qui joint t_employes et t_agences' ou 'Corrige l'erreur SQL').");
        }

        for (size_t i = 0; i < m_aiChatHistory.size(); ++i)
        {
            const auto &msg = m_aiChatHistory[i];
            ImGui::PushID(static_cast<int>(i));

            if (msg.isUser)
            {
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Vous :");
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(msg.text.c_str());
                ImGui::PopTextWrapPos();
            }
            else
            {
                ImGui::TextColored(ImVec4(0.65f, 0.45f, 0.95f, 1.0f), "Assistant IA :");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(msg.text.c_str());
                ImGui::PopTextWrapPos();
                // Boutons d'action directe si l'IA a généré un bloc SQL
                if (!msg.extractedSql.empty())
                {
                    ImGui::Spacing();
                    if (ImGui::SmallButton((std::string(ICON_LC_COPY) + "  Copier dans l'Éditeur SQL").c_str()))
                    {
                        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", msg.extractedSql.c_str());
                        addConsoleLog("Requête générée par l'IA copiée dans l'Éditeur SQL.", LogLevel::Info);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton((std::string(ICON_LC_PLAY) + "  Exécuter ce SQL").c_str()))
                    {
                        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", msg.extractedSql.c_str());
                        executeRawSqlQuery(msg.extractedSql);
                    }
                }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (m_isAiThinking.load())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "%s  Génération en cours avec %s...",
                               ICON_LC_LOADER, m_aiModelNameBuf.data());
        }
        if (m_aiScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_aiScrollToBottom = false;
        }
    }
    ImGui::EndChild();
    // 3. Barre de saisie en bas
    bool thinking = m_isAiThinking.load();
    if (thinking)
        ImGui::BeginDisabled();
    float sendBtnWidth = 115.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - sendBtnWidth - ImGui::GetStyle().ItemSpacing.x);
    bool enterPressed = ImGui::InputTextWithHint(
        "##AiPromptInput",
        "Demandez une requête SQL, une optimisation ou l'explication d'une erreur (Entrée pour envoyer)...",
        m_aiPromptBuf.data(),
        m_aiPromptBuf.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.30f, 0.80f, 0.90f));
    bool sendClicked = ImGui::Button((std::string(ICON_LC_SEND) + "  Envoyer").c_str(), ImVec2(sendBtnWidth, 0.0f));
    ImGui::PopStyleColor();
    if ((enterPressed || sendClicked) && m_aiPromptBuf[0] != '\0')
    {
        std::string prompt(m_aiPromptBuf.data());
        m_aiPromptBuf[0] = '\0';
        sendPromptToOllamaAsync(prompt);
    }
    if (thinking)
        ImGui::EndDisabled();
    ImGui::End();
}