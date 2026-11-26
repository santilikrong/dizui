#include "SqlFunctionsCatalog.hpp"

namespace dizui::sql
{
    const std::vector<SqlCategory> &getAllCategories()
    {
        static const std::vector<SqlCategory> cats = {
            SqlCategory::All,
            SqlCategory::Logical,
            SqlCategory::Comparison,
            SqlCategory::Conditional,
            SqlCategory::Mathematical,
            SqlCategory::String,
            SqlCategory::PatternMatching,
            SqlCategory::Formatting,
            SqlCategory::DateTime,
            SqlCategory::Json,
            SqlCategory::Xml,
            SqlCategory::Array,
            SqlCategory::TextSearch,
            SqlCategory::Aggregate,
            SqlCategory::Window,
            SqlCategory::Subquery,
            SqlCategory::MergeSupport};
        return cats;
    }

    const char *getCategoryTranslationKey(SqlCategory cat)
    {
        switch (cat)
        {
        case SqlCategory::All:
            return "sql_cat.all";
        case SqlCategory::Logical:
            return "sql_cat.logical";
        case SqlCategory::Comparison:
            return "sql_cat.comparison";
        case SqlCategory::Conditional:
            return "sql_cat.conditional";
        case SqlCategory::Mathematical:
            return "sql_cat.mathematical";
        case SqlCategory::String:
            return "sql_cat.string";
        case SqlCategory::PatternMatching:
            return "sql_cat.pattern";
        case SqlCategory::Formatting:
            return "sql_cat.formatting";
        case SqlCategory::DateTime:
            return "sql_cat.datetime";
        case SqlCategory::Json:
            return "sql_cat.json";
        case SqlCategory::Xml:
            return "sql_cat.xml";
        case SqlCategory::Array:
            return "sql_cat.array";
        case SqlCategory::TextSearch:
            return "sql_cat.textsearch";
        case SqlCategory::Aggregate:
            return "sql_cat.aggregate";
        case SqlCategory::Window:
            return "sql_cat.window";
        case SqlCategory::Subquery:
            return "sql_cat.subquery";
        case SqlCategory::MergeSupport:
            return "sql_cat.merge";
        }
        return "sql_cat.all";
    }

    const std::vector<SqlFunctionEntry> &getSqlFunctionsCatalog()
    {
        static const std::vector<SqlFunctionEntry> catalog = {
            // --- Logical Operators ---
            {SqlCategory::Logical, "AND", " AND ", "sql_desc.and"},
            {SqlCategory::Logical, "OR", " OR ", "sql_desc.or"},
            {SqlCategory::Logical, "NOT", "NOT ()", "sql_desc.not"},

            // --- Comparison Functions & Operators ---
            {SqlCategory::Comparison, "BETWEEN ... AND ...", " BETWEEN  AND ", "sql_desc.between"},
            {SqlCategory::Comparison, "IS NULL", " IS NULL", "sql_desc.is_null"},
            {SqlCategory::Comparison, "IS NOT NULL", " IS NOT NULL", "sql_desc.is_not_null"},
            {SqlCategory::Comparison, "IS DISTINCT FROM", " IS DISTINCT FROM ", "sql_desc.is_distinct"},
            {SqlCategory::Comparison, "NUM_NONNULLS(a, b)", "NUM_NONNULLS(, )", "sql_desc.num_nonnulls"},
            {SqlCategory::Comparison, "NUM_NULLS(a, b)", "NUM_NULLS(, )", "sql_desc.num_nulls"},

            // --- Conditional Expressions ---
            {SqlCategory::Conditional, "CASE WHEN ... THEN ... ELSE ... END", "CASE WHEN  THEN  ELSE  END", "sql_desc.case_when"},
            {SqlCategory::Conditional, "COALESCE(val, fallback)", "COALESCE(, )", "sql_desc.coalesce"},
            {SqlCategory::Conditional, "NULLIF(val1, val2)", "NULLIF(, )", "sql_desc.nullif"},
            {SqlCategory::Conditional, "GREATEST(a, b, ...)", "GREATEST(, )", "sql_desc.greatest"},
            {SqlCategory::Conditional, "LEAST(a, b, ...)", "LEAST(, )", "sql_desc.least"},

            // --- Mathematical Functions ---
            {SqlCategory::Mathematical, "ABS(x)", "ABS()", "sql_desc.abs"},
            {SqlCategory::Mathematical, "ROUND(x, n)", "ROUND(, 2)", "sql_desc.round"},
            {SqlCategory::Mathematical, "CEIL(x) / FLOOR(x)", "CEIL()", "sql_desc.ceil"},
            {SqlCategory::Mathematical, "TRUNC(x, n)", "TRUNC(, 2)", "sql_desc.trunc"},
            {SqlCategory::Mathematical, "MOD(y, x)", "MOD(, )", "sql_desc.mod"},
            {SqlCategory::Mathematical, "POWER(a, b)", "POWER(, )", "sql_desc.power"},
            {SqlCategory::Mathematical, "SQRT(x)", "SQRT()", "sql_desc.sqrt"},
            {SqlCategory::Mathematical, "RANDOM()", "RANDOM()", "sql_desc.random"},

            // --- String Functions & Operators ---
            {SqlCategory::String, "a || b (Concaténation)", " || ", "sql_desc.concat_op"},
            {SqlCategory::String, "CONCAT(a, b, ...)", "CONCAT(, ' ', )", "sql_desc.concat"},
            {SqlCategory::String, "CONCAT_WS(sep, a, b)", "CONCAT_WS(', ', , )", "sql_desc.concat_ws"},
            {SqlCategory::String, "UPPER(str) / LOWER(str)", "UPPER()", "sql_desc.upper"},
            {SqlCategory::String, "INITCAP(str)", "INITCAP()", "sql_desc.initcap"},
            {SqlCategory::String, "TRIM(str)", "TRIM()", "sql_desc.trim"},
            {SqlCategory::String, "SUBSTRING(str FROM 1 FOR 3)", "SUBSTRING( FROM 1 FOR 3)", "sql_desc.substring"},
            {SqlCategory::String, "LEFT(str, n) / RIGHT(str, n)", "LEFT(, 3)", "sql_desc.left"},
            {SqlCategory::String, "LENGTH(str)", "LENGTH()", "sql_desc.length"},
            {SqlCategory::String, "REPLACE(str, from, to)", "REPLACE(, '', '')", "sql_desc.replace"},
            {SqlCategory::String, "REGEXP_REPLACE(str, pat, rep)", "REGEXP_REPLACE(, '', '', 'g')", "sql_desc.regexp_replace"},
            {SqlCategory::String, "SPLIT_PART(str, delim, pos)", "SPLIT_PART(, ',', 1)", "sql_desc.split_part"},
            {SqlCategory::String, "LPAD(str, len, fill)", "LPAD(, 5, '0')", "sql_desc.lpad"},

            // --- Pattern Matching ---
            {SqlCategory::PatternMatching, "LIKE '%...%'", " LIKE '%'", "sql_desc.like"},
            {SqlCategory::PatternMatching, "ILIKE '%...%' (Insensible casse)", " ILIKE '%'", "sql_desc.ilike"},
            {SqlCategory::PatternMatching, "SIMILAR TO '...'", " SIMILAR TO ''", "sql_desc.similar_to"},
            {SqlCategory::PatternMatching, "~ 'regex' (POSIX)", " ~ ''", "sql_desc.regex_match"},
            {SqlCategory::PatternMatching, "~* 'regex' (POSIX Insensible)", " ~* ''", "sql_desc.regex_imatch"},

            // --- Data Type Formatting ---
            {SqlCategory::Formatting, "CAST(x AS type)", "CAST( AS TEXT)", "sql_desc.cast"},
            {SqlCategory::Formatting, "x::numeric / x::text", "::numeric", "sql_desc.pg_cast"},
            {SqlCategory::Formatting, "TO_CHAR(timestamp, fmt)", "TO_CHAR(, 'YYYY-MM-DD')", "sql_desc.to_char"},
            {SqlCategory::Formatting, "TO_DATE(str, fmt)", "TO_DATE(, 'YYYY-MM-DD')", "sql_desc.to_date"},
            {SqlCategory::Formatting, "TO_TIMESTAMP(str, fmt)", "TO_TIMESTAMP(, 'YYYY-MM-DD HH24:MI:SS')", "sql_desc.to_timestamp"},
            {SqlCategory::Formatting, "TO_NUMBER(str, fmt)", "TO_NUMBER(, '999G999D99')", "sql_desc.to_number"},

            // --- Date/Time Functions & Operators ---
            {SqlCategory::DateTime, "NOW() / CURRENT_TIMESTAMP", "NOW()", "sql_desc.now"},
            {SqlCategory::DateTime, "CURRENT_DATE", "CURRENT_DATE", "sql_desc.current_date"},
            {SqlCategory::DateTime, "DATE_TRUNC('month', ts)", "DATE_TRUNC('month', )", "sql_desc.date_trunc"},
            {SqlCategory::DateTime, "EXTRACT(YEAR FROM ts)", "EXTRACT(YEAR FROM )", "sql_desc.extract"},
            {SqlCategory::DateTime, "AGE(ts1, ts2)", "AGE(NOW(), )", "sql_desc.age"},
            {SqlCategory::DateTime, "+ INTERVAL '1 day'", " + INTERVAL '1 day'", "sql_desc.interval"},

            // --- JSON Functions & Operators ---
            {SqlCategory::Json, "col -> 'key' (Objet JSON)", " -> ''", "sql_desc.json_get"},
            {SqlCategory::Json, "col ->> 'key' (Texte)", " ->> ''", "sql_desc.json_get_text"},
            {SqlCategory::Json, "col #>> '{a,b}' (Chemin texte)", " #>> '{}'", "sql_desc.json_path_text"},
            {SqlCategory::Json, "col @> '{\"k\":\"v\"}'::jsonb", " @> '{}'::jsonb", "sql_desc.json_contains"},
            {SqlCategory::Json, "JSONB_EXTRACT_PATH_TEXT(col, 'k')", "JSONB_EXTRACT_PATH_TEXT(, '')", "sql_desc.jsonb_extract"},
            {SqlCategory::Json, "JSONB_BUILD_OBJECT('k', val)", "JSONB_BUILD_OBJECT('', )", "sql_desc.jsonb_build"},
            {SqlCategory::Json, "JSONB_ARRAY_ELEMENTS(col)", "JSONB_ARRAY_ELEMENTS()", "sql_desc.jsonb_elements"},
            {SqlCategory::Json, "ROW_TO_JSON(t)", "ROW_TO_JSON()", "sql_desc.row_to_json"},

            // --- XML Functions ---
            {SqlCategory::Xml, "XPATH('/root/node/text()', xml)", "XPATH('//text()', )", "sql_desc.xpath"},
            {SqlCategory::Xml, "XMLELEMENT(NAME tag, val)", "XMLELEMENT(NAME , )", "sql_desc.xmlelement"},
            {SqlCategory::Xml, "XMLPARSE(DOCUMENT str)", "XMLPARSE(DOCUMENT )", "sql_desc.xmlparse"},

            // --- Array Functions & Operators ---
            {SqlCategory::Array, "ARRAY[a, b]", "ARRAY[, ]", "sql_desc.array_ctor"},
            {SqlCategory::Array, "col = ANY(ARRAY[...])", " = ANY(ARRAY[])", "sql_desc.any_array"},
            {SqlCategory::Array, "ARRAY_LENGTH(arr, 1)", "ARRAY_LENGTH(, 1)", "sql_desc.array_length"},
            {SqlCategory::Array, "ARRAY_TO_STRING(arr, ',')", "ARRAY_TO_STRING(, ',')", "sql_desc.array_to_string"},
            {SqlCategory::Array, "STRING_TO_ARRAY(str, ',')", "STRING_TO_ARRAY(, ',')", "sql_desc.string_to_array"},
            {SqlCategory::Array, "UNNEST(arr)", "UNNEST()", "sql_desc.unnest"},

            // --- Text Search Functions ---
            {SqlCategory::TextSearch, "TO_TSVECTOR('french', col)", "TO_TSVECTOR('french', )", "sql_desc.to_tsvector"},
            {SqlCategory::TextSearch, "PLAINTO_TSQUERY('french', q)", "PLAINTO_TSQUERY('french', '')", "sql_desc.plainto_tsquery"},
            {SqlCategory::TextSearch, "tsvec @@ tsquery (Match)", "TO_TSVECTOR('french', ) @@ PLAINTO_TSQUERY('french', '')", "sql_desc.ts_match"},
            {SqlCategory::TextSearch, "TS_RANK(tsvec, tsquery)", "TS_RANK(, )", "sql_desc.ts_rank"},
            // --- Aggregate Functions ---
            {SqlCategory::Aggregate, "COUNT()", "COUNT()", "sql_desc.count_all", true, false},
            {SqlCategory::Aggregate, "COUNT(DISTINCT col)", "COUNT(DISTINCT )", "sql_desc.count_distinct", true, false},
            {SqlCategory::Aggregate, "SUM(col)", "SUM()", "sql_desc.sum", true, false},
            {SqlCategory::Aggregate, "AVG(col)", "AVG()", "sql_desc.avg", true, false},
            {SqlCategory::Aggregate, "MIN(col) / MAX(col)", "MIN()", "sql_desc.minmax", true, false},
            {SqlCategory::Aggregate, "STRING_AGG(col, ', ')", "STRING_AGG(, ', ')", "sql_desc.string_agg", true, false},
            {SqlCategory::Aggregate, "ARRAY_AGG(col)", "ARRAY_AGG()", "sql_desc.array_agg", true, false},
            {SqlCategory::Aggregate, "JSONB_AGG(col)", "JSONB_AGG()", "sql_desc.jsonb_agg", true, false},
            {SqlCategory::Aggregate, "FILTER (WHERE ...)", " FILTER (WHERE )", "sql_desc.agg_filter", true, false},
            // --- Window Functions ---
            {SqlCategory::Window, "ROW_NUMBER() OVER (...)", "ROW_NUMBER() OVER (PARTITION BY  ORDER BY )", "sql_desc.row_number", false, true},
            {SqlCategory::Window, "RANK() OVER (...)", "RANK() OVER (PARTITION BY  ORDER BY )", "sql_desc.rank", false, true},
            {SqlCategory::Window, "DENSE_RANK() OVER (...)", "DENSE_RANK() OVER (PARTITION BY  ORDER BY )", "sql_desc.dense_rank", false, true},
            {SqlCategory::Window, "NTILE(4) OVER (...)", "NTILE(4) OVER (ORDER BY )", "sql_desc.ntile", false, true},
            {SqlCategory::Window, "LAG(col, 1) OVER (...)", "LAG(, 1) OVER (PARTITION BY  ORDER BY )", "sql_desc.lag", false, true},
            {SqlCategory::Window, "LEAD(col, 1) OVER (...)", "LEAD(, 1) OVER (PARTITION BY  ORDER BY )", "sql_desc.lead", false, true},
            {SqlCategory::Window, "FIRST_VALUE(col) OVER (...)", "FIRST_VALUE() OVER (PARTITION BY  ORDER BY )", "sql_desc.first_value", false, true},
            {SqlCategory::Window, "LAST_VALUE(col) OVER (...)", "LAST_VALUE() OVER (PARTITION BY  ORDER BY  ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)", "sql_desc.last_value", false, true},
            {SqlCategory::Window, "SUM(col) OVER (Cumul)", "SUM() OVER (PARTITION BY  ORDER BY )", "sql_desc.win_sum", false, true},
            {SqlCategory::Window, "AVG(col) OVER (Moyenne)", "AVG() OVER (PARTITION BY )", "sql_desc.win_avg", false, true},
            // --- Subquery Expressions ---
            {SqlCategory::Subquery, "EXISTS (SELECT 1 FROM ...)", "EXISTS (SELECT 1 FROM  WHERE )", "sql_desc.exists"},
            {SqlCategory::Subquery, "NOT EXISTS (SELECT 1 FROM ...)", "NOT EXISTS (SELECT 1 FROM  WHERE )", "sql_desc.not_exists"},
            {SqlCategory::Subquery, "IN (SELECT col FROM ...)", " IN (SELECT  FROM )", "sql_desc.in_subquery"},
            {SqlCategory::Subquery, "NOT IN (SELECT col FROM ...)", " NOT IN (SELECT  FROM )", "sql_desc.not_in_subquery"},
            // --- Merge Support Functions ---
            {SqlCategory::MergeSupport, "MERGE_ACTION() (PG 17+)", "MERGE_ACTION()", "sql_desc.merge_action"}};
        return catalog;
    }
}