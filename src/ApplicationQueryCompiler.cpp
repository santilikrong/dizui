#include "Application.hpp"
#include <cppsqlx/sqlliterals.hpp>
#include <cppsqlx/selectquery.hpp>
#include <cppsqlx/statement.hpp>
#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <sstream>

namespace ed = ax::NodeEditor;
using namespace cppsqlx::literals;

/*
cppsqlx::SelectQuery Application::compileVisualGraphToSelectQuery_old()
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
    // Cas spécial : Graphe d'assemblage Ensembliste via CTE intermédiaire
    // Génère : WITH req1 AS (...), req2 AS (...), req_union_... AS (...) SELECT ...
    // =========================================================================
    bool hasUnionLinks = std::any_of(m_visualQueryLinks.begin(), m_visualQueryLinks.end(),
                                     [](const VisualQueryLink &l)
                                     { return l.isSetOperation; });

    if (hasUnionLinks)
    {
        auto findNodeById = [&](uintptr_t id) -> const VisualQueryNode *
        {
            for (const auto &n : m_visualQueryNodes)
                if (n.id == id)
                    return &n;
            return nullptr;
        };

        auto findSavedQuery = [&](const std::string &name) -> const SavedQuery *
        {
            for (const auto &sq : m_savedQueries)
                if (sq.name == name)
                    return &sq;
            return nullptr;
        };

        auto buildWhereForNode = [&](uintptr_t targetNodeId, const std::string &aliasPrefix) -> std::string
        {
            std::string nodeWhere;
            for (const auto &pred : m_visualQueryPredicates)
            {
                if (pred.isCustomExpression || pred.connectedSrcPinId == 0 || pred.value.empty())
                    continue;

                if ((pred.connectedSrcPinId / 1000) != targetNodeId)
                    continue;

                std::string colName = pred.targetColumn;
                if (auto dot = colName.find('.'); dot != std::string::npos)
                    colName = colName.substr(dot + 1);

                std::string fullCol = aliasPrefix.empty() ? colName : (aliasPrefix + "." + colName);
                cppsqlx::SQLExpression cExpr = operator""_col(fullCol.c_str(), fullCol.length());

                cppsqlx::SQLExpression vExpr;
                try
                {
                    size_t idx = 0;
                    long long numVal = std::stoll(pred.value, &idx);
                    vExpr = (idx == pred.value.length()) ? cppsqlx::SQLExpression(static_cast<int64_t>(numVal))
                                                         : cppsqlx::SQLExpression(pred.value);
                }
                catch (...)
                {
                    vExpr = cppsqlx::SQLExpression(pred.value);
                }

                std::string singlePred;
                if (pred.op == cppsqlx::SQLOperator::Equal)
                    singlePred = cExpr.equal(vExpr).toSQL();
                else if (pred.op == cppsqlx::SQLOperator::NotEqual)
                    singlePred = cExpr.notEqual(vExpr).toSQL();
                else if (pred.op == cppsqlx::SQLOperator::Greater)
                    singlePred = cExpr.greater(vExpr).toSQL();
                else if (pred.op == cppsqlx::SQLOperator::Less)
                    singlePred = cExpr.less(vExpr).toSQL();
                else if (pred.op == cppsqlx::SQLOperator::GreaterOrEqual)
                    singlePred = cExpr.greaterOrEqual(vExpr).toSQL();
                else if (pred.op == cppsqlx::SQLOperator::LessOrEqual)
                    singlePred = cExpr.lessOrEqual(vExpr).toSQL();
                else
                    singlePred = cExpr.equal(vExpr).toSQL();

                nodeWhere = nodeWhere.empty() ? singlePred : ("(" + nodeWhere + ") AND (" + singlePred + ")");
            }
            return nodeWhere;
        };

        // Helper pour convertir un nœud (CTE ou Table physique) en SelectQuery élémentaire
        auto buildBranchSelectQuery = [&](const VisualQueryNode &node, std::string &outBranchName) -> std::optional<cppsqlx::SelectQuery>
        {
            outBranchName = node.alias.empty() ? node.datasetName : node.alias;
            // Nettoie d'éventuels points de schéma ("public.t1" -> "t1") pour un nom de CTE valide
            if (auto dot = outBranchName.find('.'); dot != std::string::npos)
                outBranchName = outBranchName.substr(dot + 1);

            if (node.isCTE)
            {
                const SavedQuery *sq = findSavedQuery(node.datasetName);
                if (sq && !sq->compiledSelectJson.is_null() && !sq->compiledSelectJson.empty())
                {
                    auto subRes = cppsqlx::SelectQuery::fromJSON(sq->compiledSelectJson);
                    if (subRes.has_value())
                    {
                        // Déclare la CTE source (ex: WITH req1 AS (...))
                        auto subPtr = std::make_shared<cppsqlx::SelectQuery>(std::move(*subRes));
                        query.with(outBranchName, subPtr);

                        // Branche de l'union : SELECT * FROM req1
                        cppsqlx::SelectQuery branchSelect;
                        branchSelect.select(operator""_col("*", 1));
                        auto cteRef = std::make_shared<cppsqlx::Dataset>(outBranchName);
                        branchSelect.from(cteRef, outBranchName);
                        return branchSelect;
                    }
                }
            }
            else if (m_activeProject && m_activeProject->datasets().contains(node.datasetName))
            {
                const auto &ds = m_activeProject->datasets().at(node.datasetName);
                cppsqlx::SelectQuery branchSelect;

                if (!node.selectedColumns.empty())
                {
                    for (size_t colIdx : node.selectedColumns)
                    {
                        if (ds.columns().contains(colIdx))
                        {
                            std::string cName = ds.columns().at(colIdx).name();
                            branchSelect.select(operator""_col(cName.c_str(), cName.length()));
                        }
                    }
                }
                else
                {
                    branchSelect.select(operator""_col("*", 1));
                }

                auto dsPtr = std::make_shared<cppsqlx::Dataset>(ds);
                branchSelect.from(dsPtr, outBranchName);

                std::string branchWhere = buildWhereForNode(node.id, outBranchName);
                if (!branchWhere.empty())
                {
                    branchSelect.where(cppsqlx::SQLExpression(branchWhere));
                }

                return branchSelect;
            }

            return std::nullopt;
        };

        // 1. Ordonner les nœuds impliqués dans la chaîne UNION
        std::vector<const VisualQueryNode *> unionChainNodes;
        std::vector<cppsqlx::SetOperator> unionOps;
        std::set<uintptr_t> unionedNodeIds;

        // Trouve le premier lien ensembliste pour démarrer la chaîne
        for (const auto &link : m_visualQueryLinks)
        {
            if (!link.isSetOperation)
                continue;

            const VisualQueryNode *src = findNodeById(link.startPinId / 1000);
            const VisualQueryNode *dst = findNodeById(link.endPinId / 1000);
            if (!src || !dst)
                continue;

            if (unionChainNodes.empty())
            {
                unionChainNodes.push_back(src);
                unionedNodeIds.insert(src->id);
            }
            if (!unionedNodeIds.contains(dst->id))
            {
                unionChainNodes.push_back(dst);
                unionOps.push_back(link.setOp);
                unionedNodeIds.insert(dst->id);
            }
            else if (!unionedNodeIds.contains(src->id))
            {
                unionChainNodes.push_back(src);
                unionOps.push_back(link.setOp);
                unionedNodeIds.insert(src->id);
            }
        }

        if (unionChainNodes.size() >= 2)
        {
            std::string firstName;
            auto unionSubQueryOpt = buildBranchSelectQuery(*unionChainNodes[0], firstName);
            std::string unionCteName = "req_union_" + firstName;

            if (unionSubQueryOpt.has_value())
            {
                for (size_t i = 1; i < unionChainNodes.size(); ++i)
                {
                    std::string nextName;
                    if (auto nextBranchOpt = buildBranchSelectQuery(*unionChainNodes[i], nextName))
                    {
                        unionCteName += "_" + nextName;
                        cppsqlx::SetOperator op = unionOps[i - 1];
                        if (op == cppsqlx::SetOperator::UNION_ALL)
                            unionSubQueryOpt->unionAll(*nextBranchOpt);
                        else if (op == cppsqlx::SetOperator::UNION)
                            unionSubQueryOpt->unionDistinct(*nextBranchOpt);
                        else if (op == cppsqlx::SetOperator::INTERSECT)
                            unionSubQueryOpt->intersect(*nextBranchOpt);
                        else if (op == cppsqlx::SetOperator::EXCEPT)
                            unionSubQueryOpt->except(*nextBranchOpt);
                    }
                }

                // Déclare la CTE d'assemblage : req_union_req1_req2 AS (SELECT * FROM req1 UNION SELECT * FROM req2)
                auto unionCtePtr = std::make_shared<cppsqlx::SelectQuery>(std::move(*unionSubQueryOpt));
                query.with(unionCteName, unionCtePtr);

                // Requête externe : SELECT <colonnes retenues> FROM req_union_req1_req2
                auto unionDsPtr = std::make_shared<cppsqlx::Dataset>(unionCteName);
                query.from(unionDsPtr, unionCteName);

                // Projection des colonnes retenues par l'utilisateur dans m_projectionItems
                cppsqlx::SQLSelectionSet outerSelect;
                bool hasOuterCols = false;

                if (!m_projectionItems.empty())
                {
                    for (const auto &item : m_projectionItems)
                    {
                        if (item.sqlExpression.empty())
                            continue;

                        std::string colOnly = item.sqlExpression;
                        if (!item.isCustomExpression)
                        {
                            if (auto dot = colOnly.find('.'); dot != std::string::npos)
                                colOnly = colOnly.substr(dot + 1);
                            colOnly = unionCteName + "." + colOnly;
                        }

                        cppsqlx::SQLExpression expr = operator""_col(colOnly.c_str(), colOnly.length());
                        if (!item.alias.empty())
                            expr = expr.as(item.alias);

                        outerSelect.add(expr);
                        local_select_headers.push_back(item.alias.empty() ? colOnly : item.alias);
                        hasOuterCols = true;
                    }
                }

                if (hasOuterCols)
                {
                    query.select(outerSelect);
                }
                else
                {
                    query.select(operator""_col("*", 1));
                    // Récupère les en-têtes depuis le schéma de la première branche
                    const auto *firstNode = unionChainNodes[0];
                    if (firstNode->isCTE)
                    {
                        if (const SavedQuery *sq = findSavedQuery(firstNode->datasetName))
                            for (const auto &c : sq->outputSchema)
                                local_select_headers.push_back(c.name);
                    }
                    else if (m_activeProject && m_activeProject->datasets().contains(firstNode->datasetName))
                    {
                        const auto &ds = m_activeProject->datasets().at(firstNode->datasetName);
                        for (size_t cIdx : firstNode->selectedColumns)
                            if (ds.columns().contains(cIdx))
                                local_select_headers.push_back(ds.columns().at(cIdx).name());
                    }
                    if (local_select_headers.empty())
                        local_select_headers.push_back("*");
                }

                m_queryResultHeaders = local_select_headers;

                // Applique aussi d'éventuelles jointures supplémentaires avec d'autres tables non-union
                for (const auto &link : m_visualQueryLinks)
                {
                    if (link.isSetOperation)
                        continue;

                    const VisualQueryNode *sNode = findNodeById(link.startPinId / 1000);
                    const VisualQueryNode *dNode = findNodeById(link.endPinId / 1000);
                    if (!sNode || !dNode)
                        continue;

                    const VisualQueryNode *extNode = unionedNodeIds.contains(sNode->id) ? dNode : sNode;
                    if (unionedNodeIds.contains(extNode->id))
                        continue;
                    std::shared_ptr<cppsqlx::Dataset> extDs = nullptr;
                    if (extNode->isCTE)
                        extDs = std::make_shared<cppsqlx::Dataset>(extNode->datasetName);
                    else if (m_activeProject->datasets().contains(extNode->datasetName))
                        extDs = std::make_shared<cppsqlx::Dataset>(m_activeProject->datasets().at(extNode->datasetName));
                    if (extDs)
                    {
                        std::string lCol = link.leftFieldName;
                        if (auto dot = lCol.find('.'); dot != std::string::npos)
                            lCol = (unionedNodeIds.contains(sNode->id) ? unionCteName : sNode->alias) + "." + lCol.substr(dot + 1);
                        std::string rCol = link.rightFieldName;
                        if (auto dot = rCol.find('.'); dot != std::string::npos)
                            rCol = (unionedNodeIds.contains(dNode->id) ? unionCteName : dNode->alias) + "." + rCol.substr(dot + 1);
                        cppsqlx::SQLExpression onExpr = operator""_col(lCol.c_str(), lCol.length())
                                                            .equal(operator""_col(rCol.c_str(), rCol.length()));
                        query.join("INNER JOIN", extDs, extNode->alias, onExpr);
                        unionedNodeIds.insert(extNode->id);
                    }
                }
                // Applique les filtres WHERE sur la requête finale (ex: WHERE id != 10)
                std::string combinedWhere;
                for (const auto &pred : m_visualQueryPredicates)
                {
                    std::string singlePred;
                    if (pred.isCustomExpression && !pred.customSqlCondition.empty())
                    {
                        singlePred = pred.customSqlCondition;
                    }
                    else if (!pred.isCustomExpression && pred.connectedSrcPinId != 0 && !pred.value.empty())
                    {
                        uintptr_t srcId = pred.connectedSrcPinId / 1000;
                        if (unionedNodeIds.contains(srcId))
                            continue;
                        std::string colName = pred.targetColumn;
                        if (auto dot = colName.find('.'); dot != std::string::npos)
                            colName = colName.substr(dot + 1);
                        std::string prefix = unionedNodeIds.contains(srcId) ? unionCteName : "";
                        if (prefix.empty())
                        {
                            if (const auto *n = findNodeById(srcId))
                                prefix = n->alias;
                        }
                        std::string fullCol = prefix.empty() ? colName : (prefix + "." + colName);
                        cppsqlx::SQLExpression cExpr = operator""_col(fullCol.c_str(), fullCol.length());
                        cppsqlx::SQLExpression vExpr;
                        try
                        {
                            size_t idx = 0;
                            long long numVal = std::stoll(pred.value, &idx);
                            vExpr = (idx == pred.value.length()) ? cppsqlx::SQLExpression(static_cast<int64_t>(numVal))
                                                                 : cppsqlx::SQLExpression(pred.value);
                        }
                        catch (...)
                        {
                            vExpr = cppsqlx::SQLExpression(pred.value);
                        }
                        if (pred.op == cppsqlx::SQLOperator::Equal)
                            singlePred = cExpr.equal(vExpr).toSQL();
                        else if (pred.op == cppsqlx::SQLOperator::NotEqual)
                            singlePred = cExpr.notEqual(vExpr).toSQL();
                        else if (pred.op == cppsqlx::SQLOperator::Greater)
                            singlePred = cExpr.greater(vExpr).toSQL();
                        else if (pred.op == cppsqlx::SQLOperator::Less)
                            singlePred = cExpr.less(vExpr).toSQL();
                        else if (pred.op == cppsqlx::SQLOperator::GreaterOrEqual)
                            singlePred = cExpr.greaterOrEqual(vExpr).toSQL();
                        else if (pred.op == cppsqlx::SQLOperator::LessOrEqual)
                            singlePred = cExpr.lessOrEqual(vExpr).toSQL();
                        else
                            singlePred = cExpr.equal(vExpr).toSQL();
                    }
                    if (!singlePred.empty())
                        combinedWhere = combinedWhere.empty() ? singlePred : ("(" + combinedWhere + ") AND (" + singlePred + ")");
                }
                if (!combinedWhere.empty())
                    query.where(cppsqlx::SQLExpression(combinedWhere));
                ed::SetCurrentEditor(nullptr);
                return query;
            }
        }
    }

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
        if (link.isSetOperation)
            continue;

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
*/

cppsqlx::SQLExpression Application::buildPredicateComparisonExpr(
    const cppsqlx::SQLExpression &leftExpr,
    cppsqlx::SQLOperator op,
    const cppsqlx::SQLExpression &rightExpr) const
{
    switch (op)
    {
    case cppsqlx::SQLOperator::Equal:
        return leftExpr.equal(rightExpr);
    case cppsqlx::SQLOperator::NotEqual:
        return leftExpr.notEqual(rightExpr);
    case cppsqlx::SQLOperator::Greater:
        return leftExpr.greater(rightExpr);
    case cppsqlx::SQLOperator::Less:
        return leftExpr.less(rightExpr);
    case cppsqlx::SQLOperator::GreaterOrEqual:
        return leftExpr.greaterOrEqual(rightExpr);
    case cppsqlx::SQLOperator::LessOrEqual:
        return leftExpr.lessOrEqual(rightExpr);
    default:
        return leftExpr.equal(rightExpr);
    }
}

cppsqlx::SQLExpression Application::parseLiteralOrStringExpr(const std::string &rawValue) const
{
    try
    {
        size_t idx = 0;
        long long numVal = std::stoll(rawValue, &idx);
        if (idx == rawValue.length())
            return cppsqlx::SQLExpression(static_cast<int64_t>(numVal));
    }
    catch (...)
    {
    }
    return cppsqlx::SQLExpression(rawValue);
}

void Application::registerGraphCtes(cppsqlx::SelectQuery &query) const
{
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
}

void Application::compileGraphSelectClause(cppsqlx::SelectQuery &query, bool &outHasAggregate)
{
    std::vector<std::string> localSelectHeaders;
    cppsqlx::SQLSelectionSet selectionSet;
    bool hasSelection = false;
    outHasAggregate = false;

    if (!m_projectionItems.empty())
    {
        for (const auto &item : m_projectionItems)
        {
            if (item.sqlExpression.empty())
                continue;

            std::string finalSqlExpr = item.sqlExpression;
            if (!item.isCustomExpression && item.sourceNodeId != 0)
            {
                for (const auto &node : m_visualQueryNodes)
                {
                    if (node.id == item.sourceNodeId)
                    {
                        std::string rawCol = finalSqlExpr;
                        if (auto dot = rawCol.find('.'); dot != std::string::npos)
                            rawCol = rawCol.substr(dot + 1);
                        finalSqlExpr = node.alias.empty() ? rawCol : (node.alias + "." + rawCol);
                        break;
                    }
                }
            }

            cppsqlx::SQLExpression expr = operator""_col(finalSqlExpr.c_str(), finalSqlExpr.length());
            if (!item.alias.empty())
                expr = expr.as(item.alias);

            selectionSet.add(expr);
            localSelectHeaders.push_back(item.alias.empty() ? finalSqlExpr : item.alias);
            hasSelection = true;

            if (item.isAggregate)
                outHasAggregate = true;
        }
        query.select(selectionSet);
    }
    else
    {
        for (const auto &node : m_visualQueryNodes)
        {
            if (!node.isCTE && m_activeProject->datasets().contains(node.datasetName))
            {
                const auto &dataset = m_activeProject->datasets().at(node.datasetName);
                for (size_t colIdx : node.selectedColumns)
                {
                    if (dataset.columns().contains(colIdx))
                    {
                        std::string fieldName = dataset.columns().at(colIdx).name();
                        std::string qualifiedCol = node.alias.empty() ? fieldName : (node.alias + "." + fieldName);
                        std::string uniqueAlias = node.alias.empty() ? fieldName : (node.alias + "_" + fieldName);

                        query.select(operator""_col(qualifiedCol.c_str(), qualifiedCol.length()).as(uniqueAlias));
                        localSelectHeaders.push_back(uniqueAlias);
                        hasSelection = true;
                    }
                }
            }
        }
    }

    if (!hasSelection)
    {
        query.select(operator""_col("*", 1));
        localSelectHeaders.push_back("*");
    }

    m_queryResultHeaders = std::move(localSelectHeaders);
}

void Application::compileGraphJoins(cppsqlx::SelectQuery &query) const
{
    auto resolveNodeAsJoinable = [&](const VisualQueryNode &node) -> std::shared_ptr<cppsqlx::Dataset>
    {
        if (node.isCTE)
            return std::make_shared<cppsqlx::Dataset>(node.datasetName);
        if (m_activeProject->datasets().contains(node.datasetName))
            return std::make_shared<cppsqlx::Dataset>(m_activeProject->datasets().at(node.datasetName));
        return nullptr;
    };

    const auto &firstNode = m_visualQueryNodes[0];
    if (auto firstDsPtr = resolveNodeAsJoinable(firstNode))
    {
        std::string firstAlias = firstNode.alias.empty() ? firstNode.datasetName : firstNode.alias;
        query.from(firstDsPtr, firstAlias);
    }

    std::set<uintptr_t> joinedNodeIds{firstNode.id};

    for (const auto &link : m_visualQueryLinks)
    {
        if (link.isSetOperation)
            continue;

        uintptr_t srcNodeId = link.startPinId / 1000;
        uintptr_t dstNodeId = link.endPinId / 1000;

        const VisualQueryNode *srcNode = nullptr;
        const VisualQueryNode *dstNode = nullptr;
        for (const auto &n : m_visualQueryNodes)
        {
            if (n.id == srcNodeId)
                srcNode = &n;
            if (n.id == dstNodeId)
                dstNode = &n;
        }
        if (!srcNode || !dstNode)
            continue;

        const VisualQueryNode *targetNode = joinedNodeIds.contains(dstNodeId) ? srcNode : dstNode;
        if (joinedNodeIds.contains(targetNode->id))
            continue;

        auto targetDsPtr = resolveNodeAsJoinable(*targetNode);
        if (!targetDsPtr)
            continue;

        std::string liveLeftField = link.leftFieldName;
        if (auto dot = liveLeftField.find('.'); dot != std::string::npos)
            liveLeftField = srcNode->alias + "." + liveLeftField.substr(dot + 1);

        std::string liveRightField = link.rightFieldName;
        if (auto dot = liveRightField.find('.'); dot != std::string::npos)
            liveRightField = dstNode->alias + "." + liveRightField.substr(dot + 1);

        cppsqlx::SQLExpression leftExpr = operator""_col(liveLeftField.c_str(), liveLeftField.length());
        cppsqlx::SQLExpression rightExpr = operator""_col(liveRightField.c_str(), liveRightField.length());
        cppsqlx::SQLExpression onExpr = buildPredicateComparisonExpr(leftExpr, link.op, rightExpr);

        std::string joinTypeStr = "INNER JOIN";
        if (link.type == cppsqlx::JoinType::LEFT)
            joinTypeStr = "LEFT JOIN";
        else if (link.type == cppsqlx::JoinType::RIGHT)
            joinTypeStr = "RIGHT JOIN";
        else if (link.type == cppsqlx::JoinType::FULL)
            joinTypeStr = "FULL JOIN";

        std::string targetAlias = targetNode->alias.empty() ? targetNode->datasetName : targetNode->alias;
        query.join(joinTypeStr, targetDsPtr, targetAlias, onExpr);
        joinedNodeIds.insert(targetNode->id);
    }
}

void Application::compileGraphWhereAndGroupBy(
    cppsqlx::SelectQuery &query,
    bool hasAggregate,
    const std::set<uintptr_t> &alreadyEmbeddedNodeIds,
    const std::string &unionCteAlias) const
{
    std::string combinedWhereSql;

    for (const auto &pred : m_visualQueryPredicates)
    {
        std::string singlePredSql;

        if (pred.isCustomExpression && !pred.customSqlCondition.empty())
        {
            singlePredSql = pred.customSqlCondition;
        }
        else if (!pred.isCustomExpression && pred.connectedSrcPinId != 0 && !pred.value.empty())
        {
            uintptr_t tableNodeId = pred.connectedSrcPinId / 1000;
            if (alreadyEmbeddedNodeIds.contains(tableNodeId))
                continue;

            std::string rawCol = pred.targetColumn;
            if (auto dot = rawCol.find('.'); dot != std::string::npos)
                rawCol = rawCol.substr(dot + 1);

            std::string prefix = unionCteAlias;
            if (prefix.empty())
            {
                for (const auto &n : m_visualQueryNodes)
                {
                    if (n.id == tableNodeId)
                    {
                        prefix = n.alias;
                        break;
                    }
                }
            }

            std::string livePredColumn = prefix.empty() ? rawCol : (prefix + "." + rawCol);
            cppsqlx::SQLExpression colExpr = operator""_col(livePredColumn.c_str(), livePredColumn.length());
            cppsqlx::SQLExpression valExpr = parseLiteralOrStringExpr(pred.value);
            singlePredSql = buildPredicateComparisonExpr(colExpr, pred.op, valExpr).toSQL();
        }

        if (!singlePredSql.empty())
        {
            combinedWhereSql = combinedWhereSql.empty()
                                   ? singlePredSql
                                   : ("(" + combinedWhereSql + ") AND (" + singlePredSql + ")");
        }
    }

    if (!combinedWhereSql.empty())
    {
        query.where(cppsqlx::SQLExpression(combinedWhereSql));
    }

    if (hasAggregate)
    {
        for (const auto &item : m_projectionItems)
        {
            if (!item.isAggregate && !item.sqlExpression.empty())
                query.groupBy(cppsqlx::SQLExpression(item.sqlExpression));
        }
    }
}

cppsqlx::SelectQuery Application::compileUnionGraphToSelectQuery()
{
    cppsqlx::SelectQuery query;
    std::vector<std::string> localSelectHeaders;

    auto findNodeById = [&](uintptr_t id) -> const VisualQueryNode *
    {
        for (const auto &n : m_visualQueryNodes)
            if (n.id == id)
                return &n;
        return nullptr;
    };

    auto findSavedQuery = [&](const std::string &name) -> const SavedQuery *
    {
        for (const auto &sq : m_savedQueries)
            if (sq.name == name)
                return &sq;
        return nullptr;
    };

    auto buildWhereForNode = [&](uintptr_t targetNodeId, const std::string &aliasPrefix) -> std::string
    {
        std::string nodeWhere;
        for (const auto &pred : m_visualQueryPredicates)
        {
            if (pred.isCustomExpression || pred.connectedSrcPinId == 0 || pred.value.empty())
                continue;
            if ((pred.connectedSrcPinId / 1000) != targetNodeId)
                continue;

            std::string colName = pred.targetColumn;
            if (auto dot = colName.find('.'); dot != std::string::npos)
                colName = colName.substr(dot + 1);

            std::string fullCol = aliasPrefix.empty() ? colName : (aliasPrefix + "." + colName);
            cppsqlx::SQLExpression cExpr = operator""_col(fullCol.c_str(), fullCol.length());
            cppsqlx::SQLExpression vExpr = parseLiteralOrStringExpr(pred.value);
            std::string singlePred = buildPredicateComparisonExpr(cExpr, pred.op, vExpr).toSQL();

            nodeWhere = nodeWhere.empty() ? singlePred : ("(" + nodeWhere + ") AND (" + singlePred + ")");
        }
        return nodeWhere;
    };

    auto buildBranchSelectQuery = [&](const VisualQueryNode &node, std::string &outBranchName) -> std::optional<cppsqlx::SelectQuery>
    {
        outBranchName = node.alias.empty() ? node.datasetName : node.alias;
        if (auto dot = outBranchName.find('.'); dot != std::string::npos)
            outBranchName = outBranchName.substr(dot + 1);

        if (node.isCTE)
        {
            const SavedQuery *sq = findSavedQuery(node.datasetName);
            if (sq && !sq->compiledSelectJson.is_null() && !sq->compiledSelectJson.empty())
            {
                auto subRes = cppsqlx::SelectQuery::fromJSON(sq->compiledSelectJson);
                if (subRes.has_value())
                {
                    auto subPtr = std::make_shared<cppsqlx::SelectQuery>(std::move(*subRes));
                    query.with(outBranchName, subPtr);

                    cppsqlx::SelectQuery branchSelect;
                    branchSelect.select(operator""_col("*", 1));
                    branchSelect.from(std::make_shared<cppsqlx::Dataset>(outBranchName), outBranchName);

                    std::string branchWhere = buildWhereForNode(node.id, outBranchName);
                    if (!branchWhere.empty())
                        branchSelect.where(cppsqlx::SQLExpression(branchWhere));

                    return branchSelect;
                }
            }
        }
        else if (m_activeProject && m_activeProject->datasets().contains(node.datasetName))
        {
            const auto &ds = m_activeProject->datasets().at(node.datasetName);
            cppsqlx::SelectQuery branchSelect;

            if (!node.selectedColumns.empty())
            {
                for (size_t colIdx : node.selectedColumns)
                {
                    if (ds.columns().contains(colIdx))
                    {
                        std::string cName = ds.columns().at(colIdx).name();
                        branchSelect.select(operator""_col(cName.c_str(), cName.length()));
                    }
                }
            }
            else
            {
                branchSelect.select(operator""_col("*", 1));
            }

            branchSelect.from(std::make_shared<cppsqlx::Dataset>(ds), outBranchName);

            std::string branchWhere = buildWhereForNode(node.id, outBranchName);
            if (!branchWhere.empty())
                branchSelect.where(cppsqlx::SQLExpression(branchWhere));

            return branchSelect;
        }
        return std::nullopt;
    };

    // 1. Collecte de la chaîne ordonnée des nœuds UNION
    std::vector<const VisualQueryNode *> unionChainNodes;
    std::vector<cppsqlx::SetOperator> unionOps;
    std::set<uintptr_t> unionedNodeIds;

    for (const auto &link : m_visualQueryLinks)
    {
        if (!link.isSetOperation)
            continue;

        const VisualQueryNode *src = findNodeById(link.startPinId / 1000);
        const VisualQueryNode *dst = findNodeById(link.endPinId / 1000);
        if (!src || !dst)
            continue;

        if (unionChainNodes.empty())
        {
            unionChainNodes.push_back(src);
            unionedNodeIds.insert(src->id);
        }
        if (!unionedNodeIds.contains(dst->id))
        {
            unionChainNodes.push_back(dst);
            unionOps.push_back(link.setOp);
            unionedNodeIds.insert(dst->id);
        }
        else if (!unionedNodeIds.contains(src->id))
        {
            unionChainNodes.push_back(src);
            unionOps.push_back(link.setOp);
            unionedNodeIds.insert(src->id);
        }
    }

    if (unionChainNodes.size() < 2)
        return query;

    std::string firstName;
    auto unionSubQueryOpt = buildBranchSelectQuery(*unionChainNodes[0], firstName);
    if (!unionSubQueryOpt.has_value())
        return query;

    std::string unionCteName = "req_union_" + firstName;
    for (size_t i = 1; i < unionChainNodes.size(); ++i)
    {
        std::string nextName;
        if (auto nextBranchOpt = buildBranchSelectQuery(*unionChainNodes[i], nextName))
        {
            unionCteName += "_" + nextName;
            cppsqlx::SetOperator op = unionOps[i - 1];
            if (op == cppsqlx::SetOperator::UNION_ALL)
                unionSubQueryOpt->unionAll(*nextBranchOpt);
            else if (op == cppsqlx::SetOperator::UNION)
                unionSubQueryOpt->unionDistinct(*nextBranchOpt);
            else if (op == cppsqlx::SetOperator::INTERSECT)
                unionSubQueryOpt->intersect(*nextBranchOpt);
            else if (op == cppsqlx::SetOperator::EXCEPT)
                unionSubQueryOpt->except(*nextBranchOpt);
        }
    }

    query.with(unionCteName, std::make_shared<cppsqlx::SelectQuery>(std::move(*unionSubQueryOpt)));
    query.from(std::make_shared<cppsqlx::Dataset>(unionCteName), unionCteName);

    // 2. Projection externe sur la CTE d'assemblage
    cppsqlx::SQLSelectionSet outerSelect;
    bool hasOuterCols = false;
    bool hasAggregate = false;

    for (const auto &item : m_projectionItems)
    {
        if (item.sqlExpression.empty())
            continue;

        std::string colOnly = item.sqlExpression;
        if (!item.isCustomExpression)
        {
            if (auto dot = colOnly.find('.'); dot != std::string::npos)
                colOnly = colOnly.substr(dot + 1);
            colOnly = unionCteName + "." + colOnly;
        }

        cppsqlx::SQLExpression expr = operator""_col(colOnly.c_str(), colOnly.length());
        if (!item.alias.empty())
            expr = expr.as(item.alias);

        outerSelect.add(expr);
        localSelectHeaders.push_back(item.alias.empty() ? colOnly : item.alias);
        hasOuterCols = true;
        if (item.isAggregate)
            hasAggregate = true;
    }

    if (hasOuterCols)
    {
        query.select(outerSelect);
    }
    else
    {
        query.select(operator""_col("*", 1));
        const VisualQueryNode *firstNode = unionChainNodes[0];
        if (firstNode->isCTE)
        {
            const SavedQuery *sq = findSavedQuery(firstNode->datasetName);
            if (sq)
            {
                for (const auto &c : sq->outputSchema)
                    localSelectHeaders.push_back(c.name);
            }
        }
        else if (m_activeProject && m_activeProject->datasets().contains(firstNode->datasetName))
        {
            const auto &ds = m_activeProject->datasets().at(firstNode->datasetName);
            for (size_t cIdx : firstNode->selectedColumns)
                if (ds.columns().contains(cIdx))
                    localSelectHeaders.push_back(ds.columns().at(cIdx).name());
        }
        if (localSelectHeaders.empty())
            localSelectHeaders.push_back("*");
    }
    m_queryResultHeaders = std::move(localSelectHeaders);

    // 3. Jointures externes éventuelles avec d'autres tables du canevas
    for (const auto &link : m_visualQueryLinks)
    {
        if (link.isSetOperation)
            continue;

        const VisualQueryNode *sNode = findNodeById(link.startPinId / 1000);
        const VisualQueryNode *dNode = findNodeById(link.endPinId / 1000);
        if (!sNode || !dNode)
            continue;

        const VisualQueryNode *extNode = unionedNodeIds.contains(sNode->id) ? dNode : sNode;
        if (unionedNodeIds.contains(extNode->id))
            continue;

        std::shared_ptr<cppsqlx::Dataset> extDs = nullptr;
        if (extNode->isCTE)
            extDs = std::make_shared<cppsqlx::Dataset>(extNode->datasetName);
        else if (m_activeProject->datasets().contains(extNode->datasetName))
            extDs = std::make_shared<cppsqlx::Dataset>(m_activeProject->datasets().at(extNode->datasetName));

        if (!extDs)
            continue;

        std::string lCol = link.leftFieldName;
        if (auto dot = lCol.find('.'); dot != std::string::npos)
            lCol = (unionedNodeIds.contains(sNode->id) ? unionCteName : sNode->alias) + "." + lCol.substr(dot + 1);

        std::string rCol = link.rightFieldName;
        if (auto dot = rCol.find('.'); dot != std::string::npos)
            rCol = (unionedNodeIds.contains(dNode->id) ? unionCteName : dNode->alias) + "." + rCol.substr(dot + 1);

        cppsqlx::SQLExpression onExpr = buildPredicateComparisonExpr(
            operator""_col(lCol.c_str(), lCol.length()),
            link.op,
            operator""_col(rCol.c_str(), rCol.length()));
        query.join("INNER JOIN", extDs, extNode->alias, onExpr);
        unionedNodeIds.insert(extNode->id);
    }
    // 4. Filtres WHERE globaux et GROUP BY final
    compileGraphWhereAndGroupBy(query, hasAggregate, unionedNodeIds, "");
    return query;
}

cppsqlx::SelectQuery Application::compileVisualGraphToSelectQuery()
{
    cppsqlx::SelectQuery query;
    if (!m_activeProject || m_visualQueryNodes.empty())
    {
        m_queryResultHeaders.clear();
        return query;
    }
    ed::SetCurrentEditor(m_queryContext);
    bool hasUnionLinks = std::any_of(m_visualQueryLinks.begin(), m_visualQueryLinks.end(),
                                     [](const VisualQueryLink &l)
                                     { return l.isSetOperation; });
    if (hasUnionLinks)
    {
        query = compileUnionGraphToSelectQuery();
    }
    else
    {
        bool hasAggregate = false;
        registerGraphCtes(query);
        compileGraphSelectClause(query, hasAggregate);
        compileGraphJoins(query);
        compileGraphWhereAndGroupBy(query, hasAggregate);
    }
    ed::SetCurrentEditor(nullptr);
    return query;
}

void Application::loadQueryIntoVisualDesigner(const SavedQuery &savedQuery)
{
    m_projectionItems.clear();
    m_hasProjectionNode = false;
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

            link.isSetOperation = lJson.value("isSetOperation", false);
            link.setOp = static_cast<cppsqlx::SetOperator>(lJson.value("setOp", static_cast<int>(cppsqlx::SetOperator::UNION_ALL)));

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
                vState.value("projectionY", 120.0f));
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

std::string Application::compileVisualGraphToSQL()
{
    if (m_visualQueryNodes.empty() || !m_activeProject)
    {
        return "";
    }
    return compileVisualGraphToSelectQuery().toSQL();
}
