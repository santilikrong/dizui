#include "Application.hpp"
#include <Localization.hpp>
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
    ctx << tr("ai.system_prompt") << "\n\n";

    if (m_aiIncludeSchemaContext && m_activeProject)
    {
        const auto &datasets = m_activeProject->datasets();
        if (!datasets.empty())
        {
            ctx << "### " << tr("ai.ctx_tables_header") << "\n";
            int count = 0;
            for (const auto &[dsName, ds] : datasets)
            {
                if (++count > 35) // Limite raisonnable pour ne pas saturer le contexte local
                {
                    ctx << "- ... (" << (datasets.size() - 35) << " " << tr("ai.ctx_more_tables") << ")\n";
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
            ctx << "### " << tr("ai.ctx_saved_queries_header") << "\n";
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
            ctx << "### " << tr("ai.ctx_active_sql_header") << "\n```sql\n"
                << activeSql << "\n```\n\n";
        }
        if (!m_executionError.empty())
        {
            ctx << "### " << tr("ai.ctx_last_error_header") << "\n"
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

    // Capture des chaînes traduites sur le thread UI avant de lancer le thread réseau
    std::string errCurlInit = tr("ai.err_curl_init");
    std::string errConnPrefix = tr("ai.err_conn_prefix");
    std::string errConnHint = tr("ai.err_conn_hint");
    std::string errHttpPrefix = tr("ai.err_http_prefix");
    std::string errUnexpected = tr("ai.err_unexpected_resp");
    std::string errJsonParse = tr("ai.err_json_parse");

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

    std::thread([this, endpointUrl, payloadStr, errCurlInit, errConnPrefix, errConnHint, errHttpPrefix, errUnexpected, errJsonParse]()
                {
        std::string responseBody;
        std::string errorMsg;

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            errorMsg = errCurlInit;
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
                errorMsg = errConnPrefix + " : " + curl_easy_strerror(res) +
                           " (" + errConnHint + " " + endpointUrl + ")";
            }
            else
            {
                long httpCode = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                if (httpCode >= 400)
                {
                    errorMsg = errHttpPrefix + " (" + std::to_string(httpCode) + ") : " + responseBody;
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
                    assistantReply = errUnexpected + " : " + responseBody;
            }
            catch (const std::exception &e)
            {
                assistantReply = errJsonParse + " : " + e.what();
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
    std::string panelTitle = std::string(tr("panel.ai_assistant")) + "###AiAssistantBottomPanel";
    ImGui::Begin(panelTitle.c_str());

    // 1. Barre de configuration compacte en haut
    ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.95f, 1.0f), "%s  Ollama", ICON_LC_CPU);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("ai.model_label"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(165.0f);
    ImGui::InputText("##OllamaModel", m_aiModelNameBuf.data(), m_aiModelNameBuf.size());

    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("ai.url_label"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##OllamaUrl", m_aiOllamaUrlBuf.data(), m_aiOllamaUrlBuf.size());

    ImGui::SameLine();
    std::string schemaCheckLabel = std::string(tr("ai.include_schemas_check")) + "###AiSchemaCheck";
    ImGui::Checkbox(schemaCheckLabel.c_str(), &m_aiIncludeSchemaContext);

    ImGui::SameLine();
    std::string activeSqlCheckLabel = std::string(tr("ai.include_active_sql_check")) + "###AiActiveSqlCheck";
    ImGui::Checkbox(activeSqlCheckLabel.c_str(), &m_aiIncludeActiveSqlContext);

    ImGui::SameLine();
    std::string clearChatBtnLabel = std::string(ICON_LC_TRASH) + " " + tr("sql_editor.btn_clear") + "###ClearAiChatBtn";
    if (ImGui::SmallButton(clearChatBtnLabel.c_str()))
    {
        std::lock_guard<std::mutex> lock(m_aiChatMutex);
        m_aiChatHistory.clear();
    }

    ImGui::Separator();

    // 2. Zone défilante de l'historique des messages
    float inputBarHeight = 64.0f;
    float historyHeight = ImGui::GetContentRegionAvail().y - inputBarHeight - 8.0f;
    if (historyHeight < 60.0f)
        historyHeight = 60.0f;

    ImGui::BeginChild("AiChatHistoryScroll", ImVec2(-1.0f, historyHeight), true);
    {
        std::lock_guard<std::mutex> lock(m_aiChatMutex);

        if (m_aiChatHistory.empty())
        {
            ImGui::TextDisabled("%s", tr("ai.empty_chat_hint"));
        }

        for (size_t i = 0; i < m_aiChatHistory.size(); ++i)
        {
            const auto &msg = m_aiChatHistory[i];
            ImGui::PushID(static_cast<int>(i));

            if (msg.isUser)
            {
                ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s", tr("ai.user_prefix"));
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(msg.text.c_str());
                ImGui::PopTextWrapPos();
            }
            else
            {
                ImGui::TextColored(ImVec4(0.65f, 0.45f, 0.95f, 1.0f), "%s", tr("ai.assistant_prefix"));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(msg.text.c_str());
                ImGui::PopTextWrapPos();

                // Boutons d'action directe si l'IA a généré un bloc SQL
                if (!msg.extractedSql.empty())
                {
                    ImGui::Spacing();
                    std::string copySqlLabel = std::string(ICON_LC_COPY) + "  " + tr("ai.btn_copy_sql") + "###CopyAiSqlBtn";
                    if (ImGui::SmallButton(copySqlLabel.c_str()))
                    {
                        snprintf(m_rawSqlEditorBuf.data(), m_rawSqlEditorBuf.size(), "%s", msg.extractedSql.c_str());
                        addConsoleLog(tr("ai.log_sql_copied"), LogLevel::Info);
                    }
                    ImGui::SameLine();
                    std::string runSqlLabel = std::string(ICON_LC_PLAY) + "  " + tr("ai.btn_run_sql") + "###RunAiSqlBtn";
                    if (ImGui::SmallButton(runSqlLabel.c_str()))
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
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "%s  %s %s...",
                               ICON_LC_LOADER, tr("ai.thinking_prefix"), m_aiModelNameBuf.data());
        }
        if (m_aiScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            m_aiScrollToBottom = false;
        }
    }
    ImGui::EndChild();

    bool thinking = m_isAiThinking.load();
    if (thinking)
        ImGui::BeginDisabled();

    float sendBtnWidth = 115.0f;
    float inputWidth = ImGui::GetContentRegionAvail().x - sendBtnWidth - ImGui::GetStyle().ItemSpacing.x;
    if (inputWidth < 120.0f)
        inputWidth = 120.0f;

    ImGui::InputTextMultiline(
        "##AiPromptInput",
        m_aiPromptBuf.data(),
        m_aiPromptBuf.size(),
        ImVec2(inputWidth, inputBarHeight),
        ImGuiInputTextFlags_AllowTabInput);

    bool isPromptFocused = ImGui::IsItemFocused();

    // Affiche le texte indicatif (placeholder) lorsque le champ est vide et non focalisé
    if (m_aiPromptBuf[0] == '\0' && !isPromptFocused)
    {
        ImVec2 rectMin = ImGui::GetItemRectMin();
        ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rectMin.x + pad.x, rectMin.y + pad.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            tr("ai.prompt_hint"));
    }

    // Raccourci Ctrl + Entrée pour envoyer depuis la zone multiligne
    bool ctrlEnterPressed = isPromptFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.30f, 0.80f, 0.90f));
    std::string sendBtnLabel = std::string(ICON_LC_SEND) + "  " + tr("ai.btn_send") + "###SendAiPromptBtn";
    bool sendClicked = ImGui::Button(sendBtnLabel.c_str(), ImVec2(sendBtnWidth, inputBarHeight));
    ImGui::PopStyleColor();

    if ((ctrlEnterPressed || sendClicked) && m_aiPromptBuf[0] != '\0')
    {
        std::string prompt(m_aiPromptBuf.data());
        m_aiPromptBuf[0] = '\0';
        sendPromptToOllamaAsync(prompt);
    }

    if (thinking)
        ImGui::EndDisabled();

    ImGui::End();
}
