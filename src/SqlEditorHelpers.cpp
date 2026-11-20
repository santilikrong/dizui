#include "SqlEditorHelpers.hpp"
#include <vector>
#include <unordered_set>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace dizui::sql
{
    std::string formatRawSqlQuery(const std::string &rawSql)
    {
        if (rawSql.empty())
            return "";

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
                tok = up;

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
                    emitNewLine(1);
                else
                    newLineJustEmitted = false;
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
                out << " ";
            out << tok;
            newLineJustEmitted = false;
        }

        return out.str();
    }

    void renderSqlSyntaxHighlightOverlay(ImDrawList *drawList, ImVec2 textOrigin, const char *text, ImFont *font, float fontSize)
    {
        if (!text || !*text)
            return;

        const ImU32 colKeyword = IM_COL32(86, 156, 214, 255);
        const ImU32 colFunction = IM_COL32(220, 220, 170, 255);
        const ImU32 colString = IM_COL32(206, 145, 120, 255);
        const ImU32 colNumber = IM_COL32(181, 206, 168, 255);
        const ImU32 colComment = IM_COL32(106, 153, 85, 255);
        const ImU32 colOperator = IM_COL32(180, 180, 180, 255);
        const ImU32 colDefault = IM_COL32(220, 224, 232, 255);

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

            if (*p == '-' && *(p + 1) == '-')
            {
                const char *start = p;
                while (*p && *p != '\n')
                    ++p;
                drawList->AddText(font, fontSize, cursor, colComment, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            if (*p == '\'')
            {
                const char *start = p++;
                while (*p)
                {
                    if (*p == '\'' && *(p + 1) == '\'')
                        p += 2;
                    else if (*p == '\'')
                    {
                        ++p;
                        break;
                    }
                    else if (*p == '\n')
                        break;
                    else
                        ++p;
                }
                drawList->AddText(font, fontSize, cursor, colString, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(*p)))
            {
                const char *start = p;
                while (*p && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.'))
                    ++p;
                drawList->AddText(font, fontSize, cursor, colNumber, start, p);
                cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
                continue;
            }
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
            const char *start = p++;
            drawList->AddText(font, fontSize, cursor, colOperator, start, p);
            cursor.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, start, p).x;
        }
    }
    int sqlTextEditorCallback(ImGuiInputTextCallbackData *data)
    {
        auto *userData = static_cast<SqlEditorCallbackUserData *>(data->UserData);
        if (!userData)
            return 0;
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
            *userData->cursorPos = data->CursorPos;
        return 0;
    }
}