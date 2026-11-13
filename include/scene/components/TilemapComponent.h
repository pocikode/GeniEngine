#pragma once

#include "assets/AssetHandle.h"
#include "render/Sprite.h"
#include "scene/Component.h"
#include <glm/ext/vector_int2.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Geni
{

class RigidBody;
class Texture;

// A grid of tiles cut from one tileset texture (docs/design/0017). The object's origin is
// the map's top-left corner; cell (column, row) spans x from column * tileSize.x and
// y down from -row * tileSize.y. Tile indices count the tileset's tiles left to right, top
// to bottom; -1 is an empty cell.
//
// The map is drawn in chunks of ChunkSize x ChunkSize cells, each a SpriteCommand with
// its own bounds, so chunks off screen are culled. With collision on, the cells whose
// tile is listed in solidTiles become static boxes (one per horizontal run).
class TilemapComponent : public Component
{
    COMPONENT(TilemapComponent);

  public:
    static constexpr int ChunkSize = 16;
    static constexpr int EmptyTile = -1;

    TilemapComponent();
    ~TilemapComponent() override;

    void Init() override;
    void Update(float deltaTime) override;
    void EditUpdate() override
    {
        Update(0.0f);
    }
    void OnDestroy() override;
    void ResolveAssets(SerializationContext &context) override;

    Texture *GetTileset() const;
    void SetTileset(const std::shared_ptr<Texture> &texture);
    void SetTileset(const AssetHandle<Texture> &texture);
    AssetID GetTilesetID() const
    {
        return m_tilesetID;
    }
    void SetTilesetID(AssetID id);

    // Tiles in the tileset image.
    int GetTilesetColumns() const
    {
        return m_tilesetColumns;
    }
    void SetTilesetColumns(int columns);
    int GetTilesetRows() const
    {
        return m_tilesetRows;
    }
    void SetTilesetRows(int rows);

    // Map size in cells. Resizing keeps the tiles that still fit.
    int GetWidth() const
    {
        return m_width;
    }
    int GetHeight() const
    {
        return m_height;
    }
    void Resize(int width, int height);
    void SetWidth(int width)
    {
        Resize(width, m_height);
    }
    void SetHeight(int height)
    {
        Resize(m_width, height);
    }
    glm::vec2 GetTileSize() const
    {
        return m_tileSize;
    }
    void SetTileSize(const glm::vec2 &size);

    // EmptyTile outside the map.
    int GetTile(int column, int row) const;
    // Ignored outside the map.
    void SetTile(int column, int row, int tile);
    void Fill(int tile);

    // The tiles as text: rows separated by newlines, cells by commas (Tiled's CSV layer).
    // Missing cells are empty; extra cells are ignored.
    std::string GetTilesText() const;
    void SetTilesText(const std::string &text);

    // A point in the object's space to the cell it is in (may be outside the map), and a
    // cell's centre in the object's space.
    glm::ivec2 LocalToCell(const glm::vec2 &local) const;
    glm::vec2 CellCenter(int column, int row) const;
    // The same with world positions (through the object's transform).
    glm::ivec2 WorldToCell(const glm::vec3 &world) const;
    glm::vec3 CellToWorld(int column, int row) const;

    glm::vec4 GetColor() const
    {
        return m_color;
    }
    void SetColor(const glm::vec4 &color);
    // Lit by the scene's lights (docs/design/0021); off: colours exactly as painted.
    bool GetLit() const
    {
        return m_lit;
    }
    void SetLit(bool lit)
    {
        m_lit = lit;
    }
    int GetLayer() const
    {
        return m_layer;
    }
    void SetLayer(int layer)
    {
        m_layer = layer;
    }
    SpriteFilter GetFilter() const
    {
        return m_filter;
    }
    void SetFilter(SpriteFilter filter)
    {
        m_filter = filter;
    }

    bool GetCollision() const
    {
        return m_collision;
    }
    void SetCollision(bool collision);
    // Tile indices that collide, as text: "0,1,2" or ranges "0-7,12".
    std::string GetSolidTilesText() const
    {
        return m_solidTilesText;
    }
    void SetSolidTilesText(const std::string &text);
    bool IsSolidTile(int tile) const;
    // True when the cell holds a solid tile (collision on or not).
    bool IsSolid(int column, int row) const
    {
        return IsSolidTile(GetTile(column, row));
    }
    // Static boxes made for the solid tiles (for tests).
    size_t GetColliderCount() const
    {
        return m_bodies.size();
    }
    // Quads drawn last frame and chunks submitted (for tests and stats).
    size_t GetQuadCount() const;
    size_t GetChunkCount() const
    {
        return m_chunks.size();
    }

  private:
    struct Chunk
    {
        std::vector<SpriteQuad> quads;
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    void RebuildChunks();
    void RebuildColliders();
    void ClearColliders();

    AssetHandle<Texture> m_tilesetHandle;
    std::shared_ptr<Texture> m_tileset;
    AssetID m_tilesetID;
    int m_tilesetColumns = 1;
    int m_tilesetRows = 1;
    int m_width = 0;
    int m_height = 0;
    glm::vec2 m_tileSize{1.0f};
    std::vector<int> m_tiles;
    glm::vec4 m_color{1.0f};
    int m_layer = 0;
    bool m_lit = false;
    SpriteFilter m_filter = SpriteFilter::Nearest;

    bool m_collision = false;
    std::string m_solidTilesText;
    std::vector<bool> m_solid;

    std::vector<Chunk> m_chunks;
    bool m_chunksDirty = true;
    // The tileset the chunks' texture coordinates were built for (they inset by half a
    // texel of that size).
    const Texture *m_builtFor = nullptr;
    bool m_initialized = false;
    bool m_collidersDirty = false;
    std::vector<std::unique_ptr<RigidBody>> m_bodies;
};

} // namespace Geni
