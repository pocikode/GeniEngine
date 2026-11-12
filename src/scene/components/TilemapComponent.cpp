#include "scene/components/TilemapComponent.h"
#include "assets/AssetManager.h"
#include "core/Engine.h"
#include "graphics/Texture.h"
#include "physics/Collider.h"
#include "physics/RigidBody.h"
#include "render/RenderQueue.h"
#include "scene/GameObject.h"
#include "scene/Reflection.h"
#include "scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Geni
{

namespace
{
// Highest tile index solidTiles may name; keeps a typo like "0-99999999" cheap.
constexpr int kMaxSolidTile = 1 << 16;
} // namespace

TilemapComponent::TilemapComponent() = default;

TilemapComponent::~TilemapComponent()
{
    ClearColliders();
}

Texture *TilemapComponent::GetTileset() const
{
    return m_tilesetHandle.IsValid() ? m_tilesetHandle.Get() : m_tileset.get();
}

void TilemapComponent::SetTileset(const std::shared_ptr<Texture> &texture)
{
    m_tileset = texture;
    m_tilesetHandle = AssetHandle<Texture>();
    m_tilesetID = texture ? texture->GetAssetID() : AssetID();
    m_chunksDirty = true;
}

void TilemapComponent::SetTileset(const AssetHandle<Texture> &texture)
{
    m_tilesetHandle = texture;
    m_tileset.reset();
    m_tilesetID = texture.GetID();
    m_chunksDirty = true;
}

void TilemapComponent::SetTilesetID(AssetID id)
{
    if (id != m_tilesetID)
    {
        m_tilesetHandle.Reset();
        m_tileset.reset();
        m_chunksDirty = true;
    }
    m_tilesetID = id;
}

void TilemapComponent::SetTilesetColumns(int columns)
{
    m_tilesetColumns = std::max(1, columns);
    m_chunksDirty = true;
}

void TilemapComponent::SetTilesetRows(int rows)
{
    m_tilesetRows = std::max(1, rows);
    m_chunksDirty = true;
}

void TilemapComponent::Resize(int width, int height)
{
    width = std::clamp(width, 0, 1 << 14);
    height = std::clamp(height, 0, 1 << 14);
    if (width == m_width && height == m_height)
    {
        return;
    }
    std::vector<int> tiles(static_cast<size_t>(width) * static_cast<size_t>(height), EmptyTile);
    for (int row = 0; row < std::min(height, m_height); ++row)
    {
        for (int column = 0; column < std::min(width, m_width); ++column)
        {
            tiles[static_cast<size_t>(row) * width + column] = m_tiles[static_cast<size_t>(row) * m_width + column];
        }
    }
    m_tiles = std::move(tiles);
    m_width = width;
    m_height = height;
    m_chunksDirty = true;
    m_collidersDirty = true;
}

void TilemapComponent::SetTileSize(const glm::vec2 &size)
{
    m_tileSize = glm::max(size, glm::vec2(1e-4f));
    m_chunksDirty = true;
    m_collidersDirty = true;
}

int TilemapComponent::GetTile(int column, int row) const
{
    if (column < 0 || row < 0 || column >= m_width || row >= m_height)
    {
        return EmptyTile;
    }
    return m_tiles[static_cast<size_t>(row) * m_width + column];
}

void TilemapComponent::SetTile(int column, int row, int tile)
{
    if (column < 0 || row < 0 || column >= m_width || row >= m_height)
    {
        return;
    }
    int &cell = m_tiles[static_cast<size_t>(row) * m_width + column];
    tile = tile < 0 ? EmptyTile : tile;
    if (cell == tile)
    {
        return;
    }
    if (IsSolidTile(cell) != IsSolidTile(tile))
    {
        m_collidersDirty = true;
    }
    cell = tile;
    m_chunksDirty = true;
}

void TilemapComponent::Fill(int tile)
{
    std::fill(m_tiles.begin(), m_tiles.end(), tile < 0 ? EmptyTile : tile);
    m_chunksDirty = true;
    m_collidersDirty = true;
}

std::string TilemapComponent::GetTilesText() const
{
    std::string text;
    for (int row = 0; row < m_height; ++row)
    {
        for (int column = 0; column < m_width; ++column)
        {
            if (column > 0)
            {
                text += ',';
            }
            text += std::to_string(m_tiles[static_cast<size_t>(row) * m_width + column]);
        }
        if (row + 1 < m_height)
        {
            text += '\n';
        }
    }
    return text;
}

void TilemapComponent::SetTilesText(const std::string &text)
{
    std::fill(m_tiles.begin(), m_tiles.end(), EmptyTile);
    std::istringstream lines(text);
    std::string line;
    int row = 0;
    while (row < m_height && std::getline(lines, line))
    {
        std::istringstream cells(line);
        std::string cell;
        int column = 0;
        while (column < m_width && std::getline(cells, cell, ','))
        {
            char *end = nullptr;
            const long value = std::strtol(cell.c_str(), &end, 10);
            m_tiles[static_cast<size_t>(row) * m_width + column] =
                end != cell.c_str() && value >= 0 ? static_cast<int>(value) : EmptyTile;
            ++column;
        }
        ++row;
    }
    m_chunksDirty = true;
    m_collidersDirty = true;
}

glm::ivec2 TilemapComponent::LocalToCell(const glm::vec2 &local) const
{
    return glm::ivec2(
        static_cast<int>(std::floor(local.x / m_tileSize.x)), static_cast<int>(std::floor(-local.y / m_tileSize.y))
    );
}

glm::vec2 TilemapComponent::CellCenter(int column, int row) const
{
    return glm::vec2(
        (static_cast<float>(column) + 0.5f) * m_tileSize.x, -(static_cast<float>(row) + 0.5f) * m_tileSize.y
    );
}

glm::ivec2 TilemapComponent::WorldToCell(const glm::vec3 &world) const
{
    const glm::mat4 toLocal = glm::inverse(m_owner->GetWorldTransform());
    return LocalToCell(glm::vec2(toLocal * glm::vec4(world, 1.0f)));
}

glm::vec3 TilemapComponent::CellToWorld(int column, int row) const
{
    return glm::vec3(m_owner->GetWorldTransform() * glm::vec4(CellCenter(column, row), 0.0f, 1.0f));
}

void TilemapComponent::SetColor(const glm::vec4 &color)
{
    m_color = color;
    m_chunksDirty = true;
}

void TilemapComponent::SetCollision(bool collision)
{
    if (collision != m_collision)
    {
        m_collision = collision;
        m_collidersDirty = true;
    }
}

void TilemapComponent::SetSolidTilesText(const std::string &text)
{
    m_solidTilesText = text;
    m_solid.clear();
    std::istringstream items(text);
    std::string item;
    while (std::getline(items, item, ','))
    {
        int first = 0;
        int last = 0;
        const size_t dash = item.find('-', item.find_first_not_of(' '));
        try
        {
            if (dash != std::string::npos && dash > 0)
            {
                first = std::stoi(item.substr(0, dash));
                last = std::stoi(item.substr(dash + 1));
            }
            else
            {
                first = last = std::stoi(item);
            }
        }
        catch (const std::exception &)
        {
            continue;
        }
        first = std::clamp(first, 0, kMaxSolidTile);
        last = std::clamp(last, 0, kMaxSolidTile);
        if (last >= static_cast<int>(m_solid.size()))
        {
            m_solid.resize(static_cast<size_t>(last) + 1, false);
        }
        for (int tile = first; tile <= last; ++tile)
        {
            m_solid[static_cast<size_t>(tile)] = true;
        }
    }
    m_collidersDirty = true;
}

bool TilemapComponent::IsSolidTile(int tile) const
{
    return tile >= 0 && tile < static_cast<int>(m_solid.size()) && m_solid[static_cast<size_t>(tile)];
}

size_t TilemapComponent::GetQuadCount() const
{
    size_t count = 0;
    for (const auto &chunk : m_chunks)
    {
        count += chunk.quads.size();
    }
    return count;
}

void TilemapComponent::Init()
{
    m_initialized = true;
    RebuildColliders();
}

void TilemapComponent::OnDestroy()
{
    ClearColliders();
}

void TilemapComponent::ResolveAssets(SerializationContext &context)
{
    if (context.assets && m_tilesetID.IsValid() && !m_tilesetHandle.IsValid())
    {
        SetTileset(context.assets->LoadTexture(m_tilesetID, true));
    }
}

void TilemapComponent::RebuildChunks()
{
    m_chunks.clear();
    const Texture *tileset = GetTileset();
    m_builtFor = tileset;
    m_chunksDirty = false;
    if (m_width == 0 || m_height == 0)
    {
        return;
    }

    const glm::vec2 cellUv(1.0f / static_cast<float>(m_tilesetColumns), 1.0f / static_cast<float>(m_tilesetRows));
    // Half a texel in from each edge, so linear filtering and MSAA never sample the
    // neighbouring tile.
    glm::vec2 inset(0.0f);
    if (tileset && tileset->GetWidth() > 0 && tileset->GetHeight() > 0)
    {
        inset =
            glm::vec2(0.5f / static_cast<float>(tileset->GetWidth()), 0.5f / static_cast<float>(tileset->GetHeight()));
    }
    const int tileCount = m_tilesetColumns * m_tilesetRows;
    const glm::vec4 color = SrgbToLinear(m_color);

    const int chunkColumns = (m_width + ChunkSize - 1) / ChunkSize;
    const int chunkRows = (m_height + ChunkSize - 1) / ChunkSize;
    for (int chunkRow = 0; chunkRow < chunkRows; ++chunkRow)
    {
        for (int chunkColumn = 0; chunkColumn < chunkColumns; ++chunkColumn)
        {
            Chunk chunk;
            const int column0 = chunkColumn * ChunkSize;
            const int row0 = chunkRow * ChunkSize;
            const int column1 = std::min(column0 + ChunkSize, m_width);
            const int row1 = std::min(row0 + ChunkSize, m_height);
            for (int row = row0; row < row1; ++row)
            {
                for (int column = column0; column < column1; ++column)
                {
                    const int tile = m_tiles[static_cast<size_t>(row) * m_width + column];
                    if (tile < 0 || tile >= tileCount)
                    {
                        continue;
                    }
                    SpriteQuad quad;
                    quad.min = glm::vec2(
                        static_cast<float>(column) * m_tileSize.x, -static_cast<float>(row + 1) * m_tileSize.y
                    );
                    quad.max = quad.min + m_tileSize;
                    const glm::vec2 uv(
                        static_cast<float>(tile % m_tilesetColumns) * cellUv.x,
                        static_cast<float>(tile / m_tilesetColumns) * cellUv.y
                    );
                    quad.uvTopLeft = uv + inset;
                    quad.uvBottomRight = uv + cellUv - inset;
                    quad.color = color;
                    chunk.quads.push_back(quad);
                }
            }
            if (chunk.quads.empty())
            {
                continue;
            }
            const glm::vec2 lo(static_cast<float>(column0) * m_tileSize.x, -static_cast<float>(row1) * m_tileSize.y);
            const glm::vec2 hi(static_cast<float>(column1) * m_tileSize.x, -static_cast<float>(row0) * m_tileSize.y);
            chunk.center = glm::vec3((lo + hi) * 0.5f, 0.0f);
            chunk.radius = 0.5f * glm::length(hi - lo);
            m_chunks.push_back(std::move(chunk));
        }
    }
}

void TilemapComponent::ClearColliders()
{
    // A body's destructor takes it out of the physics world.
    m_bodies.clear();
}

void TilemapComponent::RebuildColliders()
{
    m_collidersDirty = false;
    ClearColliders();
    if (!m_collision || !m_owner || m_width == 0 || m_height == 0)
    {
        return;
    }
    const glm::mat4 world = m_owner->GetWorldTransform();
    const glm::vec3 scale(
        glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))
    );
    const glm::quat rotation = m_owner->GetWorldRotation();
    const Scene *scene = m_owner->GetScene();
    auto &physics = Engine::GetInstance().GetPhysicsManager();

    for (int row = 0; row < m_height; ++row)
    {
        int column = 0;
        while (column < m_width)
        {
            if (!IsSolid(column, row))
            {
                ++column;
                continue;
            }
            int end = column;
            while (end < m_width && IsSolid(end, row))
            {
                ++end;
            }
            // One box for the run [column, end) of this row, 1 unit deep in Z.
            const glm::vec3 size(static_cast<float>(end - column) * m_tileSize.x, m_tileSize.y, 1.0f);
            const glm::vec2 center(
                0.5f * static_cast<float>(column + end) * m_tileSize.x, -(static_cast<float>(row) + 0.5f) * m_tileSize.y
            );
            auto body = std::make_unique<RigidBody>(BodyType::Static, std::make_shared<BoxCollider>(size), 0.0f, 0.6f);
            body->SetScale(scale);
            const glm::vec3 position(world * glm::vec4(center, 0.0f, 1.0f));
            body->SetWorldPosition(scene ? scene->ToWorld(position) : glm::dvec3(position));
            body->SetRotation(rotation);
            body->SetUserData(m_owner->GetID());
            physics.AddRigidBody(body.get());
            m_bodies.push_back(std::move(body));
            column = end;
        }
    }
}

void TilemapComponent::Update(float deltaTime)
{
    (void)deltaTime;
    if (m_initialized && m_collidersDirty)
    {
        RebuildColliders();
    }
    Texture *tileset = GetTileset();
    // A tileset that is still loading draws nothing.
    if (!tileset && (m_tilesetID.IsValid() || m_tilesetHandle.IsValid()))
    {
        return;
    }
    if (m_chunksDirty || tileset != m_builtFor)
    {
        RebuildChunks();
    }
    if (m_color.a <= 0.0f)
    {
        return;
    }

    SpriteCommand command;
    command.texture = tileset;
    command.filter = m_filter;
    command.modelMatrix = m_owner->GetWorldTransform();
    command.layer = m_layer;
    auto &queue = Engine::GetInstance().GetRenderQueue();
    for (const auto &chunk : m_chunks)
    {
        command.quads = chunk.quads.data();
        command.quadCount = static_cast<uint32_t>(chunk.quads.size());
        command.boundsCenter = chunk.center;
        command.boundsRadius = chunk.radius;
        queue.SubmitSprites(command);
    }
}

namespace
{
const bool s_registered = RegisterComponentType<TilemapComponent>(
    "TilemapComponent",
    {
        AssetIDProperty<TilemapComponent>(
            "tileset", AssetType::Texture, &TilemapComponent::GetTilesetID, &TilemapComponent::SetTilesetID
        ),
        IntProperty<TilemapComponent>(
            "tilesetColumns", &TilemapComponent::GetTilesetColumns, &TilemapComponent::SetTilesetColumns
        ),
        IntProperty<TilemapComponent>(
            "tilesetRows", &TilemapComponent::GetTilesetRows, &TilemapComponent::SetTilesetRows
        ),
        // Width and height come before the tiles: they size the grid the text fills.
        IntProperty<TilemapComponent>("width", &TilemapComponent::GetWidth, &TilemapComponent::SetWidth),
        IntProperty<TilemapComponent>("height", &TilemapComponent::GetHeight, &TilemapComponent::SetHeight),
        Vec2Property<TilemapComponent>("tileSize", &TilemapComponent::GetTileSize, &TilemapComponent::SetTileSize),
        StringProperty<TilemapComponent>("tiles", &TilemapComponent::GetTilesText, &TilemapComponent::SetTilesText),
        Vec4Property<TilemapComponent>("color", &TilemapComponent::GetColor, &TilemapComponent::SetColor),
        IntProperty<TilemapComponent>("layer", &TilemapComponent::GetLayer, &TilemapComponent::SetLayer),
        EnumProperty<TilemapComponent, SpriteFilter>(
            "filter", &TilemapComponent::GetFilter, &TilemapComponent::SetFilter,
            {{SpriteFilter::Linear, "linear"}, {SpriteFilter::Nearest, "nearest"}}
        ),
        BoolProperty<TilemapComponent>("collision", &TilemapComponent::GetCollision, &TilemapComponent::SetCollision),
        StringProperty<TilemapComponent>(
            "solidTiles", &TilemapComponent::GetSolidTilesText, &TilemapComponent::SetSolidTilesText
        ),
    }
);
} // namespace

} // namespace Geni

namespace Geni
{
void LinkTilemapComponent()
{
}
} // namespace Geni
