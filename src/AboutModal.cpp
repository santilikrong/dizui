#include "AboutModal.hpp"
#include "Localization.hpp"
#include "IconsLucide.hpp"
#include "imgui.h"
#include <string>

#ifndef DIZUI_VERSION
#define DIZUI_VERSION "1.0.0"
#endif

namespace dizui::ui
{
    void renderAboutModal(bool &showAboutModal)
    {
        std::string aboutModalTitle = std::string(tr("about.modal_title")) + "###AboutDizuiModal";
        if (showAboutModal)
            ImGui::OpenPopup(aboutModalTitle.c_str());

        ImGui::SetNextWindowSize(ImVec2(480.0f, 250.0f), ImGuiCond_Once);
        if (ImGui::BeginPopupModal(aboutModalTitle.c_str(), &showAboutModal, ImGuiWindowFlags_NoResize))
        {
            // ... dans renderAboutModal() ...
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "%s  dizui Studio", ICON_LC_BLOCKS);
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", tr("about.version_label"), DIZUI_VERSION);

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped("%s", tr("about.description"));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextDisabled("%s :", tr("about.tech_stack_label"));
            ImGui::BulletText("C++23 / PostgreSQL (libpq & cppsqlx)");
            ImGui::BulletText("Dear ImGui / imgui-node-editor / GLFW");
            ImGui::BulletText("Ollama Local AI Integration");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float availX = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availX - 120.0f);
            if (ImGui::Button(tr("btn.close"), ImVec2(120.0f, 0.0f)))
            {
                showAboutModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
