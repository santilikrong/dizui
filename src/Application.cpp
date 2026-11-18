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

using namespace cppsqlx::literals;

namespace
{

    struct SqlEditorCallbackUserData
    {
        int *cursorPos;
        std::string *pendingInsertion;
        int replaceStart;
        int replaceEnd;
    };

    int sqlTextEditorCallback(ImGuiInputTextCallbackData *data)
    {
        auto *userData = static_cast<SqlEditorCallbackUserData *>(data->UserData);
        if (!userData)
            return 0;

        // Si une suggestion vient d'être validée, on remplace le mot en cours dans le buffer ImGui
        if (userData->pendingInsertion && !userData->pendingInsertion->empty())
        {
            int start = std::max(0, std::min(userData->replaceStart, data->BufTextLen));
            int end = std::max(start, std::min(userData->replaceEnd, data->BufTextLen));

            data->DeleteChars(start, end - start);
            data->InsertChars(start, userData->pendingInsertion->c_str());
            data->CursorPos = start + static_cast<int>(userData->pendingInsertion->size());
            data->SelectionStart = data->SelectionEnd = data->CursorPos;

            userData->pendingInsertion->clear();
        }

        if (userData->cursorPos)
        {
            *userData->cursorPos = data->CursorPos;
        }
        return 0;
    }

    void renderSqlSyntaxHighlightOverlay(ImDrawList *drawList, ImVec2 textOrigin, const char *text, ImFont *font, float fontSize)
    {
        if (!text || !*text)
            return;

        // Palette de couleurs syntaxiques (adaptée aux thèmes sombres et clairs)
        const ImU32 colKeyword = IM_COL32(86, 156, 214, 255);   // Bleu vif : SELECT, FROM, WHERE, JOIN...
        const ImU32 colFunction = IM_COL32(220, 220, 170, 255); // Jaune doux : COUNT, SUM, COALESCE, CONCAT...
        const ImU32 colString = IM_COL32(206, 145, 120, 255);   // Orange/Corail : 'chaînes de caractères'
        const ImU32 colNumber = IM_COL32(181, 206, 168, 255);   // Vert pastel : 123, 45.67
        const ImU32 colComment = IM_COL32(106, 153, 85, 255);   // Vert olive : -- commentaires
        const ImU32 colOperator = IM_COL32(180, 180, 180, 255); // Gris clair : =, <, >, (, ), ;, ,
        const ImU32 colDefault = IM_COL32(220, 224, 232, 255);  // Blanc cassé : identifiants, tables, colonnes

        static const std::unordered_set<std::string> sqlKeywords = {
            "WITH", "AS", "SELECT", "DISTINCT", "FROM", "INNER", "LEFT", "RIGHT", "FULL",
            "OUTER", "CROSS", "JOIN", "ON", "WHERE", "AND", "OR", "NOT", "IN", "IS", "NULL",
            "LIKE", "ILIKE", "BETWEEN", "EXISTS", "CASE", "WHEN", "THEN", "ELSE", "END",
            "GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET", "UNION",
            "ALL", "INTERSECT", "EXCEPT", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE",
            "CREATE", "TABLE", "VIEW", "DROP", "ALTER", "EXPLAIN", "ANALYZE", "TRUE", "FALSE"};

        static const std::unordered_set<std::string> sqlFunctions = {
            "COUNT", "SUM", "AVG", "MIN", "MAX", "COALESCE", "NULLIF", "CAST", "CONCAT",
            "SUBSTRING", "TRIM", "LOWER", "UPPER", "LENGTH", "NOW", "CURRENT_DATE",
            "DATE_TRUNC", "EXTRACT", "ROUND", "ABS", "ROW_NUMBER", "RANK", "DENSE_RANK", "OVER", "PARTITION"};

        auto toUpper = [](const std::string &s)
        {
            std::string up = s;
            for (char &ch : up)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return up;
        };

        float lineHeight = ImGui::GetTextLineHeight();
        float spaceWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, " ").x;
        ImVec2 cursor = textOrigin;

        const char *p = text;
        while (*p)
        {
            // 1. Sauts de ligne et tabulations
            if (*p == '\n')
            {
                cursor.x = textOrigin.x;
                cursor.y += lineHeight;
                ++p;
                continue;
            }
            if (*p == '\r')
            {
                ++p;
                continue;
            }
            if (*p == '\t')
            {
                cursor.x += spaceWidth * 4.0f;
                ++p;
                continue;
            }
            if (*p == ' ')
            {
                cursor.x += spaceWidth;
                ++p;
                continue;
            }

            // 2. Commentaire sur une ligne (-- ...)
            if (*p == '-' && *(p + 1) == '-')
            {
                const char *start = p;
                while (*p && *p != '\n')
                    ++p;
                drawList->AddText(font, fontSize, cursor, colComment, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            // 3. Chaîne littérale ('...')
            if (*p == '\'')
            {
                const char *start = p++;
                while (*p)
                {
                    if (*p == '\'' && *(p + 1) == '\'')
                    {
                        p += 2;
                    }
                    else if (*p == '\'')
                    {
                        ++p;
                        break;
                    }
                    else if (*p == '\n')
                    {
                        break;
                    }
                    else
                    {
                        ++p;
                    }
                }
                drawList->AddText(font, fontSize, cursor, colString, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            // 4. Nombre (entier ou décimal)
            if (std::isdigit(static_cast<unsigned char>(*p)))
            {
                const char *start = p;
                while (*p && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.'))
                    ++p;
                drawList->AddText(font, fontSize, cursor, colNumber, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            // 5. Mot-clé, fonction ou identifiant
            if (std::isalpha(static_cast<unsigned char>(*p)) || *p == '_' || *p == '"')
            {
                const char *start = p;
                if (*p == '"')
                {
                    ++p;
                    while (*p && *p != '"' && *p != '\n')
                        ++p;
                    if (*p == '"')
                        ++p;
                }
                else
                {
                    while (*p && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
                        ++p;
                }

                std::string word(start, p);
                std::string up = toUpper(word);

                ImU32 color = colDefault;
                if (sqlKeywords.contains(up))
                    color = colKeyword;
                else if (sqlFunctions.contains(up))
                    color = colFunction;

                drawList->AddText(font, fontSize, cursor, color, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            // 6. Opérateurs et ponctuation
            const char *start = p++;
            drawList->AddText(font, fontSize, cursor, colOperator, start, p);
            cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
        }
    }

    std::string formatRawSqlQuery(const std::string &rawSql)
    {
        if (rawSql.empty())
            return "";

        // 1. Découpage en jetons tout en préservant les chaînes littérales '...' et identifiants "..."
        std::vector<std::string> tokens;
        std::string current;
        bool inSingleQuote = false;
        bool inDoubleQuote = false;

        for (size_t i = 0; i < rawSql.size(); ++i)
        {
            char c = rawSql[i];
            if (c == '\'' && !inDoubleQuote)
            {
                inSingleQuote = !inSingleQuote;
                current.push_back(c);
            }
            else if (c == '"' && !inSingleQuote)
            {
                inDoubleQuote = !inDoubleQuote;
                current.push_back(c);
            }
            else if (!inSingleQuote && !inDoubleQuote && std::isspace(static_cast<unsigned char>(c)))
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
            }
            else if (!inSingleQuote && !inDoubleQuote && (c == ',' || c == '(' || c == ')' || c == ';'))
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.emplace_back(1, c);
            }
            else
            {
                current.push_back(c);
            }
        }
        if (!current.empty())
            tokens.push_back(current);

        // 2. Liste des mots-clés SQL à mettre en majuscules
        auto toUpper = [](const std::string &s)
        {
            std::string up = s;
            for (char &ch : up)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return up;
        };

        static const std::unordered_set<std::string> keywords = {
            "WITH", "AS", "SELECT", "DISTINCT", "FROM", "INNER", "LEFT", "RIGHT", "FULL",
            "OUTER", "CROSS", "JOIN", "ON", "WHERE", "AND", "OR", "NOT", "IN", "IS", "NULL",
            "LIKE", "ILIKE", "BETWEEN", "EXISTS", "CASE", "WHEN", "THEN", "ELSE", "END",
            "GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET", "UNION",
            "ALL", "INTERSECT", "EXCEPT", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE"};

        // 3. Reconstruction avec sauts de ligne et indentation
        std::ostringstream out;
        int parenDepth = 0;
        bool inSelectClause = false;
        bool newLineJustEmitted = true;

        auto emitNewLine = [&](int extraIndent = 0)
        {
            out << "\n";
            int spaces = (parenDepth + extraIndent) * 4;
            if (spaces > 0)
                out << std::string(spaces, ' ');
            newLineJustEmitted = true;
        };

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            std::string tok = tokens[i];
            bool isQuoted = (!tok.empty() && (tok.front() == '\'' || tok.front() == '"'));
            std::string up = isQuoted ? tok : toUpper(tok);

            if (!isQuoted && keywords.contains(up))
            {
                tok = up;
            }

            // Détection des clauses principales au niveau racine (ou début de sous-requête)
            if (!isQuoted)
            {
                if (up == "SELECT")
                {
                    inSelectClause = true;
                    if (!newLineJustEmitted)
                        emitNewLine();
                    out << "SELECT";
                    emitNewLine(1);
                    continue;
                }
                if (up == "FROM" || up == "WHERE" || up == "HAVING" || up == "LIMIT" || up == "OFFSET" || up == "UNION")
                {
                    inSelectClause = false;
                    emitNewLine();
                    out << up;
                    newLineJustEmitted = false;
                    continue;
                }
                if ((up == "GROUP" || up == "ORDER") && i + 1 < tokens.size() && toUpper(tokens[i + 1]) == "BY")
                {
                    inSelectClause = false;
                    emitNewLine();
                    out << up << " BY";
                    ++i;
                    newLineJustEmitted = false;
                    continue;
                }
                if ((up == "INNER" || up == "LEFT" || up == "RIGHT" || up == "FULL" || up == "CROSS") &&
                    i + 1 < tokens.size() && toUpper(tokens[i + 1]) == "JOIN")
                {
                    inSelectClause = false;
                    emitNewLine();
                    out << up << " JOIN";
                    ++i;
                    newLineJustEmitted = false;
                    continue;
                }
                if (up == "JOIN")
                {
                    inSelectClause = false;
                    emitNewLine();
                    out << "JOIN";
                    newLineJustEmitted = false;
                    continue;
                }
                if (up == "AND" || up == "OR")
                {
                    emitNewLine(1);
                    out << up;
                    newLineJustEmitted = false;
                    continue;
                }
            }

            if (tok == ",")
            {
                out << ",";
                if (inSelectClause)
                {
                    emitNewLine(1);
                }
                else
                {
                    newLineJustEmitted = false;
                }
                continue;
            }

            if (tok == "(")
            {
                if (!newLineJustEmitted && i > 0 && tokens[i - 1] != "(")
                    out << " ";
                out << "(";
                ++parenDepth;
                newLineJustEmitted = false;
                continue;
            }

            if (tok == ")")
            {
                if (parenDepth > 0)
                    --parenDepth;
                out << ")";
                newLineJustEmitted = false;
                continue;
            }

            if (tok == ";")
            {
                out << ";\n";
                inSelectClause = false;
                newLineJustEmitted = true;
                continue;
            }

            if (!newLineJustEmitted && i > 0 && tokens[i - 1] != "(")
            {
                out << " ";
            }
            out << tok;
            newLineJustEmitted = false;
        }

        return out.str();
    }


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

    g_coutRedirector = std::make_unique<GuiStreamRedirector>(std::cout, [this](const std::string &line) {
        LogLevel lvl = LogLevel::Info;
        if (line.find("succès") != std::string::npos || line.find("OK") != std::string::npos)
            lvl = LogLevel::Success;
        addConsoleLog(line, lvl);
    });

    g_cerrRedirector = std::make_unique<GuiStreamRedirector>(std::cerr, [this](const std::string &line) {
        addConsoleLog(line, LogLevel::Error);
    });

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

void Application::createNewProject(const std::string &projectName)
{
    m_activeProject = std::make_unique<cppsqlx::DataIntegrationProject>();
    m_activeProject->setName(projectName);

    m_currentProjectName = projectName;
    m_currentProjectFilePath = "";
    // ... reste de la méthode inchangé ...

    m_selectedGateway = "";
    m_selectedContext = "";

    // Réinitialisation des catalogues
    m_savedQueries.clear();
    m_savedTasks.clear();
    m_savedWorkflows.clear();

    // Nettoyage du Query Designer
    m_visualQueryNodes.clear();
    m_visualQueryLinks.clear();
    m_visualQueryPredicates.clear();
    m_projectionItems.clear();
    m_hasProjectionNode = false;
    m_queryResultHeaders.clear();
    m_queryResultRows.clear();
    m_executionError = "";
    m_compiledSQLPreview = "";

    // Nettoyage du Workflow Designer
    m_workflowNodes.clear();
    m_workflowLinks.clear();
    strncpy(m_activeWorkflowNameBuf, "Mon_Workflow", sizeof(m_activeWorkflowNameBuf) - 1);
    m_activeWorkflowNameBuf[sizeof(m_activeWorkflowNameBuf) - 1] = '\0';
    memset(m_activeWorkflowDescBuf, 0, sizeof(m_activeWorkflowDescBuf));
    m_compiledWorkflowJson = "";

    {
        std::lock_guard<std::mutex> lock(m_workflowStatusMutex);
        m_liveJobStatuses.clear();
        m_liveJobMessages.clear();
    }

    // Mise à jour du titre de la fenêtre
    std::string windowTitle = "dizui Studio - " + m_currentProjectName + " [Non enregistré]";
    if (GLFWwindow *currentWin = glfwGetCurrentContext())
    {
        glfwSetWindowTitle(currentWin, windowTitle.c_str());
    }
}

cppsqlx::SelectQuery Application::compileVisualGraphToSelectQuery()
{
    cppsqlx::SelectQuery query;
    std::vector<std::string> local_select_headers;

    if (!m_activeProject || m_visualQueryNodes.empty())
    {
        m_queryResultHeaders.clear();
        return query;
    }

    ed::SetCurrentEditor(m_queryContext);

    // =========================================================================
    // Étape 0 : Collecte et déclaration des CTE (WITH nom_requete AS (...))
    // =========================================================================
    std::set<std::string> registeredCteNames;
    for (const auto &node : m_visualQueryNodes)
    {
        if (!node.isCTE || registeredCteNames.contains(node.datasetName))
            continue;

        for (const auto &sq : m_savedQueries)
        {
            if (sq.name == node.datasetName && !sq.compiledSelectJson.is_null() && !sq.compiledSelectJson.empty())
            {
                auto subRes = cppsqlx::SelectQuery::fromJSON(sq.compiledSelectJson);
                if (subRes.has_value())
                {
                    auto subPtr = std::make_shared<cppsqlx::SelectQuery>(std::move(subRes.value()));
                    query.with(node.datasetName, subPtr);
                    registeredCteNames.insert(node.datasetName);
                }
                break;
            }
        }
    }

    // Helper interne pour résoudre un nœud (Table physique OU CTE) en objet Joinable (Dataset)
    auto resolveNodeAsJoinable = [&](const VisualQueryNode &node) -> std::shared_ptr<cppsqlx::Dataset>
    {
        if (node.isCTE)
        {
            // Une CTE est interrogée par son nom dans la clause FROM / JOIN (sans préfixe de schéma)
            cppsqlx::Dataset cteDs(node.datasetName);
            return std::make_shared<cppsqlx::Dataset>(std::move(cteDs));
        }
        if (m_activeProject->datasets().contains(node.datasetName))
        {
            return std::make_shared<cppsqlx::Dataset>(m_activeProject->datasets().at(node.datasetName));
        }
        return nullptr;
    };

    // =========================================================================
    // Étape A : Clause SELECT unifiée (SQLSelectionSet)
    // =========================================================================
    cppsqlx::SQLSelectionSet selectionSet;
    bool has_selection = false;
    bool has_aggregate = false;

    if (!m_projectionItems.empty())
    {
        for (const auto &item : m_projectionItems)
        {
            if (item.sqlExpression.empty())
                continue;

            std::string finalSqlExpr = item.sqlExpression;

            // Résolution dynamique de l'alias actuel du nœud si la colonne n'est pas une formule libre
            if (!item.isCustomExpression && item.sourceNodeId != 0)
            {
                for (const auto &node : m_visualQueryNodes)
                {
                    if (node.id == item.sourceNodeId)
                    {
                        std::string rawCol = finalSqlExpr;
                        if (auto dot = rawCol.find('.'); dot != std::string::npos)
                        {
                            rawCol = rawCol.substr(dot + 1);
                        }
                        finalSqlExpr = node.alias.empty() ? rawCol : (node.alias + "." + rawCol);
                        break;
                    }
                }
            }

            cppsqlx::SQLExpression expr = operator""_col(finalSqlExpr.c_str(), finalSqlExpr.length());
            if (!item.alias.empty())
            {
                expr = expr.as(item.alias);
            }
            selectionSet.add(expr);
            local_select_headers.push_back(item.alias.empty() ? finalSqlExpr : item.alias);
            has_selection = true;

            if (item.isAggregate)
                has_aggregate = true;
        }
        query.select(selectionSet);
    }
    else
    {
        // Rétrocompatibilité avec les anciennes requêtes sans m_projectionItems
        for (const auto &node : m_visualQueryNodes)
        {
            if (!node.isCTE && m_activeProject->datasets().contains(node.datasetName))
            {
                const auto &dataset = m_activeProject->datasets().at(node.datasetName);
                for (size_t col_idx : node.selectedColumns)
                {
                    if (dataset.columns().contains(col_idx))
                    {
                        std::string field_name = dataset.columns().at(col_idx).name();
                        std::string qualified_col = node.alias.empty() ? field_name : (node.alias + "." + field_name);
                        std::string unique_alias = node.alias.empty() ? field_name : (node.alias + "_" + field_name);

                        query.select(operator""_col(qualified_col.c_str(), qualified_col.length()).as(unique_alias));
                        local_select_headers.push_back(unique_alias);
                        has_selection = true;
                    }
                }
            }
        }
    }

    if (!has_selection)
    {
        query.select(operator""_col("*", 1));
        local_select_headers.push_back("*");
    }

    m_queryResultHeaders = local_select_headers;

    // =========================================================================
    // Étape B : Table / CTE principale (Clause FROM)
    // =========================================================================
    const auto &firstNode = m_visualQueryNodes[0];
    if (auto firstDsPtr = resolveNodeAsJoinable(firstNode))
    {
        std::string firstAlias = firstNode.alias.empty() ? firstNode.datasetName : firstNode.alias;
        query.from(firstDsPtr, firstAlias);
    }

    // =========================================================================
    // Étape C : Construction des jointures (INNER, LEFT, RIGHT, FULL)
    // =========================================================================
    std::set<uintptr_t> joinedNodeIds;
    joinedNodeIds.insert(firstNode.id);

    for (const auto &link : m_visualQueryLinks)
    {
        uintptr_t src_node_id = link.startPinId / 1000;
        uintptr_t dst_node_id = link.endPinId / 1000;

        const VisualQueryNode *srcNode = nullptr;
        const VisualQueryNode *dstNode = nullptr;

        for (const auto &n : m_visualQueryNodes)
        {
            if (n.id == src_node_id)
                srcNode = &n;
            if (n.id == dst_node_id)
                dstNode = &n;
        }

        if (!srcNode || !dstNode)
            continue;

        const VisualQueryNode *targetNode = joinedNodeIds.contains(dst_node_id) ? srcNode : dstNode;
        if (joinedNodeIds.contains(targetNode->id))
            continue;

        auto targetDsPtr = resolveNodeAsJoinable(*targetNode);
        if (!targetDsPtr)
            continue;

        std::string live_left_field = link.leftFieldName;
        if (auto dot = live_left_field.find('.'); dot != std::string::npos)
            live_left_field = srcNode->alias + "." + live_left_field.substr(dot + 1);

        std::string live_right_field = link.rightFieldName;
        if (auto dot = live_right_field.find('.'); dot != std::string::npos)
            live_right_field = dstNode->alias + "." + live_right_field.substr(dot + 1);

        cppsqlx::SQLExpression left_expr = operator""_col(live_left_field.c_str(), live_left_field.length());
        cppsqlx::SQLExpression right_expr = operator""_col(live_right_field.c_str(), live_right_field.length());

        cppsqlx::SQLExpression on_expr;
        if (link.op == cppsqlx::SQLOperator::Equal)
            on_expr = left_expr.equal(right_expr);
        else if (link.op == cppsqlx::SQLOperator::NotEqual)
            on_expr = left_expr.notEqual(right_expr);
        else if (link.op == cppsqlx::SQLOperator::Greater)
            on_expr = left_expr.greater(right_expr);
        else if (link.op == cppsqlx::SQLOperator::Less)
            on_expr = left_expr.less(right_expr);
        else if (link.op == cppsqlx::SQLOperator::GreaterOrEqual)
            on_expr = left_expr.greaterOrEqual(right_expr);
        else if (link.op == cppsqlx::SQLOperator::LessOrEqual)
            on_expr = left_expr.lessOrEqual(right_expr);
        else
            on_expr = left_expr.equal(right_expr);

        std::string joinTypeStr = "INNER JOIN";
        if (link.type == cppsqlx::JoinType::LEFT)
            joinTypeStr = "LEFT JOIN";
        else if (link.type == cppsqlx::JoinType::RIGHT)
            joinTypeStr = "RIGHT JOIN";
        else if (link.type == cppsqlx::JoinType::FULL)
            joinTypeStr = "FULL JOIN";

        std::string targetAlias = targetNode->alias.empty() ? targetNode->datasetName : targetNode->alias;
        query.join(joinTypeStr, targetDsPtr, targetAlias, on_expr);
        joinedNodeIds.insert(targetNode->id);
    }

    // =========================================================================
    // Étape D : Clause WHERE (Prédicats simples ET Expressions SQL avancées)
    // =========================================================================
    std::string combinedWhereSql = "";

    for (const auto &pred : m_visualQueryPredicates)
    {
        std::string singlePredSql = "";

        if (pred.isCustomExpression && !pred.customSqlCondition.empty())
        {
            singlePredSql = pred.customSqlCondition;
        }
        else if (!pred.isCustomExpression && pred.connectedSrcPinId != 0 && !pred.value.empty())
        {
            uintptr_t table_node_id = pred.connectedSrcPinId / 1000;
            std::string live_pred_column = pred.targetColumn;

            for (const auto &n : m_visualQueryNodes)
            {
                if (n.id == table_node_id)
                {
                    std::string raw_col = pred.targetColumn;
                    if (auto dot = raw_col.find('.'); dot != std::string::npos)
                        raw_col = raw_col.substr(dot + 1);

                    live_pred_column = n.alias + "." + raw_col;
                }
            }

            cppsqlx::SQLExpression col_expr = operator""_col(live_pred_column.c_str(), live_pred_column.length());

            cppsqlx::SQLExpression val_expr;
            try
            {
                size_t idx = 0;
                long long num_val = std::stoll(pred.value, &idx);
                if (idx == pred.value.length())
                    val_expr = cppsqlx::SQLExpression(static_cast<int64_t>(num_val));
                else
                    val_expr = cppsqlx::SQLExpression(pred.value);
            }
            catch (...)
            {
                val_expr = cppsqlx::SQLExpression(pred.value);
            }
            cppsqlx::SQLExpression final_where_expr;
            if (pred.op == cppsqlx::SQLOperator::Equal)
                final_where_expr = col_expr.equal(val_expr);
            else if (pred.op == cppsqlx::SQLOperator::NotEqual)
                final_where_expr = col_expr.notEqual(val_expr);
            else if (pred.op == cppsqlx::SQLOperator::Greater)
                final_where_expr = col_expr.greater(val_expr);
            else if (pred.op == cppsqlx::SQLOperator::Less)
                final_where_expr = col_expr.less(val_expr);
            else if (pred.op == cppsqlx::SQLOperator::GreaterOrEqual)
                final_where_expr = col_expr.greaterOrEqual(val_expr);
            else if (pred.op == cppsqlx::SQLOperator::LessOrEqual)
                final_where_expr = col_expr.lessOrEqual(val_expr);
            else
                final_where_expr = col_expr.equal(val_expr);
            singlePredSql = final_where_expr.toSQL();
        }
        if (!singlePredSql.empty())
        {
            if (combinedWhereSql.empty())
                combinedWhereSql = singlePredSql;
            else
                combinedWhereSql = "(" + combinedWhereSql + ") AND (" + singlePredSql + ")";
        }
    }
    if (!combinedWhereSql.empty())
    {
        query.where(cppsqlx::SQLExpression(combinedWhereSql));
    }
    // =========================================================================
    // Étape E : GROUP BY automatique si fonction d'agrégation présente
    // =========================================================================
    if (has_aggregate)
    {
        for (const auto &item : m_projectionItems)
        {
            if (!item.isAggregate && !item.sqlExpression.empty())
            {
                query.groupBy(cppsqlx::SQLExpression(item.sqlExpression));
            }
        }
    }
    ed::SetCurrentEditor(nullptr);
    return query;
}

std::string Application::compileVisualGraphToSQL()
{
    if (m_visualQueryNodes.empty() || !m_activeProject)
    {
        return "";
    }
    return compileVisualGraphToSelectQuery().toSQL();
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

nlohmann::json Application::serializeFullProjectToJSON()
{
    nlohmann::json rootJson;

    if (m_activeProject)
    {
        // 1. Synchronisation des tâches et workflows courants dans m_activeProject
        for (const auto &task : m_savedTasks)
        {
            m_activeProject->addTask(task);
        }

        for (const auto &wf : m_savedWorkflows)
        {
            try
            {
                nlohmann::json wfJson = nlohmann::json::parse(wf.jsonPreview);
                auto wfRes = cppsqlx::Workflow::fromJSON(wfJson);
                if (wfRes.has_value())
                {
                    m_activeProject->addWorkflow(std::move(wfRes.value()));
                }
            }
            catch (...)
            {
            }
        }

        // 2. Sérialisation officielle complète (connexions, datasets, requêtes, tâches, jobs, workflows)
        rootJson = m_activeProject->toJSON();
    }

    // 3. Ajout des métadonnées d'interface et des graphes visuels (positions, broches et câbles)
    nlohmann::json uiState;
    uiState["queries"] = nlohmann::json::array();
    for (const auto &q : m_savedQueries)
    {
        nlohmann::json schemaArr = nlohmann::json::array();
        for (const auto &col : q.outputSchema)
        {
            schemaArr.push_back({
                {"name", col.name},
                {"dataType", col.dataType},
                {"isNullable", col.isNullable},
                {"isPrimaryKey", col.isPrimaryKey}
            });
        }

        uiState["queries"].push_back({
            {"name", q.name},
            {"description", q.description},
            {"sql", q.sql},
            {"visual_state", q.visualState},
            {"select_json", q.compiledSelectJson},
            {"output_schema", std::move(schemaArr)}
        });
    }


    uiState["workflows"] = nlohmann::json::array();
    for (const auto &wf : m_savedWorkflows)
    {
        uiState["workflows"].push_back({{"name", wf.name},
                                        {"description", wf.description},
                                        {"json_preview", wf.jsonPreview},
                                        {"visual_state", wf.visualState}});
    }

    rootJson["dizui_ui_state"] = std::move(uiState);
    return rootJson;
}

bool Application::saveProjectToFile(const std::string &filePath)
{
    if (filePath.empty())
        return false;

    try
    {
        std::filesystem::path p(filePath);
        m_currentProjectFilePath = filePath;
        m_currentProjectName = p.stem().string();

        if (!m_activeProject)
        {
            m_activeProject = std::make_unique<cppsqlx::DataIntegrationProject>(m_currentProjectName, filePath);
        }
        else
        {
            // Mise à jour en place pour conserver toutes les connexions et datasets en mémoire
            m_activeProject->setName(m_currentProjectName);
            m_activeProject->setPath(filePath);
        }

        std::ofstream outFile(filePath);
        if (!outFile.is_open())
        {
            std::cerr << "[dizui Erreur] Impossible d'ouvrir le fichier en écriture : " << filePath << std::endl;
            return false;
        }

        nlohmann::json fullJson = serializeFullProjectToJSON();
        outFile << fullJson.dump(4);
        outFile.close();

        std::string windowTitle = "dizui Studio - " + m_currentProjectName + " [" + m_currentProjectFilePath + "]";
        if (GLFWwindow *currentWin = glfwGetCurrentContext())
        {
            glfwSetWindowTitle(currentWin, windowTitle.c_str());
        }

        std::cout << "[dizui Info] Projet complet sauvegardé dans : " << filePath << std::endl;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[dizui Erreur] Échec de la sauvegarde : " << e.what() << std::endl;
        return false;
    }
}

bool Application::loadProjectFromFile(const std::string &filePath)
{
    if (filePath.empty())
        return false;

    try
    {
        std::ifstream inFile(filePath);
        if (!inFile.is_open())
        {
            std::cerr << "[dizui Erreur] Impossible d'ouvrir le fichier : " << filePath << std::endl;
            return false;
        }

        nlohmann::json rootJson;
        inFile >> rootJson;
        inFile.close();

        // 1. Reconstruction officielle de DataIntegrationProject (connexions, datasets, tâches, workflows)
        auto projRes = cppsqlx::DataIntegrationProject::fromJSON(rootJson);
        if (!projRes.has_value())
        {
            std::cerr << "[dizui Erreur] JSON projet invalide : " << projRes.error() << std::endl;
            return false;
        }

        // Instanciation unique (le doublon qui vidait l'objet a été supprimé)
        m_activeProject = std::make_unique<cppsqlx::DataIntegrationProject>(std::move(projRes.value()));
        m_activeProject->setPath(filePath);
        m_activeProject->resolveDependencies();

        // Assainissement des passerelles logiques dont le nom interne serait vide après fromJSON
        for (auto &[gwKey, gwObj] : m_activeProject->logicalConnections())
        {
            if (gwObj.name().empty() && !gwKey.empty())
            {
                gwObj.setName(gwKey);
            }
        }

        m_currentProjectFilePath = filePath;
        m_currentProjectName = m_activeProject->name();

        // 2. Nettoyage des canevas actifs
        m_visualQueryNodes.clear();
        m_visualQueryLinks.clear();
        m_visualQueryPredicates.clear();
        m_projectionItems.clear();
        m_hasProjectionNode = false;
        m_workflowNodes.clear();
        m_workflowLinks.clear();
        m_savedQueries.clear();
        m_savedTasks.clear();
        m_savedWorkflows.clear();


        // 3. Repeuplement des tâches depuis m_activeProject
        for (const auto &[tName, taskObj] : m_activeProject->tasks())
        {
            m_savedTasks.push_back(taskObj);
        }

        // 4. Restauration des requêtes et workflows avec leurs graphes visuels et JSON compilés
        if (rootJson.contains("dizui_ui_state"))
        {
            const auto &uiState = rootJson["dizui_ui_state"];

            if (uiState.contains("queries") && uiState["queries"].is_array())
            {
                for (const auto &qJson : uiState["queries"])
                {
                    SavedQuery sq;
                    sq.name = qJson.value("name", "Requete");
                    sq.description = qJson.value("description", "");
                    sq.sql = qJson.value("sql", "");
                    if (qJson.contains("visual_state"))
                        sq.visualState = qJson["visual_state"];
                    if (qJson.contains("select_json"))
                        sq.compiledSelectJson = qJson["select_json"];

                    if (qJson.contains("output_schema") && qJson["output_schema"].is_array())
                    {
                        for (const auto &colJson : qJson["output_schema"])
                        {
                            cppsqlx::ColumnMetadata cm;
                            cm.name = colJson.value("name", "");
                            cm.dataType = colJson.value("dataType", "any");
                            cm.isNullable = colJson.value("isNullable", true);
                            cm.isPrimaryKey = colJson.value("isPrimaryKey", false);
                            sq.outputSchema.push_back(std::move(cm));
                        }
                    }

                    m_savedQueries.push_back(std::move(sq));
                }
            }


            if (uiState.contains("workflows") && uiState["workflows"].is_array())
            {
                for (const auto &wfJson : uiState["workflows"])
                {
                    SavedWorkflow swf;
                    swf.name = wfJson.value("name", "Workflow");
                    swf.description = wfJson.value("description", "");
                    swf.jsonPreview = wfJson.value("json_preview", "");
                    if (wfJson.contains("visual_state"))
                        swf.visualState = wfJson["visual_state"];
                    m_savedWorkflows.push_back(std::move(swf));
                }
            }
        }

        // 5. Repli : si m_savedWorkflows est encore vide, reconstruction depuis m_activeProject->workflows()
        if (m_savedWorkflows.empty() && !m_activeProject->workflows().empty())
        {
            for (const auto &[wfKey, wfObj] : m_activeProject->workflows())
            {
                SavedWorkflow swf;
                swf.name = wfObj.name().empty() ? wfKey : wfObj.name();
                swf.description = wfObj.context().description_;
                swf.jsonPreview = wfObj.toJSON().dump(4);

                nlohmann::json vs;
                vs["nodes"] = nlohmann::json::array();
                vs["links"] = nlohmann::json::array();

                uintptr_t baseId = 2000;
                for (const auto &jobPtr : wfObj.jobs())
                {
                    if (!jobPtr)
                        continue;
                    uintptr_t nid = baseId++;

                    vs["nodes"].push_back({
                        {"id", nid},
                        {"taskName", jobPtr->name()},
                        {"x", static_cast<float>(jobPtr->x())},
                        {"y", static_cast<float>(jobPtr->y())},
                        {"inputPinId", nid * 10},
                        {"outputPinId", (nid * 10) + 1}
                    });
                }

                swf.visualState = std::move(vs);
                m_savedWorkflows.push_back(std::move(swf));
            }
        }

        std::string windowTitle = "dizui Studio - " + m_currentProjectName + " [" + m_currentProjectFilePath + "]";
        if (GLFWwindow *currentWin = glfwGetCurrentContext())
        {
            glfwSetWindowTitle(currentWin, windowTitle.c_str());
        }

        std::cout << "[dizui Info] Projet chargé (" << m_activeProject->datasets().size()
                  << " datasets, " << m_savedQueries.size() << " requêtes, "
                  << m_savedTasks.size() << " tâches, " << m_savedWorkflows.size()
                  << " workflows restaurés) : " << filePath << std::endl;
        return true;

    }
    catch (const std::exception &e)
    {
        std::cerr << "[dizui Erreur] Échec du chargement : " << e.what() << std::endl;
        return false;
    }
}

void Application::loadQueryIntoVisualDesigner(const SavedQuery &savedQuery)
{
    m_projectionItems.clear(); m_hasProjectionNode = false;
    // Pré-remplissage des champs de sauvegarde avec les métadonnées de la requête éditée
    strncpy(m_saveQueryNameBuf, savedQuery.name.c_str(), sizeof(m_saveQueryNameBuf) - 1);
    m_saveQueryNameBuf[sizeof(m_saveQueryNameBuf) - 1] = '\0';

    strncpy(m_saveQueryDescBuf, savedQuery.description.c_str(), sizeof(m_saveQueryDescBuf) - 1);
    m_saveQueryDescBuf[sizeof(m_saveQueryDescBuf) - 1] = '\0';

    // 1. Nettoyage complet du canevas de requêtes
    m_visualQueryNodes.clear();
    m_visualQueryLinks.clear();
    m_visualQueryPredicates.clear();
    m_editingJoinLink = nullptr;
    m_editingPredicate = nullptr;

    if (savedQuery.visualState.is_null() || savedQuery.visualState.empty())
    {
        std::cout << "[dizui Info] Requête '" << savedQuery.name << "' sans graphe visuel associé." << std::endl;
        return;
    }

    const auto &vState = savedQuery.visualState;

    // 2. Restauration des nœuds de tables (Datasets)
    if (vState.contains("nodes") && vState["nodes"].is_array())
    {
        for (const auto &nJson : vState["nodes"])
        {
            VisualQueryNode node;
            node.id = nJson.value("id", uintptr_t(0));
            node.datasetName = nJson.value("datasetName", "");
            node.alias = nJson.value("alias", "");
            node.position = ImVec2(nJson.value("x", 100.0f), nJson.value("y", 100.0f));
            node.needsPositionInitialization = true;
            node.isCTE = nJson.value("isCTE", false);

            if (nJson.contains("selectedColumns") && nJson["selectedColumns"].is_array())
            {
                for (const auto &colIdx : nJson["selectedColumns"])
                {
                    node.selectedColumns.insert(colIdx.get<size_t>());
                }
            }
            m_visualQueryNodes.push_back(std::move(node));
        }
    }


    // 3. Restauration des liens de jointures (VisualQueryLink)
    // Restauration des liens et opérateurs de jointure
    if (vState.contains("links") && vState["links"].is_array())
    {
        ed::SetCurrentEditor(m_queryContext);
        for (const auto &lJson : vState["links"])
        {
            VisualQueryLink link;
            link.id = lJson.value("id", uintptr_t(0));
            link.startPinId = lJson.value("startPinId", uintptr_t(0));
            link.endPinId = lJson.value("endPinId", uintptr_t(0));
            link.centralNodeId = lJson.value("centralNodeId", link.id + 5000);
            link.leftPinId = lJson.value("leftPinId", link.id + 10000);
            link.rightPinId = lJson.value("rightPinId", link.id + 15000);
            link.type = static_cast<cppsqlx::JoinType>(lJson.value("type", 0));
            link.leftFieldName = lJson.value("leftFieldName", "");
            link.rightFieldName = lJson.value("rightFieldName", "");
            link.op = static_cast<cppsqlx::SQLOperator>(lJson.value("op", 0));

            float jx = lJson.value("joinX", 0.0f);
            float jy = lJson.value("joinY", 0.0f);
            ed::SetNodePosition(link.centralNodeId, ImVec2(jx, jy));

            m_visualQueryLinks.push_back(std::move(link));
        }
        ed::SetCurrentEditor(nullptr);
    }


    // 4. Restauration des filtres (VisualQueryPredicate)
    if (vState.contains("predicates") && vState["predicates"].is_array())
    {
        for (const auto &pJson : vState["predicates"])
        {
            VisualQueryPredicate pred;
            pred.id = pJson.value("id", uintptr_t(0));
            pred.position = ImVec2(pJson.value("x", 100.0f), pJson.value("y", 100.0f));
            pred.needsPositionInitialization = true;
            pred.inputPinId = pJson.value("inputPinId", uintptr_t(0));
            pred.outputPinId = pJson.value("outputPinId", uintptr_t(0));
            pred.targetColumn = pJson.value("targetColumn", "");
            pred.op = static_cast<cppsqlx::SQLOperator>(pJson.value("op", 0));
            pred.value = pJson.value("value", "");
            pred.connectedSrcPinId = pJson.value("connectedSrcPinId", uintptr_t(0));
            pred.isCustomExpression = pJson.value("isCustomExpression", false);
            pred.customSqlCondition = pJson.value("customSqlCondition", "");


            m_visualQueryPredicates.push_back(std::move(pred));
        }
    }

    // 5. Restauration de la projection unifiée et des expressions calculées
    if (vState.contains("projections") && vState["projections"].is_array())
    {
        for (const auto &projJson : vState["projections"])
        {
            VisualProjectionItem item;
            item.id = projJson.value("id", uintptr_t(0));
            item.alias = projJson.value("alias", "");
            item.sqlExpression = projJson.value("sqlExpression", "");
            item.isAggregate = projJson.value("isAggregate", false);
            item.isCustomExpression = projJson.value("isCustomExpression", false);
            m_projectionItems.push_back(std::move(item));
        }

        m_hasProjectionNode = vState.value("hasProjectionNode", !m_projectionItems.empty());
        if (m_hasProjectionNode)
        {
            m_projectionNodePos = ImVec2(
                vState.value("projectionX", 650.0f),
                vState.value("projectionY", 120.0f)
            );
            m_needsProjectionPosInit = true;
        }
    }
    else if (!m_visualQueryNodes.empty() && m_activeProject)
    {
        // Rétrocompatibilité : reconstruit automatiquement m_projectionItems depuis les anciennes requêtes
        for (const auto &node : m_visualQueryNodes)
        {
            if (!m_activeProject->datasets().contains(node.datasetName))
                continue;
            const auto &ds = m_activeProject->datasets().at(node.datasetName);
            for (size_t colIdx : node.selectedColumns)
            {
                if (ds.columns().contains(colIdx))
                {
                    std::string colName = ds.columns().at(colIdx).name();
                    std::string qual = node.alias.empty() ? colName : (node.alias + "." + colName);
                    std::string defAlias = node.alias.empty() ? colName : (node.alias + "_" + colName);
                    uintptr_t nextId = m_projectionItems.size() + 70000;
                    m_projectionItems.push_back(VisualProjectionItem{nextId, defAlias, qual, false, false});
                }
            }
        }
        if (!m_projectionItems.empty())
        {
            m_hasProjectionNode = true;
            m_projectionNodePos = ImVec2(650.0f, 120.0f);
            m_needsProjectionPosInit = true;
        }
    }


    m_needsQueryCentering = true;

    std::cout << "[dizui Info] Graphe visuel de '" << savedQuery.name << "' restauré." << std::endl;
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

void Application::executeVisualQuery(const std::string &sql)
{
    m_queryResultRows.clear();
    m_executionError = "";

    if (sql.empty())
    {
        m_executionError = "Graphe vide ou impossible à compiler.";
        return;
    }

    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "- REQUÊTE COMPILÉE SOUMISE À POSTGRESQL :" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << sql << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;

    try
    {
        if (m_activeProject && m_activeProject->logicalConnections().contains(m_selectedGateway))
        {
            const auto &gateway = m_activeProject->logicalConnections().at(m_selectedGateway);
            if (gateway.environments().contains(m_selectedContext))
            {
                const auto &phys = gateway.environments().at(m_selectedContext);

                cppsqlx::Connection execConn(phys.toConnectionString());
                auto connectRes = execConn.connect();

                if (execConn.isConnected())
                {
                    std::unique_ptr<cppsqlx::Statement> stmt = execConn.createStatement();
                    if (stmt)
                    {
                        auto rowsRes = stmt->execute(sql);
                        if (rowsRes.has_value())
                        {
                            const auto &resultSet = rowsRes.value();
                            for (const auto &row : resultSet)
                            {
                                std::vector<std::string> targetRow;
                                for (size_t col_idx = 0; col_idx < m_queryResultHeaders.size(); ++col_idx)
                                {
                                    auto cellResult = row[col_idx];
                                    targetRow.push_back(cellResult.value_or("NULL"));
                                }
                                m_queryResultRows.push_back(std::move(targetRow));
                            }
                        }
                        else
                        {
                            m_executionError = rowsRes.error();
                        }
                    }
                }
                else
                {
                    m_executionError = "Échec de connexion : " + (!connectRes ? connectRes.error() : "Impossible de joindre le serveur cible.");
                }
            }

        }

        m_showQueryResultsPanel = true;
        m_focusQueryResultsTab = true;
    }
    catch (const std::exception &e)
    {
        m_executionError = std::string("Erreur SQL : ") + e.what();
    }
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
    renderConsolePanel();renderQueryResultsPanel();
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
        colors[ImGuiCol_WindowBg]         = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_ChildBg]          = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border]           = ImVec4(0.80f, 0.82f, 0.86f, 1.00f);
        colors[ImGuiCol_FrameBg]          = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
        colors[ImGuiCol_Header]           = ImVec4(0.22f, 0.52f, 0.88f, 0.25f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.22f, 0.52f, 0.88f, 0.40f);
        colors[ImGuiCol_Button]           = ImVec4(0.86f, 0.88f, 0.92f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = ImVec4(0.22f, 0.52f, 0.88f, 0.35f);
        canvasBg   = ImVec4(0.92f, 0.93f, 0.96f, 1.00f);
        canvasGrid = ImVec4(0.82f, 0.84f, 0.88f, 0.60f);
        break;
    }

    case AppTheme::Nord:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text]             = ImVec4(0.93f, 0.94f, 0.96f, 1.00f); // Snow Storm
        colors[ImGuiCol_WindowBg]         = ImVec4(0.18f, 0.20f, 0.25f, 1.00f); // Polar Night #2E3440
        colors[ImGuiCol_ChildBg]          = ImVec4(0.21f, 0.24f, 0.29f, 1.00f); // #3B4252
        colors[ImGuiCol_PopupBg]          = ImVec4(0.18f, 0.20f, 0.25f, 0.98f);
        colors[ImGuiCol_Border]           = ImVec4(0.30f, 0.34f, 0.42f, 1.00f); // #4C566A
        colors[ImGuiCol_FrameBg]          = ImVec4(0.26f, 0.30f, 0.37f, 1.00f); // #434C5E
        colors[ImGuiCol_TitleBgActive]    = ImVec4(0.37f, 0.51f, 0.67f, 1.00f); // Frost #5E81AC
        colors[ImGuiCol_Header]           = ImVec4(0.53f, 0.75f, 0.82f, 0.30f); // Frost #88C0D0
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.53f, 0.75f, 0.82f, 0.45f);
        colors[ImGuiCol_Button]           = ImVec4(0.26f, 0.30f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = ImVec4(0.37f, 0.51f, 0.67f, 0.65f);
        canvasBg   = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
        canvasGrid = ImVec4(0.26f, 0.30f, 0.37f, 0.50f);
        break;
    }

    case AppTheme::Dracula:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text]             = ImVec4(0.97f, 0.97f, 0.95f, 1.00f); // Foreground #F8F8F2
        colors[ImGuiCol_WindowBg]         = ImVec4(0.16f, 0.16f, 0.21f, 1.00f); // Background #282A36
        colors[ImGuiCol_ChildBg]          = ImVec4(0.19f, 0.20f, 0.26f, 1.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(0.16f, 0.16f, 0.21f, 0.98f);
        colors[ImGuiCol_Border]           = ImVec4(0.38f, 0.45f, 0.64f, 0.70f); // Comment #6272A4
        colors[ImGuiCol_FrameBg]          = ImVec4(0.27f, 0.28f, 0.35f, 1.00f); // Current Line #44475A
        colors[ImGuiCol_TitleBgActive]    = ImVec4(0.74f, 0.58f, 0.98f, 0.85f); // Purple #BD93F9
        colors[ImGuiCol_Header]           = ImVec4(0.74f, 0.58f, 0.98f, 0.30f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(1.00f, 0.47f, 0.78f, 0.40f); // Pink #FF79C6
        colors[ImGuiCol_Button]           = ImVec4(0.27f, 0.28f, 0.35f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = ImVec4(0.74f, 0.58f, 0.98f, 0.50f);
        canvasBg   = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
        canvasGrid = ImVec4(0.27f, 0.28f, 0.35f, 0.55f);
        break;
    }

    case AppTheme::DarkModern:
    default:
    {
        ImGui::StyleColorsDark();
        colors[ImGuiCol_Text]             = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
        colors[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_ChildBg]          = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
        colors[ImGuiCol_PopupBg]          = ImVec4(0.11f, 0.12f, 0.15f, 0.98f);
        colors[ImGuiCol_Border]           = ImVec4(0.24f, 0.26f, 0.31f, 1.00f);
        colors[ImGuiCol_FrameBg]          = ImVec4(0.17f, 0.19f, 0.23f, 1.00f);
        colors[ImGuiCol_Header]           = ImVec4(0.20f, 0.45f, 0.75f, 0.35f);
        colors[ImGuiCol_HeaderHovered]    = ImVec4(0.25f, 0.52f, 0.85f, 0.50f);
        colors[ImGuiCol_Button]           = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonHovered]    = ImVec4(0.25f, 0.52f, 0.85f, 0.45f);
        canvasBg   = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        canvasGrid = ImVec4(0.22f, 0.24f, 0.28f, 0.45f);
        break;
    }
    }

    // Application des couleurs de grille sur les deux éditeurs de nœuds
    auto applyNodeCanvasStyle = [&](ed::EditorContext *ctx)
    {
        if (!ctx) return;
        ed::SetCurrentEditor(ctx);
        ed::GetStyle().Colors[ed::StyleColor_Bg]   = canvasBg;
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
                                               [&finalName](const SavedQuery &q) { return q.name == finalName; }))
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
                                                   [&taskToDuplicate](const cppsqlx::Task &t) { return t.name() == taskToDuplicate; });
                            if (it != m_savedTasks.end())
                            {
                                std::string copyName = taskToDuplicate + "_copie";
                                int suffix = 2;
                                while (std::any_of(m_savedTasks.begin(), m_savedTasks.end(),
                                                   [&copyName](const cppsqlx::Task &t) { return t.name() == copyName; }))
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

                // 7. Registre des Jobs
                if (ImGui::TreeNode("Registre des Jobs"))
                {
                    ImGui::TreePop();
                }
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
            "Dracula (Nocturne)"
        };

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
                    m_cachedTables.clear();    // Force le rechargement des tables du nouveau schéma
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
            std::vector<const std::string*> filteredTables;
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

        m_projectionItems.clear(); m_hasProjectionNode = false;
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
    //renderQueryCanvas(canvas_height);

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
                vState["nodes"].push_back({
                    {"id", n.id},
                    {"datasetName", n.datasetName},
                    {"x", pos.x},
                    {"y", pos.y},
                    {"alias", n.alias},
                    {"selectedColumns", n.selectedColumns},
                    {"isCTE", n.isCTE}
                });
            }

            // 2b. Liens et opérateurs de jointure (JOIN)
            vState["links"] = nlohmann::json::array();
            for (const auto &l : m_visualQueryLinks)
            {
                ImVec2 joinPos = ed::GetNodePosition(l.centralNodeId);
                vState["links"].push_back({
                    {"id", l.id},
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
                    {"joinY", joinPos.y}
                });
            }

            // 2c. Filtres (WHERE)
            vState["predicates"] = nlohmann::json::array();
            for (const auto &p : m_visualQueryPredicates)
            {
                ImVec2 pPos = ed::GetNodePosition(p.id);
                vState["predicates"].push_back({
                    {"id", p.id},
                    {"x", pPos.x},
                    {"y", pPos.y},
                    {"inputPinId", p.inputPinId},
                    {"outputPinId", p.outputPinId},
                    {"targetColumn", p.targetColumn},
                    {"op", static_cast<int>(p.op)},
                    {"value", p.value},
                    {"connectedSrcPinId", p.connectedSrcPinId},
                    {"isCustomExpression", p.isCustomExpression},
                    {"customSqlCondition", p.customSqlCondition}
                });
            }

            // 2d. Projection unifiée (SELECT / Expressions)
            vState["projections"] = nlohmann::json::array();
            for (const auto &proj : m_projectionItems)
            {
                vState["projections"].push_back({
                    {"id", proj.id},
                    {"alias", proj.alias},
                    {"sqlExpression", proj.sqlExpression},
                    {"isAggregate", proj.isAggregate},
                    {"isCustomExpression", proj.isCustomExpression},
                    {"sourceNodeId", proj.sourceNodeId}
                });
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
                std::move(deducedSchema)
            };

            auto it = std::find_if(m_savedQueries.begin(), m_savedQueries.end(),
                                   [&qName](const SavedQuery &q) { return q.name == qName; });
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

            if (link.type == cppsqlx::JoinType::LEFT)
            {
                join_label = "LEFT JOIN";
                icon_color = ImVec4(0.40f, 0.80f, 0.40f, 1.0f);
            }
            if (link.type == cppsqlx::JoinType::RIGHT)
            {
                join_label = "RIGHT JOIN";
                icon_color = ImVec4(0.85f, 0.65f, 0.20f, 1.0f);
            }
            if (link.type == cppsqlx::JoinType::FULL)
            {
                join_label = "FULL JOIN";
                icon_color = ImVec4(0.90f, 0.40f, 0.90f, 1.0f);
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
            std::string display_expr = src_alias + "." + left_pure + " " + op_symbol + " " + dst_alias + "." + right_pure;

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
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "%s  Configuration de l'Opérateur JOIN", ICON_LC_SETTINGS);
        ImGui::Separator();
        ImGui::Spacing();
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

        uintptr_t s_node = m_editingJoinLink->startPinId / 1000;
        uintptr_t d_node = m_editingJoinLink->endPinId / 1000;
        std::string current_src_alias = "";
        std::string current_dst_alias = "";
        for (const auto &n : m_visualQueryNodes)
        {
            if (n.id == s_node)
                current_src_alias = n.alias;
            if (n.id == d_node)
                current_dst_alias = n.alias;
        }
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
"COALESCE(, 0)", "EXTRACT(YEAR FROM )"
};
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
            if (i > 0 && ImGui::SmallButton("^")) indexMoveUp = i;
            ImGui::SameLine();
            if (i + 1 < static_cast<int>(m_projectionItems.size()) && ImGui::SmallButton("v")) indexMoveDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) indexToDelete = i;

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

        struct SqlFuncTemplate { const char *label; const char *snippet; bool isAgg; };
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
            {"MAX(col)", "MAX()", true}
        };

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
                    textColor = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);     // Succès (Vert)
                else if (entry.level == LogLevel::Warning)
                    textColor = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);     // Avertissement (Jaune/Ambre)
                else if (entry.level == LogLevel::Error)
                    textColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);     // Erreur (Rouge)

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
                                   [this](const cppsqlx::Task &t) { return t.name() == m_targetTaskName; });
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
                                       [&newName](const cppsqlx::Task &t) { return t.name() == newName; }));

        if (!canRename)
            ImGui::BeginDisabled();
        if (ImGui::Button("Valider", ImVec2(110.0f, 0.0f)))
        {
            if (newName != m_targetTaskName)
            {
                auto it = std::find_if(m_savedTasks.begin(), m_savedTasks.end(),
                                       [this](const cppsqlx::Task &t) { return t.name() == m_targetTaskName; });
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
            std::erase_if(m_savedTasks, [this](const cppsqlx::Task &t) { return t.name() == m_targetTaskName; });
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
        std::string formatted = formatRawSqlQuery(std::string(m_rawSqlEditorBuf.data()));
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
    SqlEditorCallbackUserData cbData{
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
        sqlTextEditorCallback,
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
    renderSqlSyntaxHighlightOverlay(drawList, textStartPos, m_rawSqlEditorBuf.data(), ImGui::GetFont(), ImGui::GetFontSize());
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
    cfg.NavigateButtonIndex = 1;    // Clic droit maintenu pour déplacer le graphe
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
                           [&cteQueryName](const SavedQuery &q) { return q.name == cteQueryName; });

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
        ImGui::SameLine();
        ImGui::TextDisabled("  |  Disposition :");
        ImGui::SameLine();
        if (m_cteStackHorizontal)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.52f, 0.85f, 0.90f));
        if (ImGui::Button("Horizontale", ImVec2(105.0f, 28.0f)))
            m_cteStackHorizontal = true;
        if (m_cteStackHorizontal)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (!m_cteStackHorizontal)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.52f, 0.85f, 0.90f));
        if (ImGui::Button("Verticale", ImVec2(95.0f, 28.0f)))
            m_cteStackHorizontal = false;
        if (!m_cteStackHorizontal)
            ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Spacing();

        // Zone défilante principale
        ImGuiWindowFlags scrollFlags = m_cteStackHorizontal ? ImGuiWindowFlags_HorizontalScrollbar : ImGuiWindowFlags_None;
        ImGui::BeginChild("CteStackScrollRegion", ImVec2(-1.0f, -1.0f), false, scrollFlags);

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
