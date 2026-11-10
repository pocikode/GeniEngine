#pragma once

#include "assets/AssetID.h"
#include "scene/Component.h"

#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Geni
{

class AssetManager;
class Scene;

// What a property setter may need beyond the JSON value: the scene being built (for
// skeleton joints) and the asset manager (to resolve asset IDs into handles).
struct SerializationContext
{
    Scene *scene = nullptr;
    AssetManager *assets = nullptr;
};

enum class PropertyType
{
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    String,
    Enum,
    Asset
};

struct PropertyDesc
{
    std::string name;
    PropertyType type = PropertyType::String;
    std::function<nlohmann::json(const Component &)> get;
    std::function<bool(Component &, const nlohmann::json &, SerializationContext &)> set;
};

struct ComponentTypeDesc
{
    std::string name;
    std::function<Component *()> create;
    std::vector<PropertyDesc> properties;
};

// Process-wide map from a component type name to a factory and its properties. Built-in
// components register themselves in their translation unit; games can do the same.
class TypeRegistry
{
  public:
    static TypeRegistry &Get();

    void Register(ComponentTypeDesc desc);
    const ComponentTypeDesc *Find(const std::string &name) const;
    Component *Create(const std::string &name) const;

    nlohmann::json Serialize(const std::string &typeName, const Component &component) const;
    bool Deserialize(
        const std::string &typeName, Component &component, const nlohmann::json &properties,
        SerializationContext &context
    ) const;

    std::vector<std::string> GetComponentTypes() const;

  private:
    std::map<std::string, ComponentTypeDesc> m_types;
};

template <typename T> bool RegisterComponentType(std::string name, std::vector<PropertyDesc> properties)
{
    ComponentTypeDesc desc;
    desc.name = std::move(name);
    desc.create = [] { return new T(); };
    desc.properties = std::move(properties);
    TypeRegistry::Get().Register(std::move(desc));
    return true;
}

namespace Detail
{

inline nlohmann::json Vec2Json(const glm::vec2 &value)
{
    return nlohmann::json::array({value.x, value.y});
}
inline nlohmann::json Vec3Json(const glm::vec3 &value)
{
    return nlohmann::json::array({value.x, value.y, value.z});
}
inline nlohmann::json Vec4Json(const glm::vec4 &value)
{
    return nlohmann::json::array({value.x, value.y, value.z, value.w});
}
inline nlohmann::json QuatJson(const glm::quat &value)
{
    return nlohmann::json::array({value.x, value.y, value.z, value.w});
}

inline glm::vec2 Vec2FromJson(const nlohmann::json &value)
{
    return glm::vec2(value.at(0).get<float>(), value.at(1).get<float>());
}
inline glm::vec3 Vec3FromJson(const nlohmann::json &value)
{
    return glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
}
inline glm::vec4 Vec4FromJson(const nlohmann::json &value)
{
    return glm::vec4(
        value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(), value.at(3).get<float>()
    );
}
inline glm::quat QuatFromJson(const nlohmann::json &value)
{
    return glm::quat(
        value.at(3).get<float>(), value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()
    );
}

} // namespace Detail

template <typename C> PropertyDesc BoolProperty(std::string name, bool (C::*getter)() const, void (C::*setter)(bool))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Bool;
    property.get = [getter](const Component &component) {
        return nlohmann::json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(value.get<bool>());
        return true;
    };
    return property;
}

template <typename C> PropertyDesc IntProperty(std::string name, int (C::*getter)() const, void (C::*setter)(int))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Int;
    property.get = [getter](const Component &component) {
        return nlohmann::json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(value.get<int>());
        return true;
    };
    return property;
}

template <typename C> PropertyDesc FloatProperty(std::string name, float (C::*getter)() const, void (C::*setter)(float))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Float;
    property.get = [getter](const Component &component) {
        return nlohmann::json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(value.get<float>());
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc Vec2Property(std::string name, glm::vec2 (C::*getter)() const, void (C::*setter)(const glm::vec2 &))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Vec2;
    property.get = [getter](const Component &component) {
        return Detail::Vec2Json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(Detail::Vec2FromJson(value));
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc Vec3Property(
    std::string name, std::function<glm::vec3(const C &)> getter, std::function<void(C &, const glm::vec3 &)> setter
)
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Vec3;
    property.get = [getter](const Component &component) {
        return Detail::Vec3Json(getter(static_cast<const C &>(component)));
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        setter(static_cast<C &>(component), Detail::Vec3FromJson(value));
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc Vec3Property(std::string name, glm::vec3 (C::*getter)() const, void (C::*setter)(const glm::vec3 &))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Vec3;
    property.get = [getter](const Component &component) {
        return Detail::Vec3Json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(Detail::Vec3FromJson(value));
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc Vec4Property(std::string name, glm::vec4 (C::*getter)() const, void (C::*setter)(const glm::vec4 &))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Vec4;
    property.get = [getter](const Component &component) {
        return Detail::Vec4Json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(Detail::Vec4FromJson(value));
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc QuatProperty(std::string name, glm::quat (C::*getter)() const, void (C::*setter)(const glm::quat &))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Quat;
    property.get = [getter](const Component &component) {
        return Detail::QuatJson((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(Detail::QuatFromJson(value));
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc StringProperty(std::string name, std::string (C::*getter)() const, void (C::*setter)(const std::string &))
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::String;
    property.get = [getter](const Component &component) {
        return nlohmann::json((static_cast<const C &>(component).*getter)());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(value.get<std::string>());
        return true;
    };
    return property;
}

template <typename C>
PropertyDesc AssetIDProperty(
    std::string name, PropertyType type, AssetID (C::*getter)() const, void (C::*setter)(AssetID)
)
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = type;
    property.get = [getter](const Component &component) {
        const AssetID id = (static_cast<const C &>(component).*getter)();
        return nlohmann::json(id.IsValid() ? id.ToHex() : std::string());
    };
    property.set = [setter](Component &component, const nlohmann::json &value, SerializationContext &) {
        (static_cast<C &>(component).*setter)(AssetID::FromHex(value.get<std::string>()));
        return true;
    };
    return property;
}

template <typename C, typename E>
PropertyDesc EnumProperty(
    std::string name, E (C::*getter)() const, void (C::*setter)(E), std::vector<std::pair<E, std::string>> names
)
{
    PropertyDesc property;
    property.name = std::move(name);
    property.type = PropertyType::Enum;
    property.get = [getter, names](const Component &component) {
        const E value = (static_cast<const C &>(component).*getter)();
        for (const auto &[candidate, text] : names)
        {
            if (candidate == value)
            {
                return nlohmann::json(text);
            }
        }
        return nlohmann::json("unknown");
    };
    property.set = [setter, names](Component &component, const nlohmann::json &value, SerializationContext &) {
        const std::string text = value.get<std::string>();
        for (const auto &[candidate, name] : names)
        {
            if (name == text)
            {
                (static_cast<C &>(component).*setter)(candidate);
                return true;
            }
        }
        return false;
    };
    return property;
}

} // namespace Geni
