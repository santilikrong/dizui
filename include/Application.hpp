#pragma once

#include "WindowManager.hpp"
#include "RenderEngine.hpp"
#include <imgui_node_editor.h>
#include <cppsqlx/dataintegrationproject.hpp>
#include <cppsqlx/datasetfactory.hpp>
#include <cppsqlx/databasemetadata.hpp>
#include <cppsqlx/selectquery.hpp>
#include <cppsqlx/task.hpp>

#include <memory>
#include <set>
#include <vector>
#include <map>
#include <string>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

namespace ed = ax::NodeEditor;

class Application
{
public:
    Application();
    ~Application();

    void run();

private:
    // =========================================================================
    // 1. STRUCTURES DE DONNÉES ET ÉNUMÉRATIONS INTERNES
    // =========================================================================
    enum class ActiveTab
    {
        Project,
        Search,
        Git,
        Package,
        Settings
    };

    enum class VisualJobStatus
    {
        Idle,
        Running,
        Success,
        Failed
    };

    struct VisualQueryNode
    {
        uintptr_t id;
        std::string datasetName; // Nom du Dataset physique OU nom de la SavedQuery (CTE)
        ImVec2 position;
        bool needsPositionInitialization = true;
        std::string alias;
        std::set<size_t> selectedColumns;
        bool isCTE = false; // Vrai si ce nœud provient d'une SavedQuery utilisée en CTE
    };

    struct VisualQueryLink
    {
        uintptr_t id;
        uintptr_t startPinId;
        uintptr_t endPinId;

        uintptr_t centralNodeId;
        uintptr_t leftPinId;
        uintptr_t rightPinId;

        cppsqlx::JoinType type = cppsqlx::JoinType::INNER;
        std::string leftFieldName;
        std::string rightFieldName;
        cppsqlx::SQLOperator op = cppsqlx::SQLOperator::Equal;

        ImVec2 joinPosition = ImVec2(0.0f, 0.0f);
        bool needsJoinPosInit = false;

        std::string getExpressionString() const
        {
            return leftFieldName + " " + std::string(cppsqlx::toSQL(op)) + " " + rightFieldName;
        }
    };

    struct VisualQueryPredicate
    {
        uintptr_t id;
        ImVec2 position;
        bool needsPositionInitialization = true;

        uintptr_t inputPinId;
        uintptr_t outputPinId;

        std::string targetColumn;
        cppsqlx::SQLOperator op = cppsqlx::SQLOperator::Equal;
        std::string value;
        uintptr_t connectedSrcPinId = 0;

        // Nouveaux champs pour le mode Expression SQL avancée
        bool isCustomExpression = false;
        std::string customSqlCondition = ""; // Ex: "UPPER(emp.nom) LIKE 'M%' AND emp.actif = true"
    };


    struct VisualProjectionItem
    {
        uintptr_t id;
        std::string alias;
        std::string sqlExpression;
        bool isAggregate = false;
        bool isCustomExpression = false;
        uintptr_t sourceNodeId = 0; // Identifiant du nœud source (Table ou CTE)
    };


    struct SavedQuery
    {
        std::string name;
        std::string description;
        std::string sql;
        nlohmann::json visualState;
        nlohmann::json compiledSelectJson;
        std::vector<cppsqlx::ColumnMetadata> outputSchema; // Colonnes déduites via LIMIT 0
    };


    struct WorkflowJobNode
    {
        uintptr_t id;
        std::string taskName;
        ImVec2 position;
        bool needsPositionInitialization = true;

        uintptr_t inputPinId;
        uintptr_t outputPinId;

        VisualJobStatus executionStatus = VisualJobStatus::Idle;
        std::string statusMessage = "";
    };

    struct WorkflowJobLink
    {
        uintptr_t id;
        uintptr_t startPinId;
        uintptr_t endPinId;
    };

    struct SavedWorkflow
    {
        std::string name;
        std::string description;
        std::string jsonPreview;
        nlohmann::json visualState;
    };

    enum class AppTheme
    {
        DarkModern,
        Light,
        Nord,
        Dracula
    };


    // =========================================================================
    // 2. MÉTHODES DE RENDU DE L'INTERFACE (UI)
    // =========================================================================
    // Architecture principale
    void renderUI();
    void renderLeftSidebar();
    void renderActivityBar();
    void renderProjectExplorerTree();
    void renderMainContentZone();
    void renderStatusBar();

    // Éditeur visuel de requêtes (Query Designer)
    void renderQueryEditor();
    void renderQueryCanvas(float canvas_height);
    void renderQueryResultTable(float bottom_zone_height);
    void renderQueryModals();
    void handleCanvasInteraction();
    void renderCanvasDatasetNodes();
    void renderCanvasFilterNodes();
    void renderCanvasJoinNodes();
    void handleCanvasActions();

    // Éditeur visuel de workflows (Workflow Designer)
    void renderWorkflowEditor();
    void renderWorkflowToolbar();
    void renderWorkflowCanvas();
    void renderWorkflowModals();

    // Modales globales
    void renderGlobalModals();
    void renderReverseEngineeringModal();
    void renderCreateTaskModal();
    void renderSaveProjectAsModal();
    void renderOpenProjectModal();
    void renderRenameModals();

    void renderCanvasProjectionNode();
    void renderExpressionEditorModal();

    void renderConnectionModals();
        
    void applyTheme(AppTheme theme);


    // =========================================================================
    // 3. MÉTHODES MÉTIER, COMPILATION ET PERSISTANCE
    // =========================================================================
    // Compilation et exécution SQL
    cppsqlx::SelectQuery compileVisualGraphToSelectQuery();
    std::string compileVisualGraphToSQL();
    void executeVisualQuery(const std::string &sql);

    // Compilation et exécution asynchrone des Workflows
    std::string compileWorkflowToJSON(const std::string &workflowName = "Preview_Workflow");
    void launchWorkflowExecutionAsync(const std::string &logicalConnName, const std::string &envContext);

    // Gestion de projet et restauration des graphes
    nlohmann::json serializeFullProjectToJSON();
    bool saveProjectToFile(const std::string &filePath);
    bool loadProjectFromFile(const std::string &filePath);
    void loadQueryIntoVisualDesigner(const SavedQuery &savedQuery);
    void loadWorkflowIntoVisualDesigner(const SavedWorkflow &savedWorkflow);
    void createNewProject(const std::string &projectName = "Nouveau_Projet");


    // =========================================================================
    // 4. ATTRIBUTS MEMBRES (ÉTAT DE L'APPLICATION)
    // =========================================================================
    // Fenêtrage et contextes graphiques
    std::unique_ptr<WindowManager> m_windowManager;
    std::unique_ptr<RenderEngine> m_renderEngine;
    ed::EditorContext *m_workflowContext = nullptr;
    ed::EditorContext *m_queryContext = nullptr;

    ActiveTab m_currentTab = ActiveTab::Project;
    float m_queryEditorHeight = 300.0f;
    float m_queryVisualHeight = 350.0f;
    bool m_needsQueryCentering = false;
    bool m_needsWorkflowCentering = false;

    // Projet actif et connexions
    std::unique_ptr<cppsqlx::DataIntegrationProject> m_activeProject;
    std::string m_currentProjectName = "Nouveau_Projet";
    std::string m_currentProjectFilePath = "";
    std::string m_selectedGateway = "";
    std::string m_selectedContext = "";

    // Catalogue enregistré dans le projet
    std::vector<SavedQuery> m_savedQueries;
    std::vector<cppsqlx::Task> m_savedTasks;
    std::vector<SavedWorkflow> m_savedWorkflows;

    // État du canevas Query Designer
    std::vector<VisualQueryNode> m_visualQueryNodes;
    std::vector<VisualQueryLink> m_visualQueryLinks;
    std::vector<VisualQueryPredicate> m_visualQueryPredicates;
    std::vector<std::string> m_queryResultHeaders;
    std::vector<std::vector<std::string>> m_queryResultRows;
    std::string m_executionError = "";
    std::string m_compiledSQLPreview = "";

    // État du canevas Workflow Designer et exécution parallèle
    std::vector<WorkflowJobNode> m_workflowNodes;
    std::vector<WorkflowJobLink> m_workflowLinks;
    char m_activeWorkflowNameBuf[128] = "Mon_Workflow";
    char m_activeWorkflowDescBuf[512] = "";
    std::string m_compiledWorkflowJson = "";

    std::atomic<bool> m_isWorkflowRunning{false};
    std::mutex m_workflowStatusMutex;
    std::map<std::string, VisualJobStatus> m_liveJobStatuses;
    std::map<std::string, std::string> m_liveJobMessages;

    // États des modales et formulaires UI
    bool m_showReverseModal = false;
    std::string m_reverseSelectedSchema = "public";
    std::string m_reverseSelectedObject = "";
    std::vector<std::string> m_cachedSchemas;
    std::vector<std::string> m_cachedTables;
    std::vector<std::string> m_cachedViews;
    std::vector<cppsqlx::ColumnMetadata> m_cachedStructure;

    bool m_showJoinModal = false;
    VisualQueryLink *m_editingJoinLink = nullptr;
    bool m_showPredicateModal = false;
    VisualQueryPredicate *m_editingPredicate = nullptr;

    char m_whereExprFormulaBuf[1024] = "";


    bool m_showSQLPreviewModal = false;
    bool m_showSaveQueryModal = false;
    char m_saveQueryNameBuf[64] = "";
    char m_saveQueryDescBuf[256] = "";

    bool m_showCreateTaskModal = false;
    char m_newTaskNameBuf[128] = "";
    char m_newTaskStepNameBuf[128] = "";
    int m_selectedQueryIndexForTask = -1;
    int m_selectedTargetTypeIndex = 0;
    int m_tableOperationMode = 0;
    int m_currentStepIndex = 1;
    cppsqlx::Task m_workingTaskContext;

    bool m_showWorkflowJsonModal = false;
    bool m_showSaveWorkflowModal = false;
    char m_saveWorkflowNameBuf[128] = "";
    char m_saveWorkflowDescBuf[256] = "";

    bool m_showRunWorkflowModal = false;
    int m_selectedRunGatewayIndex = 0;
    int m_selectedRunContextIndex = 0;

    bool m_showSaveProjectAsModal = false;
    bool m_showOpenProjectModal = false;
    std::filesystem::path m_browserCurrentDir = std::filesystem::current_path();
    char m_projectFileNameBuf[256] = "mon_projet.json";
    char m_newFolderNameBuf[256] = "";
    bool m_showNewFolderInput = false;

    bool m_showRenameQueryModal = false;
    int m_renamingQueryIndex = -1;
    char m_renameQueryBuf[128] = "";

    bool m_showRenameWorkflowModal = false;
    int m_renamingWorkflowIndex = -1;
    char m_renameWorkflowBuf[128] = "";


    // Projection unifiée (SelectionSet) et Nœud de Transformation
    std::vector<VisualProjectionItem> m_projectionItems;
    bool m_hasProjectionNode = false;
    ImVec2 m_projectionNodePos = ImVec2(650.0f, 120.0f);
    bool m_needsProjectionPosInit = false;

    // États de l'éditeur avancé d'expression SQL
    bool m_showExpressionModal = false;
    int m_editingProjectionIndex = -1; // -1 = nouvelle expression, >= 0 = édition d'un élément existant
    char m_exprAliasBuf[128] = "";
    char m_exprFormulaBuf[1024] = "";
    bool m_exprIsAggregate = false;

    // Modale : Créer / Éditer une Connexion Physique
    bool m_showPhysConnModal = false;
    bool m_isEditingPhysConn = false;
    std::string m_originalPhysConnName = "";
    char m_physConnNameBuf[128] = "";
    char m_physConnHostBuf[128] = "localhost";
    int m_physConnPort = 5432;
    char m_physConnDbNameBuf[128] = "";
    char m_physConnUserBuf[128] = "";
    char m_physConnPassBuf[128] = "";
    std::string m_physConnTestStatus = "";
    bool m_physConnTestOk = false;

    // Modale : Renommer une Connexion Physique
    bool m_showRenamePhysConnModal = false;
    std::string m_renamingPhysConnOldName = "";
    char m_renamePhysConnBuf[128] = "";

    // Modale : Créer / Éditer une Passerelle Logique (Gateway)
    bool m_showLogConnModal = false;
    bool m_isEditingLogConn = false;
    std::string m_originalLogConnName = "";
    char m_logConnNameBuf[128] = "";
    std::string m_logConnDevTarget = "";
    std::string m_logConnTestTarget = "";
    std::string m_logConnProdTarget = "";

    // Modale : Renommer une Passerelle Logique
    bool m_showRenameLogConnModal = false;
    std::string m_renamingLogConnOldName = "";
    char m_renameLogConnBuf[128] = "";

    int m_physConnSslModeIndex = 2; // 0: disable, 1: allow, 2: prefer (défaut), 3: require, 4: verify-ca, 5: verify-full


    
    AppTheme m_currentTheme = AppTheme::DarkModern;


};
