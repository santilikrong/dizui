#pragma once
#include <string>
#include <vector>

namespace dizui::sql
{
    enum class SqlCategory
    {
        All = 0,
        Logical,
        Comparison,
        Conditional,
        Mathematical,
        String,
        PatternMatching,
        Formatting,
        DateTime,
        Json,
        Xml,
        Array,
        TextSearch,
        Aggregate,
        Window,
        Subquery,
        MergeSupport
    };

    struct SqlFunctionEntry
    {
        SqlCategory category;
        const char *label;   // Affichage dans la liste
        const char *snippet; // Code inséré au curseur
        const char *descKey; // Clé de traduction pour l'infobulle
        bool isAggregate = false;
        bool isWindow = false;
    };

    const std::vector<SqlCategory> &getAllCategories();
    const char *getCategoryTranslationKey(SqlCategory cat);
    const std::vector<SqlFunctionEntry> &getSqlFunctionsCatalog();
}
