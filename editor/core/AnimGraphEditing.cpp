#include "AnimGraphEditing.h"

#include "animation/AnimationGraph.h"

#include <cmath>
#include <sstream>

namespace Geni::Editor
{

namespace
{

std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t");
    const size_t last = text.find_last_not_of(" \t");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

} // namespace

bool AnimGraphDocument::Load(const nlohmann::ordered_json &document, std::string *error)
{
    if (!document.is_object() || !document.contains("states") || !document["states"].is_object())
    {
        if (error)
        {
            *error = "not an animation graph (no \"states\" object)";
        }
        return false;
    }
    m_document = document;
    return true;
}

std::string AnimGraphDocument::Validate() const
{
    std::string error;
    const auto plain = nlohmann::json::parse(m_document.dump());
    return AnimationGraph::Parse(plain, &error) ? std::string() : (error.empty() ? "invalid graph" : error);
}

nlohmann::ordered_json &AnimGraphDocument::States()
{
    return m_document["states"];
}

nlohmann::ordered_json &AnimGraphDocument::Transitions()
{
    if (!m_document.contains("transitions") || !m_document["transitions"].is_array())
    {
        m_document["transitions"] = nlohmann::ordered_json::array();
    }
    return m_document["transitions"];
}

std::vector<std::string> AnimGraphDocument::GetStates() const
{
    std::vector<std::string> names;
    for (auto it = m_document["states"].begin(); it != m_document["states"].end(); ++it)
    {
        names.push_back(it.key());
    }
    return names;
}

std::string AnimGraphDocument::GetInitial() const
{
    const auto states = GetStates();
    return m_document.value("initial", states.empty() ? std::string() : states.front());
}

bool AnimGraphDocument::SetInitial(const std::string &state)
{
    if (!m_document["states"].contains(state))
    {
        return false;
    }
    m_document["initial"] = state;
    return true;
}

std::vector<std::string> AnimGraphDocument::GetParameters() const
{
    std::vector<std::string> names;
    if (m_document.contains("parameters") && m_document["parameters"].is_object())
    {
        for (auto it = m_document["parameters"].begin(); it != m_document["parameters"].end(); ++it)
        {
            names.push_back(it.key());
        }
    }
    return names;
}

bool AnimGraphDocument::AddState(const std::string &name, const std::string &clip)
{
    if (name.empty() || name == "*" || States().contains(name))
    {
        return false;
    }
    States()[name] = {{"clip", clip}};
    return true;
}

bool AnimGraphDocument::RenameState(const std::string &from, const std::string &to)
{
    if (to.empty() || to == "*" || !States().contains(from) || States().contains(to))
    {
        return false;
    }
    // Rebuild the object so the state keeps its place in the file.
    nlohmann::ordered_json states = nlohmann::ordered_json::object();
    for (auto it = States().begin(); it != States().end(); ++it)
    {
        states[it.key() == from ? to : it.key()] = it.value();
    }
    States() = std::move(states);
    for (auto &transition : Transitions())
    {
        for (const char *end : {"from", "to"})
        {
            if (transition.value(end, std::string()) == from)
            {
                transition[end] = to;
            }
        }
    }
    if (m_document.value("initial", std::string()) == from)
    {
        m_document["initial"] = to;
    }
    if (m_document.contains("editor") && m_document["editor"].contains("positions") &&
        m_document["editor"]["positions"].contains(from))
    {
        m_document["editor"]["positions"][to] = m_document["editor"]["positions"][from];
        m_document["editor"]["positions"].erase(from);
    }
    return true;
}

bool AnimGraphDocument::RemoveState(const std::string &name)
{
    if (!States().contains(name) || States().size() < 2)
    {
        return false; // a graph needs a state
    }
    States().erase(name);
    auto &transitions = Transitions();
    for (size_t i = transitions.size(); i-- > 0;)
    {
        if (transitions[i].value("from", std::string()) == name || transitions[i].value("to", std::string()) == name)
        {
            transitions.erase(i);
        }
    }
    if (m_document.value("initial", std::string()) == name)
    {
        m_document["initial"] = GetStates().front();
    }
    if (m_document.contains("editor") && m_document["editor"].contains("positions"))
    {
        m_document["editor"]["positions"].erase(name);
    }
    return true;
}

const nlohmann::ordered_json *AnimGraphDocument::GetState(const std::string &name) const
{
    const auto &states = m_document["states"];
    const auto it = states.find(name);
    return it == states.end() ? nullptr : &*it;
}

bool AnimGraphDocument::SetStateClip(const std::string &name, const std::string &clip)
{
    if (!States().contains(name) || clip.empty())
    {
        return false;
    }
    auto &state = States()[name];
    state.erase("blend");
    state["clip"] = clip;
    return true;
}

bool AnimGraphDocument::SetStateLoop(const std::string &name, bool loop)
{
    if (!States().contains(name))
    {
        return false;
    }
    States()[name]["loop"] = loop;
    return true;
}

bool AnimGraphDocument::SetStateSpeed(const std::string &name, float speed)
{
    if (!States().contains(name) || !std::isfinite(speed) || speed < 0.0f)
    {
        return false;
    }
    States()[name]["speed"] = speed;
    return true;
}

std::vector<AnimTransitionView> AnimGraphDocument::GetTransitions() const
{
    std::vector<AnimTransitionView> views;
    if (!m_document.contains("transitions"))
    {
        return views;
    }
    for (const auto &transition : m_document["transitions"])
    {
        AnimTransitionView view;
        view.from = transition.value("from", std::string("*"));
        view.to = transition.value("to", std::string());
        view.duration = transition.value("duration", 0.2f);
        view.exitTime = transition.value("exitTime", -1.0f);
        view.conditions = FormatConditions(transition.value("when", nlohmann::ordered_json::array()));
        views.push_back(view);
    }
    return views;
}

int AnimGraphDocument::AddTransition(const std::string &from, const std::string &to)
{
    if ((from != "*" && !States().contains(from)) || !States().contains(to) || from == to)
    {
        return -1;
    }
    Transitions().push_back({{"from", from}, {"to", to}, {"when", nlohmann::ordered_json::array()}, {"duration", 0.2}});
    return static_cast<int>(Transitions().size() - 1);
}

bool AnimGraphDocument::RemoveTransition(int index)
{
    auto &transitions = Transitions();
    if (index < 0 || index >= static_cast<int>(transitions.size()))
    {
        return false;
    }
    transitions.erase(static_cast<size_t>(index));
    return true;
}

bool AnimGraphDocument::SetTransition(int index, const AnimTransitionView &view, std::string *error)
{
    auto &transitions = Transitions();
    if (index < 0 || index >= static_cast<int>(transitions.size()))
    {
        return false;
    }
    nlohmann::ordered_json when;
    if (!ParseConditions(view.conditions, when, error))
    {
        return false;
    }
    auto &transition = transitions[static_cast<size_t>(index)];
    transition["from"] = view.from;
    transition["to"] = view.to;
    transition["when"] = when;
    transition["duration"] = std::max(0.0f, view.duration);
    if (view.exitTime >= 0.0f)
    {
        transition["exitTime"] = view.exitTime;
    }
    else
    {
        transition.erase("exitTime");
    }
    return true;
}

glm::vec2 AnimGraphDocument::GetPosition(const std::string &state) const
{
    if (m_document.contains("editor") && m_document["editor"].contains("positions") &&
        m_document["editor"]["positions"].contains(state))
    {
        const auto &p = m_document["editor"]["positions"][state];
        return glm::vec2(p.at(0).get<float>(), p.at(1).get<float>());
    }
    const auto states = GetStates();
    for (size_t i = 0; i < states.size(); ++i)
    {
        if (states[i] == state)
        {
            return glm::vec2(60.0f + 220.0f * static_cast<float>(i), 120.0f + (i % 2 ? 90.0f : 0.0f));
        }
    }
    return glm::vec2(60.0f);
}

void AnimGraphDocument::SetPosition(const std::string &state, const glm::vec2 &position)
{
    m_document["editor"]["positions"][state] = {std::round(position.x), std::round(position.y)};
}

bool AnimGraphDocument::ParseConditions(const std::string &text, nlohmann::ordered_json &out, std::string *error)
{
    out = nlohmann::ordered_json::array();
    std::stringstream parts(text);
    std::string part;
    while (std::getline(parts, part, ';'))
    {
        part = Trim(part);
        if (part.empty())
        {
            continue;
        }
        std::istringstream words(part);
        std::string name;
        std::string op;
        std::string value;
        words >> name >> op >> value;
        if (op.empty())
        {
            out.push_back({name});
            continue;
        }
        static const char *kOps[] = {"==", "!=", "<", "<=", ">", ">="};
        bool known = false;
        for (const char *candidate : kOps)
        {
            known = known || op == candidate;
        }
        if (!known || value.empty())
        {
            if (error)
            {
                *error = "condition '" + part + "': use 'name', or 'name OP value' with ==, !=, <, <=, >, >=";
            }
            return false;
        }
        if (value == "true" || value == "false")
        {
            out.push_back({name, op, value == "true"});
            continue;
        }
        try
        {
            size_t used = 0;
            const double number = std::stod(value, &used);
            if (used != value.size())
            {
                throw std::invalid_argument(value);
            }
            out.push_back({name, op, number});
        }
        catch (const std::exception &)
        {
            if (error)
            {
                *error = "condition '" + part + "': the value must be a number, true or false";
            }
            return false;
        }
    }
    return true;
}

std::string AnimGraphDocument::FormatConditions(const nlohmann::ordered_json &when)
{
    std::string text;
    for (const auto &condition : when)
    {
        if (!condition.is_array() || condition.empty())
        {
            continue;
        }
        if (!text.empty())
        {
            text += "; ";
        }
        text += condition[0].get<std::string>();
        if (condition.size() >= 3)
        {
            const auto &value = condition[2];
            std::string shown;
            if (value.is_boolean())
            {
                shown = value.get<bool>() ? "true" : "false";
            }
            else
            {
                std::ostringstream number;
                number << value.get<double>();
                shown = number.str();
            }
            text += " " + condition[1].get<std::string>() + " " + shown;
        }
    }
    return text;
}

} // namespace Geni::Editor
