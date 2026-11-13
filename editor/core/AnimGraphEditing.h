#pragma once

// Editing .animgraph files (docs/design/0028): the file's JSON with operations that keep
// it consistent. Fields the editor does not know about are kept. Pure, tested without a
// window.

#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Geni::Editor
{

struct AnimTransitionView
{
    std::string from; // "*": any state
    std::string to;
    float duration = 0.2f;
    float exitTime = -1.0f;
    // "grounded == true; jump"
    std::string conditions;
};

class AnimGraphDocument
{
  public:
    bool Load(const nlohmann::ordered_json &document, std::string *error = nullptr);
    const nlohmann::ordered_json &GetJson() const
    {
        return m_document;
    }
    // The runtime parser's verdict on the current document (empty: valid).
    std::string Validate() const;

    std::vector<std::string> GetStates() const;
    std::string GetInitial() const;
    bool SetInitial(const std::string &state);
    std::vector<std::string> GetParameters() const;

    bool AddState(const std::string &name, const std::string &clip);
    bool RenameState(const std::string &from, const std::string &to);
    bool RemoveState(const std::string &name);
    // The state's JSON ({"clip"} or {"blend"}, "loop", "speed").
    const nlohmann::ordered_json *GetState(const std::string &name) const;
    bool SetStateClip(const std::string &name, const std::string &clip);
    bool SetStateLoop(const std::string &name, bool loop);
    bool SetStateSpeed(const std::string &name, float speed);

    std::vector<AnimTransitionView> GetTransitions() const;
    int AddTransition(const std::string &from, const std::string &to);
    bool RemoveTransition(int index);
    bool SetTransition(int index, const AnimTransitionView &view, std::string *error = nullptr);

    // Node positions (the "editor" block, ignored by the runtime). States without one are
    // laid out in a row.
    glm::vec2 GetPosition(const std::string &state) const;
    void SetPosition(const std::string &state, const glm::vec2 &position);

    // "a == 1; jump" <-> [["a", "==", 1], ["jump"]].
    static bool ParseConditions(const std::string &text, nlohmann::ordered_json &out, std::string *error = nullptr);
    static std::string FormatConditions(const nlohmann::ordered_json &when);

  private:
    nlohmann::ordered_json &States();
    nlohmann::ordered_json &Transitions();
    nlohmann::ordered_json m_document;
};

} // namespace Geni::Editor
