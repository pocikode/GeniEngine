#include "scene/Reflection.h"
#include "core/Log.h"

namespace Geni
{

TypeRegistry &TypeRegistry::Get()
{
    static TypeRegistry registry;
    return registry;
}

void TypeRegistry::Register(ComponentTypeDesc desc)
{
    if (desc.name.empty() || !desc.create)
    {
        GENI_LOG_ERROR("reflection: ignoring a component type without a name or factory");
        return;
    }
    auto [it, inserted] = m_types.emplace(desc.name, std::move(desc));
    if (!inserted)
    {
        GENI_LOG_WARN("reflection: component type '%s' registered twice; keeping the first", it->first.c_str());
    }
}

const ComponentTypeDesc *TypeRegistry::Find(const std::string &name) const
{
    auto it = m_types.find(name);
    return it == m_types.end() ? nullptr : &it->second;
}

Component *TypeRegistry::Create(const std::string &name) const
{
    const ComponentTypeDesc *type = Find(name);
    return type ? type->create() : nullptr;
}

nlohmann::json TypeRegistry::Serialize(const std::string &typeName, const Component &component) const
{
    nlohmann::json properties = nlohmann::json::object();
    const ComponentTypeDesc *type = Find(typeName);
    if (!type)
    {
        GENI_LOG_WARN("reflection: no descriptor for component type '%s'", typeName.c_str());
        return properties;
    }
    for (const auto &property : type->properties)
    {
        if (property.get)
        {
            properties[property.name] = property.get(component);
        }
    }
    return properties;
}

bool TypeRegistry::Deserialize(
    const std::string &typeName, Component &component, const nlohmann::json &properties, SerializationContext &context
) const
{
    const ComponentTypeDesc *type = Find(typeName);
    if (!type)
    {
        GENI_LOG_WARN("reflection: no descriptor for component type '%s'", typeName.c_str());
        return false;
    }
    for (const auto &property : type->properties)
    {
        auto it = properties.find(property.name);
        if (it == properties.end() || !property.set)
        {
            continue;
        }
        if (!property.set(component, *it, context))
        {
            GENI_LOG_WARN("reflection: could not read property '%s'", property.name.c_str());
        }
    }
    return true;
}

std::vector<std::string> TypeRegistry::GetComponentTypes() const
{
    std::vector<std::string> names;
    names.reserve(m_types.size());
    for (const auto &[name, type] : m_types)
    {
        (void)type;
        names.push_back(name);
    }
    return names;
}

} // namespace Geni
