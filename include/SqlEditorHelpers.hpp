#ifndef DIZUI_SQL_EDITOR_HELPERS_HPP
#define DIZUI_SQL_EDITOR_HELPERS_HPP

#include <string>
#include <imgui.h>

namespace dizui::sql
{
    struct SqlEditorCallbackUserData
    {
        int *cursorPos = nullptr;
        std::string *pendingInsertion = nullptr;
        int replaceStart = 0;
        int replaceEnd = 0;
    };

    // Formate une requête SQL brute (mots-clés en majuscules, sauts de ligne et indentation)
    std::string formatRawSqlQuery(const std::string &rawSql);

    // Superpose la coloration syntaxique SQL en temps réel sur un InputTextMultiline transparent
    void renderSqlSyntaxHighlightOverlay(ImDrawList *drawList, ImVec2 textOrigin, const char *text, ImFont *font, float fontSize);

    // Callback ImGui gérant la position du curseur et l'insertion de l'autocomplétion
    int sqlTextEditorCallback(ImGuiInputTextCallbackData *data);
}

#endif // DIZUI_SQL_EDITOR_HELPERS_HPP
