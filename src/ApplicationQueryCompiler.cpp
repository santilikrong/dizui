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

            bool isWindow = item.isWindowFunction ||
                            (finalSqlExpr.find("OVER") != std::string::npos) ||
                            (finalSqlExpr.find("over") != std::string::npos);

            if (item.isAggregate && !isWindow)
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
            // Vérifie à la fois l'indicateur explicite et la présence éventuelle du mot-clé OVER dans l'expression
            bool isWindow = item.isWindowFunction ||
                            (item.sqlExpression.find("OVER") != std::string::npos) ||
                            (item.sqlExpression.find("over") != std::string::npos);

            if (!item.isAggregate && !isWindow && !item.sqlExpression.empty())
            {
                query.groupBy(cppsqlx::SQLExpression(item.sqlExpression));
            }
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
            item.sourceNodeId = projJson.value("sourceNodeId", uintptr_t(0));

            bool detectedOver = (item.sqlExpression.find("OVER") != std::string::npos) ||
                                (item.sqlExpression.find("over") != std::string::npos);
            item.isWindowFunction = projJson.value("isWindowFunction", detectedOver);

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
