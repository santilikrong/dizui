#include "Application.hpp"
#include "Localization.hpp"
#include <GLFW/glfw3.h>
#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace ed = ax::NodeEditor;

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
            schemaArr.push_back({{"name", col.name},
                                 {"dataType", col.dataType},
                                 {"isNullable", col.isNullable},
                                 {"isPrimaryKey", col.isPrimaryKey}});
        }

        uiState["queries"].push_back({{"name", q.name},
                                      {"description", q.description},
                                      {"sql", q.sql},
                                      {"visual_state", q.visualState},
                                      {"select_json", q.compiledSelectJson},
                                      {"output_schema", std::move(schemaArr)}});
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
            std::string errMsg = std::string(tr("project.load_open_error")) + " : " + filePath;
            std::cerr << "[dizui] " << errMsg << std::endl;
            addConsoleLog(errMsg, LogLevel::Error);
            return false;
        }

        nlohmann::json rootJson;
        inFile >> rootJson;
        inFile.close();

        // 1. Reconstruction officielle de DataIntegrationProject (connexions, datasets, tâches, workflows)
        auto projRes = cppsqlx::DataIntegrationProject::fromJSON(rootJson);
        if (!projRes.has_value())
        {
            std::string errMsg = std::string(tr("project.load_invalid_json")) + " : " + projRes.error();
            std::cerr << "[dizui] " << errMsg << std::endl;
            addConsoleLog(errMsg, LogLevel::Error);
            return false;
        }

        // Instanciation unique
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

        // 2. Nettoyage des canevas actifs et réinitialisation des sélecteurs de connexion
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

        m_selectedGateway.clear();
        m_selectedContext.clear();

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

                    vs["nodes"].push_back({{"id", nid},
                                           {"taskName", jobPtr->name()},
                                           {"x", static_cast<float>(jobPtr->x())},
                                           {"y", static_cast<float>(jobPtr->y())},
                                           {"inputPinId", nid * 10},
                                           {"outputPinId", (nid * 10) + 1}});
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

        std::string okMsg = std::string(tr("project.load_success")) + " : " + m_currentProjectName;
        std::cout << "[dizui Info] " << okMsg << std::endl;
        addConsoleLog(okMsg, LogLevel::Success);
        return true;
    }
    catch (const std::exception &e)
    {
        std::string errMsg = std::string(tr("project.load_exception")) + " : " + e.what();
        std::cerr << "[dizui] " << errMsg << std::endl;
        addConsoleLog(errMsg, LogLevel::Error);
        return false;
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
