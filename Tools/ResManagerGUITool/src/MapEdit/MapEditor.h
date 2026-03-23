#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <imgui.h>

class Map;

// ─────────────────────────────────────────────────────────────
// ObstacleEdit – AABB local trong region
// ─────────────────────────────────────────────────────────────
struct ObstacleEdit
{
    float x = 0.f, y = 0.f, w = 32.f, h = 32.f;
    float x1() const { return x + w; }
    float y1() const { return y + h; }
};

// ─────────────────────────────────────────────────────────────
// PlacedTile – một tile đã được paint lên region
// Đây là đơn vị lưu trong file dat
// ─────────────────────────────────────────────────────────────
struct PlacedTile
{
    std::string sprPath;   // đường dẫn tới .spr file
    bool   useAnim  = false; // true=dùng animation, false=dùng frame tĩnh
    int    animIdx  = 0;     // index animation (khi useAnim=true)
    int    frameIdx = 0;     // index frame tuyệt đối (khi useAnim=false)
    float  x        = 0.f;  // world-space position (tuyệt đối)
    float  y        = 0.f;
    int    layer    = 0;     // layer thứ mấy

    // ── runtime preview (không lưu file) ─────────────────────
    unsigned int previewTexID = 0;
    int          frameW = 0, frameH = 0;
    int          pivotX = 0, pivotY = 0;

    // animation playback
    std::vector<unsigned int> frameTex;   // textures cho từng frame (anim hoặc all-frames)
    float animTimer   = 0.f;
    int   curFrame    = 0;
    int   totalFrames = 0;
    int   fps         = 30;

    bool loaded = false;
};

// ─────────────────────────────────────────────────────────────
// TilesetSprEntry – một file .spr trong tileset panel
// ─────────────────────────────────────────────────────────────
struct TilesetSprEntry
{
    std::string path;        // full path
    std::string name;        // filename (display)

    struct AnimInfo {
        std::string name;
        int startFrame = 0, frameCount = 0, fps = 30;
        bool loop = true;
    };
    std::vector<AnimInfo> anims;
    int totalFrames = 0;

    // preview textures (tất cả frames)
    std::vector<unsigned int> frameTex;
    int frameW = 0, frameH = 0;
    int pivotX = 0, pivotY = 0;
    float animTimer = 0.f;
    int   curFrame  = 0;

    bool loaded = false;
};

// ─────────────────────────────────────────────────────────────
// TilesetFolder – một thư mục tileset chứa nhiều .spr
// ─────────────────────────────────────────────────────────────
struct TilesetFolder
{
    std::string folderPath;
    std::string name;
    std::vector<TilesetSprEntry> sprs;
    bool open = false;
};

// ─────────────────────────────────────────────────────────────
// PaintBrush – brush hiện tại người dùng đang cầm
// ─────────────────────────────────────────────────────────────
struct PaintBrush
{
    bool   valid    = false;
    std::string sprPath;
    bool   useAnim  = false;
    int    animIdx  = 0;   // index anim trong TilesetSprEntry::anims
    int    frameIdx = 0;   // index frame tuyệt đối
    int    fps      = 30;
    int    frameW   = 0, frameH = 0;
    int    pivotX   = 0, pivotY = 0;
    int blockW = 1;
    int blockH = 1;
    // texture để hiện thị cursor preview
    unsigned int previewTex = 0;
    // tất cả frames của brush (để animate cursor)
    std::vector<unsigned int> frameTex;
    float animTimer = 0.f;
    int   curFrame  = 0;
    int   totalFrames = 0;
};

// ─────────────────────────────────────────────────────────────
// MapEditor
// ─────────────────────────────────────────────────────────────
class MapEditor
{
public:
    enum class EditMode { Select, PaintTile, AddObstacle, EraseObstacle,PaintMultiTile };

    MapEditor();
    ~MapEditor();

    // ── File operations ─────────────────────────────────────
    bool LoadMap(const std::string& folderPath);
    bool SaveMap();
    bool SaveMapTo(const std::string& folderPath);
    bool CreateNewMap(const std::string& folderPath, int width, int height, int unitSize);

    // ── Properties ──────────────────────────────────────────
    void SetMapProperties(int width, int height, int unitSize);
    void GetMapProperties(int& width, int& height, int& unitSize) const;

    // ── Region / obstacle ───────────────────────────────────
    void SelectRegion(int regionX, int regionY);
    void AddObstacleToRegion(float lx, float ly, float w, float h);
    void RemoveObstacleFromRegion(int obstacleIndex);
    void ModifyObstacleInRegion(int obstacleIndex, float lx, float ly, float w, float h);

    // ── Tile painting ───────────────────────────────────────
    void PlaceTileAt(float worldX, float worldY, int layer);
    void PlaceTileBlock(float worldX, float worldY, int layer, int width, int height);
    void EraseTileAt(float worldX, float worldY, int layer);

    // ── Tileset panel ────────────────────────────────────────
    void AddTilesetFolder(const std::string& folderPath);
    void RemoveTilesetFolder(int idx);
    void LoadTilesetSpr(TilesetSprEntry& entry);
    void FreeTilesetSpr(TilesetSprEntry& entry);
    void EraseTileBlock(float wx, float wy, int layer, int bw, int bh);

    // ── Brush ────────────────────────────────────────────────
    void SetBrushFromTileset(TilesetSprEntry& entry, bool useAnim, int animOrFrameIdx);

    // ── Rendering ───────────────────────────────────────────
    void RenderEditor();
    void RenderRegionEditor();
    void RenderObstacleEditor();

    bool HasUnsavedChanges() const { return _unsavedChanges; }
    const std::string& GetCurrentMapPath() const { return _currentMapPath; }

private:
    // ── Sub-panels ──────────────────────────────────────────
    void RenderMenuBar();
    void RenderToolbar();
    void RenderTilesetPanel();    // ← panel mới (cột trái)
    void RenderCanvasPanel();     // canvas vẽ map
    void RenderObstaclePanel();   // cột phải: obstacle editor
    void RenderLayerPanel();      // cột phải: layer / tile info
    void RenderStatusBar();
    void RenderDialogs();
    void RenderMapPropertiesPanel();

    // ── Internal ────────────────────────────────────────────
    void MarkDirty();
    void ClearSelection();
    void EnsureRegionArrays(int rw, int rh);
    void TickPreviews(float dt);

    // tile preview helpers
    void LoadTilePreview(PlacedTile& tile);
    void FreeTilePreview(PlacedTile& tile);

    // tileset helpers
    void TickTilesetAnimations(float dt);

    // ── Map data ────────────────────────────────────────────
    std::unique_ptr<Map> _map;
    std::string  _currentMapPath;
    bool         _unsavedChanges = false;
    int _mapWidth = 0, _mapHeight = 0, _unitSize = 64;

    // Obstacle cache [regionY][regionX]
    std::vector<std::vector<std::vector<ObstacleEdit>>> _regionObstacles;

    // Tile cache [regionY][regionX][layer] = list of PlacedTile
    std::vector<std::vector<std::vector<std::vector<PlacedTile>>>> _regionTiles;

    int _paintLayer = 0; // layer đang paint

    // ── Tileset data ─────────────────────────────────────────
    std::vector<TilesetFolder> _tilesetFolders;
    PaintBrush                 _brush;            // brush hiện tại

    // ── Selection ────────────────────────────────────────────
    int _selectedRegionX       = -1;
    int _selectedRegionY       = -1;
    int _selectedObstacleIndex = -1;
    int _selectedTileIdx       = -1;  // trong region+layer hiện tại

    // ── Edit state ───────────────────────────────────────────
    EditMode _editMode          = EditMode::Select;
    bool     _isDragging        = false;
    float    _dragStartWX       = 0.f;
    float    _dragStartWY       = 0.f;
    bool     _isPainting        = false; // LMB held trong PaintTile mode
    int      _obstacleSnapSize  = 16;

    // ── View ─────────────────────────────────────────────────
    float _viewScale   = 1.f;
    float _viewOffsetX = 0.f;
    float _viewOffsetY = 0.f;
    bool  _isPanning   = false;

    // ── Status ───────────────────────────────────────────────
    char  _statusMsg[256]{};
    float _statusTimer = 0.f;

    // ── Dialogs ──────────────────────────────────────────────
    bool _dlgNewMap  = false;
    bool _dlgLoadMap = false;
    bool _dlgSaveAs  = false;
    bool _dlgAddTilesetFolder = false;
    char _dlgBuf[512]{};
    char _newMapFolder[512]{};
    char _newMapName[256]{};
    int  _newMapW    = 2048;
    int  _newMapH    = 2048;
    int  _newMapUnit = 256;
    char _tilesetFolderBuf[512]{};

    // ── Export type ──────────────────────────────────────────
    // 0 = region_S only (server map, no visual tiles)
    // 1 = region_S + region_C (full client+server map)
    int  _exportType = 1; // default: C (có tile data)

    // ── VFS root ─────────────────────────────────────────────
    // Thư mục gốc để tính relative path khi export sprPath.
    // Mặc định = current_path() (thư mục chạy editor, e.g. bin/SResManager).
    // sprPath được lưu dạng relative: "titleset/abc.spr"
    // Client tìm ở: res/paks/res/<sprPath>
    std::string _vfsRoot;
    char        _vfsRootBuf[512]{};
    bool        _dlgVfsRoot = false;

    // ── UI flags ─────────────────────────────────────────────
    bool _showGrid       = true;
    bool _showObstacles  = true;
    bool _showTiles      = true;
};
