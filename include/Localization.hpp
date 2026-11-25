#ifndef DIZUI_LOCALIZATION_HPP
#define DIZUI_LOCALIZATION_HPP

#include <string>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>

enum class AppLanguage
{
    FR,
    EN
};

class Localization
{
public:
    static Localization &instance()
    {
        static Localization inst;
        return inst;
    }

    bool loadLanguage(AppLanguage lang, const std::string &localesDir = "assets/locales")
    {
        m_currentLang = lang;
        std::string filename = (lang == AppLanguage::FR) ? "/fr.json" : "/en.json";
        std::ifstream file(localesDir + filename);
        if (!file.is_open())
            return false;

        try
        {
            nlohmann::json j;
            file >> j;
            m_translations.clear();
            for (auto &[key, val] : j.items())
            {
                if (val.is_string())
                    m_translations[key] = val.get<std::string>();
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // Retourne un const char* directement consommable par ImGui::Text, ImGui::Button, etc.
    const char *get(const std::string &key) const
    {
        auto it = m_translations.find(key);
        if (it != m_translations.end())
            return it->second.c_str();
        return key.c_str(); // Repli sécurisé sur la clé brute si absente
    }

    AppLanguage currentLanguage() const { return m_currentLang; }

private:
    Localization() = default;
    AppLanguage m_currentLang = AppLanguage::FR;
    std::unordered_map<std::string, std::string> m_translations;
};

// Raccourci global pratique pour les appels ImGui
inline const char *tr(const std::string &key)
{
    return Localization::instance().get(key);
}

#endif // DIZUI_LOCALIZATION_HPP
