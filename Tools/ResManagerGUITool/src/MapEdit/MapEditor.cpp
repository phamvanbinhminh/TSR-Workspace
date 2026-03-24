
#include "MapEditor.h"
#include "ResManager/Map/Map.h"
#include "ResManager/Spr/Spr.h"
#include "Core/AppConfig.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <combaseapi.h>

#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>

namespace fs = std::filesystem;

// ─── Windows folder picker ─────────────────────────────────
static std::string BrowseForFolder(const char* title = "Select Folder")
{
    BROWSEINFOA bi{};
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return {};
    char path[MAX_PATH];
    std::string result;
    if (SHGetPathFromIDListA(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}

// ─── Upload RGBA pixels to GL texture ──────────────────────
static unsigned int UploadTex(const void* pixels, int w, int h)
{
    if (!pixels || w <= 0 || h <= 0) return 0;
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════
// Thread pool: WorkerLoop / StartWorkers / StopWorkers
// ═══════════════════════════════════════════════════════════
// ── SPR file cache (shared across worker threads) ─────────
// Tránh đọc cùng 1 file nhiều lần khi nhiều tiles dùng chung 1 SPR
struct SprCacheEntry {
    SprLoadedData data;
    bool          ok = false;
};
static std::mutex                                        s_sprCacheMutex;
static std::unordered_map<std::string, SprCacheEntry>   s_sprCache;

static const SprCacheEntry* GetOrLoadSpr(const std::string& resolvedPath)
{
    // Fast path: đã có trong cache
    {
        std::lock_guard<std::mutex> lk(s_sprCacheMutex);
        auto it = s_sprCache.find(resolvedPath);
        if (it != s_sprCache.end())
            return &it->second;
    }

    // Load từ disk (không giữ lock)
    SprCacheEntry entry;
    SprReader reader;
    entry.ok = reader.LoadFromFile(resolvedPath, entry.data);

    // Insert vào cache
    {
        std::lock_guard<std::mutex> lk(s_sprCacheMutex);
        // Double-check: thread khác có thể đã insert rồi
        auto it = s_sprCache.find(resolvedPath);
        if (it != s_sprCache.end())
            return &it->second;
        s_sprCache[resolvedPath] = std::move(entry);
        return &s_sprCache[resolvedPath];
    }
}

void MapEditor::WorkerLoop()
{
    while (true)
    {
        LoadTask task;
        {
            std::unique_lock<std::mutex> lk(_taskMutex);
            _taskCV.wait(lk, [this]{ return _workerStop || !_taskQueue.empty(); });
            if (_workerStop && _taskQueue.empty()) return;
            task = std::move(_taskQueue.back());
            _taskQueue.pop_back();
        }

        // Tile đã bị free/reset trong lúc chờ → bỏ qua
        if (!task.tile || task.tile->loadState != 1) continue;

        PendingUpload result;
        result.tile    = task.tile;
        result.success = false;

        // Resolve path
        std::string resolvedPath = task.sprPath;
        if (!resolvedPath.empty() && !fs::path(resolvedPath).is_absolute())
        {
            if (!task.vfsRoot.empty())
            {
                std::string candidate = task.vfsRoot + "/" + resolvedPath;
                if (fs::exists(candidate)) resolvedPath = candidate;
            }
        }

        // Dùng SPR cache để tránh đọc cùng 1 file nhiều lần
        const SprCacheEntry* cached = GetOrLoadSpr(resolvedPath);
        if (cached && cached->ok)
        {
            const SprLoadedData& data = cached->data;
            result.pivotX  = data.header.pivotX;
            result.pivotY  = data.header.pivotY;
            result.useAnim = task.useAnim;

            if (task.useAnim && task.animIdx < (int)data.animations.size())
            {
                const auto& a = data.animations[task.animIdx];
                result.fps = a.fps > 0 ? (int)a.fps : 30;
                int start  = (int)a.startFrame;
                int count  = (int)a.frameCount;
                for (int i = start; i < start + count && i < (int)data.frames.size(); i++)
                {
                    const auto& f = data.frames[i];
                    PendingUpload::FrameData fd;
                    fd.pixels = f.pixels; // copy pixels (cache giữ bản gốc)
                    fd.w = f.width; fd.h = f.height;
                    result.frames.push_back(std::move(fd));
                }
            }
            else
            {
                result.fps = 0;
                int fi = task.frameIdx;
                if (fi >= 0 && fi < (int)data.frames.size())
                {
                    const auto& f = data.frames[fi];
                    PendingUpload::FrameData fd;
                    fd.pixels = f.pixels;
                    fd.w = f.width; fd.h = f.height;
                    result.frames.push_back(std::move(fd));
                }
            }
            result.success = true;
        }

        task.tile->loadState = 2; // pending GL upload

        {
            std::lock_guard<std::mutex> lk(_uploadMutex);
            _pendingUploads.push_back(std::move(result));
        }
    }
}

void MapEditor::StartWorkers()
{
    _workerStop = false;
    _workerThreads.reserve(kNumWorkers);
    for (int i = 0; i < kNumWorkers; i++)
        _workerThreads.emplace_back(&MapEditor::WorkerLoop, this);
}

void MapEditor::StopWorkers()
{
    {
        std::lock_guard<std::mutex> lk(_taskMutex);
        _workerStop = true;
    }
    _taskCV.notify_all();
    for (auto& t : _workerThreads)
        if (t.joinable()) t.join();
    _workerThreads.clear();
}

MapEditor::MapEditor()
{
    // Mặc định VFS root = thư mục chạy editor (bin/SResManager)
    _vfsRoot = fs::current_path().generic_string();
    strncpy(_vfsRootBuf, _vfsRoot.c_str(), sizeof(_vfsRootBuf) - 1);
    StartWorkers();
}

MapEditor::~MapEditor()
{
    StopWorkers();
    if (_loadThread.joinable()) _loadThread.join();

    for (auto& rowY : _regionTiles)
    for (auto& rowX : rowY)
    for (auto& layer : rowX)
    for (auto& tile  : layer)
        FreeTilePreview(tile);

    for (auto& folder : _tilesetFolders)
    for (auto& spr    : folder.sprs)
        FreeTilesetSpr(spr);
}

// ═══════════════════════════════════════════════════════════
// Tileset folder
// ═══════════════════════════════════════════════════════════
void MapEditor::AddTilesetFolder(const std::string& folderPath)
{
    if (!fs::is_directory(folderPath)) return;
    TilesetFolder tf;
    tf.folderPath = folderPath;
    tf.name       = fs::path(folderPath).filename().string();
    tf.open       = true;
    for (const auto& e : fs::directory_iterator(folderPath))
    {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".spr") continue;
        TilesetSprEntry se;
        se.path = e.path().string();
        se.name = e.path().filename().string();
        tf.sprs.push_back(std::move(se));
    }
    std::sort(tf.sprs.begin(), tf.sprs.end(),
              [](const TilesetSprEntry& a, const TilesetSprEntry& b){ return a.name < b.name; });
    _tilesetFolders.push_back(std::move(tf));
    snprintf(_statusMsg, sizeof(_statusMsg), "Added tileset: %s (%d sprs)",
             fs::path(folderPath).filename().string().c_str(),
             (int)_tilesetFolders.back().sprs.size());
    _statusTimer = 3.f;
}

void MapEditor::RemoveTilesetFolder(int idx)
{
    if (idx < 0 || idx >= (int)_tilesetFolders.size()) return;
    for (auto& spr : _tilesetFolders[idx].sprs) FreeTilesetSpr(spr);
    _tilesetFolders.erase(_tilesetFolders.begin() + idx);
}

void MapEditor::LoadTilesetSpr(TilesetSprEntry& entry)
{
    if (entry.loaded) return;
    entry.loaded = true;
    SprReader reader;
    SprLoadedData data;
    if (!reader.LoadFromFile(entry.path, data)) return;
    entry.pivotX = data.header.pivotX;
    entry.pivotY = data.header.pivotY;
    entry.totalFrames = (int)data.frames.size();
    for (const auto& f : data.frames)
    {
        unsigned int tex = UploadTex(f.pixels.empty() ? nullptr : f.pixels.data(), f.width, f.height);
        entry.frameTex.push_back(tex);
        if (entry.frameW == 0) { entry.frameW = f.width; entry.frameH = f.height; }
    }
    for (const auto& a : data.animations)
    {
        TilesetSprEntry::AnimInfo ai;
        ai.name       = std::string(a.name);
        ai.startFrame = (int)a.startFrame;
        ai.frameCount = (int)a.frameCount;
        ai.fps        = (int)(a.fps > 0 ? a.fps : 30);
        ai.loop       = (a.loop != 0);
        entry.anims.push_back(ai);
    }
}

void MapEditor::FreeTilesetSpr(TilesetSprEntry& entry)
{
    for (auto t : entry.frameTex) if (t) glDeleteTextures(1, &t);
    entry.frameTex.clear();
    entry.loaded = false;
    entry.curFrame = 0;
}

// ═══════════════════════════════════════════════════════════
// Brush
// ═══════════════════════════════════════════════════════════
void MapEditor::SetBrushFromTileset(TilesetSprEntry& entry, bool useAnim, int idx)
{
    if (!entry.loaded) LoadTilesetSpr(entry);
    _brush.valid   = true;
    _brush.sprPath = entry.path;
    _brush.useAnim = useAnim;
    _brush.frameW  = entry.frameW;
    _brush.frameH  = entry.frameH;
    _brush.pivotX  = entry.pivotX;
    _brush.pivotY  = entry.pivotY;
    _brush.frameTex.clear();

    if (useAnim && idx >= 0 && idx < (int)entry.anims.size())
    {
        _brush.animIdx  = idx;
        _brush.frameIdx = 0;
        _brush.fps      = entry.anims[idx].fps;
        int start = entry.anims[idx].startFrame;
        int count = entry.anims[idx].frameCount;
        for (int i = start; i < start + count && i < (int)entry.frameTex.size(); i++)
            _brush.frameTex.push_back(entry.frameTex[i]);
    }
    else
    {
        _brush.useAnim  = false;
        _brush.animIdx  = 0;
        int fi = (idx >= 0 && idx < (int)entry.frameTex.size()) ? idx : 0;
        _brush.frameIdx = fi;
        _brush.fps      = 0;
        _brush.frameTex.push_back(fi < (int)entry.frameTex.size() ? entry.frameTex[fi] : 0u);
    }

    _brush.totalFrames = (int)_brush.frameTex.size();
    _brush.curFrame    = 0;
    _brush.animTimer   = 0.f;
    _brush.previewTex  = _brush.frameTex.empty() ? 0 : _brush.frameTex[0];
    _editMode = EditMode::PaintTile;
}

// ═══════════════════════════════════════════════════════════
// Tick animations
// ═══════════════════════════════════════════════════════════
void MapEditor::TickTilesetAnimations(float dt)
{
    for (auto& folder : _tilesetFolders)
    for (auto& spr : folder.sprs)
    {
        if (!spr.loaded || spr.frameTex.empty()) continue;
        int fps = (!spr.anims.empty()) ? spr.anims[0].fps : 0;
        if (fps <= 0) continue;
        spr.animTimer += dt;
        float dur = 1.f / (float)fps;
        while (spr.animTimer >= dur) {
            spr.animTimer -= dur;
            spr.curFrame = (spr.curFrame + 1) % (int)spr.frameTex.size();
        }
    }
}

void MapEditor::TickPreviews(float dt)
{
    if (_brush.valid && _brush.useAnim && _brush.totalFrames > 1 && _brush.fps > 0)
    {
        _brush.animTimer += dt;
        float dur = 1.f / (float)_brush.fps;
        while (_brush.animTimer >= dur) {
            _brush.animTimer -= dur;
            _brush.curFrame = (_brush.curFrame + 1) % _brush.totalFrames;
        }
        _brush.previewTex = _brush.frameTex[_brush.curFrame];
    }
    for (auto& rowY : _regionTiles)
    for (auto& rowX : rowY)
    for (auto& layer : rowX)
    for (auto& tile : layer)
    {
        if (!tile.loaded || !tile.useAnim || tile.totalFrames <= 1 || tile.fps <= 0) continue;
        tile.animTimer += dt;
        float dur = 1.f / (float)tile.fps;
        while (tile.animTimer >= dur) {
            tile.animTimer -= dur;
            tile.curFrame = (tile.curFrame + 1) % tile.totalFrames;
        }
        if (!tile.frameTex.empty())
            tile.previewTexID = tile.frameTex[tile.curFrame];
    }
    TickTilesetAnimations(dt);
}

// ═══════════════════════════════════════════════════════════
// Tile preview (placed tiles)
// ═══════════════════════════════════════════════════════════
void MapEditor::LoadTilePreview(PlacedTile& tile)
{
    if (tile.loaded) return;
    tile.loaded = true;

    // Resolve path: nếu sprPath là relative, thử restore absolute từ _vfsRoot
    std::string resolvedPath = tile.sprPath;
    if (!resolvedPath.empty() && !fs::path(resolvedPath).is_absolute())
    {
        if (!_vfsRoot.empty())
        {
            std::string candidate = _vfsRoot + "/" + resolvedPath;
            if (fs::exists(candidate))
                resolvedPath = candidate;
        }
    }

    SprReader reader;
    SprLoadedData data;
    if (!reader.LoadFromFile(resolvedPath, data)) return;
    tile.pivotX = data.header.pivotX;
    tile.pivotY = data.header.pivotY;
    if (tile.useAnim && tile.animIdx < (int)data.animations.size())
    {
        const auto& a = data.animations[tile.animIdx];
        tile.fps = a.fps > 0 ? (int)a.fps : 30;
        int start = (int)a.startFrame, count = (int)a.frameCount;
        for (int i = start; i < start + count && i < (int)data.frames.size(); i++)
        {
            const auto& f = data.frames[i];
            tile.frameTex.push_back(UploadTex(f.pixels.empty() ? nullptr : f.pixels.data(), f.width, f.height));
            if (tile.frameW == 0) { tile.frameW = f.width; tile.frameH = f.height; }
        }
    }
    else
    {
        tile.fps = 0;
        int fi = tile.frameIdx;
        if (fi >= 0 && fi < (int)data.frames.size())
        {
            const auto& f = data.frames[fi];
            tile.frameTex.push_back(UploadTex(f.pixels.empty() ? nullptr : f.pixels.data(), f.width, f.height));
            tile.frameW = f.width; tile.frameH = f.height;
        }
    }
    tile.totalFrames  = (int)tile.frameTex.size();
    tile.curFrame     = 0;
    tile.animTimer    = 0.f;
    tile.previewTexID = tile.frameTex.empty() ? 0 : tile.frameTex[0];
}

void MapEditor::FreeTilePreview(PlacedTile& tile)
{
    for (auto t : tile.frameTex) if (t) glDeleteTextures(1, &t);
    tile.frameTex.clear();
    tile.previewTexID = 0;
    tile.loaded    = false;
    tile.loadState = 0; // reset về unloaded
}

// ═══════════════════════════════════════════════════════════
// QueueTileLoad  — đẩy tile vào background thread để đọc SPR
// Chỉ cần decode pixel, KHÔNG gọi OpenGL.
// ═══════════════════════════════════════════════════════════
void MapEditor::QueueTileLoad(PlacedTile& tile)
{
    if (tile.loadState != 0) return; // đã queue / loading / loaded
    tile.loadState = 1; // queued

    LoadTask task;
    task.tile     = &tile;
    task.sprPath  = tile.sprPath;
    task.vfsRoot  = _vfsRoot;
    task.useAnim  = tile.useAnim;
    task.animIdx  = tile.animIdx;
    task.frameIdx = tile.frameIdx;

    {
        std::lock_guard<std::mutex> lk(_taskMutex);
        _taskQueue.push_back(std::move(task));
    }
    _taskCV.notify_one(); // đánh thức 1 worker
}

// ═══════════════════════════════════════════════════════════
// PollPendingUploads  — gọi từ GUI thread, upload GL texture
// từ kết quả đã được decode bởi worker threads.
// ═══════════════════════════════════════════════════════════
void MapEditor::PollPendingUploads()
{
    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lk(_uploadMutex);
        batch.swap(_pendingUploads);
    }

    for (auto& pu : batch)
    {
        if (!pu.tile) continue;
        PlacedTile& tile = *pu.tile;

        // Nếu tile bị free trong lúc đang load → bỏ qua
        if (tile.loadState != 2) continue;

        if (!pu.success || pu.frames.empty())
        {
            tile.loadState = 3; // đánh dấu xong (dù fail) để không retry mãi
            tile.loaded    = false;
            continue;
        }

        tile.pivotX = pu.pivotX;
        tile.pivotY = pu.pivotY;
        tile.fps    = pu.fps;

        for (const auto& fd : pu.frames)
        {
            unsigned int tex = UploadTex(fd.pixels.empty() ? nullptr : fd.pixels.data(), fd.w, fd.h);
            tile.frameTex.push_back(tex);
            if (tile.frameW == 0) { tile.frameW = fd.w; tile.frameH = fd.h; }
        }

        tile.totalFrames  = (int)tile.frameTex.size();
        tile.curFrame     = 0;
        tile.animTimer    = 0.f;
        tile.previewTexID = tile.frameTex.empty() ? 0 : tile.frameTex[0];
        tile.loaded       = true;
        tile.loadState    = 3; // loaded
    }
}

// ═══════════════════════════════════════════════════════════
// PollLoadResult  — gọi từ GUI thread để nhận kết quả LoadMap bg thread
// ═══════════════════════════════════════════════════════════
void MapEditor::PollLoadResult()
{
    if (!_isLoading.load()) return;

    std::unique_ptr<LoadResult> result;
    {
        std::lock_guard<std::mutex> lk(_loadResultMutex);
        result = std::move(_pendingResult);
    }
    if (!result) return; // chưa xong

    // Load thread xong → apply kết quả trên GUI thread
    _isLoading.store(false);
    if (_loadThread.joinable()) _loadThread.join();

    if (!result->success)
    {
        snprintf(_statusMsg, sizeof(_statusMsg), "LOAD FAILED: %s", result->folderPath.c_str());
        _statusTimer = 5.f;
        return;
    }

    _currentMapPath = result->folderPath;
    _mapWidth       = result->mapWidth;
    _mapHeight      = result->mapHeight;
    _unitSize       = result->unitSize;
    _exportType     = result->exportType;
    _unsavedChanges = false;
    ClearSelection();
    _viewScale = 1.f; _viewOffsetX = _viewOffsetY = 0.f;

    _regionObstacles = std::move(result->obstacles);
    _regionTiles     = std::move(result->tiles);

    snprintf(_statusMsg, sizeof(_statusMsg), "Loaded: %s  (%dx%d, unit=%d)",
             fs::path(_currentMapPath).filename().string().c_str(),
             _mapWidth, _mapHeight, _unitSize);
    _statusTimer = 4.f;
}

// ── IsVisible: kiểm tra tile có nằm trong viewport canvas không ──
// Dùng _viewOffsetX/Y, _viewScale để tính screen rect của tile,
// so sánh với canvas size (lấy từ ImGui content region).
bool MapEditor::IsVisible(const PlacedTile& tile) const
{
    // Tính screen rect của tile (top-left sau khi áp pivot)
    float drawX = tile.x - tile.pivotX;
    float drawY = tile.y - tile.pivotY;

    // Screen position (không cộng canvasP0 vì chỉ cần cull tương đối)
    float sx = _viewOffsetX + drawX * _viewScale;
    float sy = _viewOffsetY + drawY * _viewScale;
    float sw = (tile.frameW > 0 ? tile.frameW : 64) * _viewScale;
    float sh = (tile.frameH > 0 ? tile.frameH : 64) * _viewScale;

    // Canvas size (lấy kích thước ImGui content region hiện tại)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float cw = avail.x > 0 ? avail.x : 800.f;
    float ch = avail.y > 0 ? avail.y : 600.f;

    // AABB overlap: tile rect [sx, sx+sw] x [sy, sy+sh] vs canvas [0, cw] x [0, ch]
    return (sx + sw > 0.f) && (sx < cw) &&
           (sy + sh > 0.f) && (sy < ch);
}

// ═══════════════════════════════════════════════════════════
// EnsureRegionArrays
// ═══════════════════════════════════════════════════════════
void MapEditor::EnsureRegionArrays(int rw, int rh)
{
    _regionObstacles.assign(rh, std::vector<std::vector<ObstacleEdit>>(rw));
    _regionTiles.assign(rh, std::vector<std::vector<std::vector<PlacedTile>>>(rw));
}

// ═══════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════
void MapEditor::MarkDirty()  { _unsavedChanges = true; }
void MapEditor::ClearSelection()
{
    _selectedRegionX = _selectedRegionY = -1;
    _selectedObstacleIndex = _selectedTileIdx = -1;
    _isDragging = _isPainting = false;
}

void MapEditor::SetMapProperties(int w, int h, int u)
{
    if (w <= 0 || h <= 0 || u <= 0) return;
    _mapWidth = w; _mapHeight = h; _unitSize = u;
    int rw = w / u, rh = h / u;
    _regionObstacles.resize(rh); for (auto& r : _regionObstacles) r.resize(rw);
    _regionTiles.resize(rh);     for (auto& r : _regionTiles)     r.resize(rw);
    MarkDirty();
}
void MapEditor::GetMapProperties(int& w, int& h, int& u) const
{ w = _mapWidth; h = _mapHeight; u = _unitSize; }

void MapEditor::SelectRegion(int rx, int ry)
{ _selectedRegionX = rx; _selectedRegionY = ry; _selectedObstacleIndex = -1; _selectedTileIdx = -1; }

void MapEditor::AddObstacleToRegion(float lx, float ly, float w, float h)
{
    if (_selectedRegionX < 0 || _selectedRegionY < 0) return;
    if (_selectedRegionY >= (int)_regionObstacles.size()) return;
    if (_selectedRegionX >= (int)_regionObstacles[_selectedRegionY].size()) return;
    ObstacleEdit ob; ob.x=lx; ob.y=ly; ob.w=w; ob.h=h;
    _regionObstacles[_selectedRegionY][_selectedRegionX].push_back(ob);
    MarkDirty();
}

void MapEditor::RemoveObstacleFromRegion(int idx)
{
    if (_selectedRegionX < 0 || _selectedRegionY < 0) return;
    auto& obs = _regionObstacles[_selectedRegionY][_selectedRegionX];
    if (idx < 0 || idx >= (int)obs.size()) return;
    obs.erase(obs.begin() + idx);
    if (_selectedObstacleIndex == idx)      _selectedObstacleIndex = -1;
    else if (_selectedObstacleIndex > idx)  _selectedObstacleIndex--;
    MarkDirty();
}

void MapEditor::ModifyObstacleInRegion(int idx, float lx, float ly, float w, float h)
{
    if (_selectedRegionX < 0 || _selectedRegionY < 0) return;
    auto& obs = _regionObstacles[_selectedRegionY][_selectedRegionX];
    if (idx < 0 || idx >= (int)obs.size()) return;
    obs[idx] = {lx, ly, w, h};
    MarkDirty();
}

// ═══════════════════════════════════════════════════════════
// PlaceTileAt / EraseTileAt
// ═══════════════════════════════════════════════════════════
void MapEditor::PlaceTileAt(float worldX, float worldY, int layer)
{
    if (!_brush.valid || _mapWidth == 0 || _unitSize == 0) return;
    int rx = (int)(worldX / _unitSize);
    int ry = (int)(worldY / _unitSize);
    if (rx < 0 || ry < 0) return;
    if (ry >= (int)_regionTiles.size() || rx >= (int)_regionTiles[ry].size()) return;
    auto& layers = _regionTiles[ry][rx];
    if (layer >= (int)layers.size()) layers.resize(layer + 1);

    PlacedTile tile;
    tile.sprPath  = _brush.sprPath;
    tile.useAnim  = _brush.useAnim;
    tile.animIdx  = _brush.animIdx;
    tile.frameIdx = _brush.frameIdx;
    tile.x        = worldX;
    tile.y        = worldY;
    tile.layer    = layer;
    tile.frameW   = _brush.frameW;
    tile.frameH   = _brush.frameH;
    tile.pivotX   = _brush.pivotX;
    tile.pivotY   = _brush.pivotY;
    tile.fps      = _brush.fps;
    tile.frameTex    = _brush.frameTex;   // shared GL ids (read-only)
    tile.totalFrames = _brush.totalFrames;
    tile.curFrame    = 0;
    tile.animTimer   = 0.f;
    tile.previewTexID = _brush.previewTex;
    tile.loaded      = true;
    layers[layer].push_back(std::move(tile));
    MarkDirty();
}

void MapEditor::EraseTileAt(float worldX, float worldY, int layer)
{
    if (_mapWidth == 0 || _unitSize == 0) return;
    int rx = (int)(worldX / _unitSize);
    int ry = (int)(worldY / _unitSize);
    if (rx < 0 || ry < 0) return;
    if (ry >= (int)_regionTiles.size() || rx >= (int)_regionTiles[ry].size()) return;
    auto& layers = _regionTiles[ry][rx];
    if (layer >= (int)layers.size()) return;
    auto& vec = layers[layer];
    for (int i = (int)vec.size() - 1; i >= 0; i--)
    {
        const auto& t = vec[i];
        float dx = worldX - t.x + t.pivotX;
        float dy = worldY - t.y + t.pivotY;
        if (dx >= 0 && dx < t.frameW && dy >= 0 && dy < t.frameH)
        {
            // Textures are shared with tileset – don't delete
            vec[i].frameTex.clear();
            vec[i].previewTexID = 0;
            vec.erase(vec.begin() + i);
            MarkDirty();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// LoadMap  — khởi động background thread để đọc region files
//            (không block GUI thread)
// ═══════════════════════════════════════════════════════════
bool MapEditor::LoadMap(const std::string& folderPath)
{
    // Nếu đang load rồi thì bỏ qua
    if (_isLoading.load()) return false;

    // Parse .map file ngay trên GUI thread (nhẹ, chỉ đọc 1 file text nhỏ)
    auto tmpMap = std::make_unique<Map>();
    if (!tmpMap->LoadMapFromFolder(folderPath))
    {
        snprintf(_statusMsg, sizeof(_statusMsg), "LOAD FAILED: cannot read .map file in %s", folderPath.c_str());
        _statusTimer = 5.f;
        return false;
    }

    int mapW    = tmpMap->GetWidth();
    int mapH    = tmpMap->GetHeight();
    int unitSz  = tmpMap->GetUnitSize();
    int expType = (tmpMap->GetMapType() == Map::MapType::S) ? 0 : 1;
    tmpMap.reset();

    int rw = (unitSz > 0) ? mapW / unitSz : 0;
    int rh = (unitSz > 0) ? mapH / unitSz : 0;

    // Free old tile textures trước khi replace
    for (auto& rowY : _regionTiles)
    for (auto& rowX : rowY)
    for (auto& layer : rowX)
    for (auto& tile : layer)
        FreeTilePreview(tile);
    _regionTiles.clear();
    _regionObstacles.clear();

    // Đánh dấu đang load
    _isLoading.store(true);
    _loadProgress.store(0.f);
    _pendingLoadPath = folderPath;

    // Lưu lại trước khi thread start
    std::string vfsRoot   = _vfsRoot;

    // Đợi load thread cũ (nếu có) kết thúc
    if (_loadThread.joinable()) _loadThread.join();

    // ── Background thread ────────────────────────────────────
    _loadThread = std::thread([this, folderPath, rw, rh, mapW, mapH, unitSz, expType, vfsRoot]()
    {
        auto result = std::make_unique<LoadResult>();
        result->folderPath  = folderPath;
        result->mapWidth    = mapW;
        result->mapHeight   = mapH;
        result->unitSize    = unitSz;
        result->exportType  = expType;
        result->success     = false;

        // Pre-allocate region arrays
        result->obstacles.assign(rh, std::vector<std::vector<ObstacleEdit>>(rw));
        result->tiles.assign(rh, std::vector<std::vector<std::vector<PlacedTile>>>(rw));

        int total = rw * rh;
        int done  = 0;

        // ── Helpers (lokale lambdas) ─────────────────────────
        auto readObs = [](std::ifstream& f,
                          std::vector<std::vector<std::vector<ObstacleEdit>>>& obstacles,
                          int y, int x)
        {
            int obsCount = 0;
            f.read((char*)&obsCount, sizeof(int));
            if (obsCount < 0 || obsCount > 100000) return;
            for (int i = 0; i < obsCount; i++)
            {
                ObstacleEdit ob;
                f.read((char*)&ob.x, sizeof(float));
                f.read((char*)&ob.y, sizeof(float));
                f.read((char*)&ob.w, sizeof(float));
                f.read((char*)&ob.h, sizeof(float));
                obstacles[y][x].push_back(ob);
            }
        };

        auto readLayers = [](std::ifstream& f,
                             std::vector<std::vector<std::vector<std::vector<PlacedTile>>>>& tiles,
                             int y, int x)
        {
            int layerCount = 0;
            f.read((char*)&layerCount, sizeof(int));
            if (layerCount <= 0 || layerCount > 64) return;
            if ((int)tiles[y][x].size() < layerCount)
                tiles[y][x].resize(layerCount);
            for (int l = 0; l < layerCount; l++)
            {
                int tileCount = 0;
                f.read((char*)&tileCount, sizeof(int));
                if (tileCount < 0 || tileCount > 500000) continue;
                for (int ti = 0; ti < tileCount; ti++)
                {
                    PlacedTile tile;
                    tile.layer = l;
                    int plen = 0;
                    f.read((char*)&plen, sizeof(int));
                    if (plen > 0 && plen < 1024) {
                        tile.sprPath.resize(plen);
                        f.read(&tile.sprPath[0], plen);
                    }
                    uint8_t ua = 0;
                    f.read((char*)&ua, 1);
                    tile.useAnim = (ua != 0);
                    f.read((char*)&tile.animIdx,  sizeof(int));
                    f.read((char*)&tile.frameIdx, sizeof(int));
                    f.read((char*)&tile.x,        sizeof(float));
                    f.read((char*)&tile.y,        sizeof(float));
                    tiles[y][x][l].push_back(std::move(tile));
                }
            }
        };

        // ── Read all regions — chỉ đọc region_C ─────────────
        // region_C (version 4) chứa đủ: obstacle table + tile layers.
        // region_S chỉ dùng bởi server, editor không cần đọc riêng.
        for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
        {
            char pathC[512];
            snprintf(pathC, sizeof(pathC), "%s/Region/%d/region_C_%d.dat", folderPath.c_str(), x, y);

            if (fs::exists(pathC))
            {
                std::ifstream f(pathC, std::ios::binary);
                if (f)
                {
                    char magic[14] = {};
                    f.read(magic, 13);
                    if (strncmp(magic, "REGION_DATA_C", 13) == 0)
                    {
                        int version = 0;
                        f.read((char*)&version, sizeof(int));

                        // version 4+: có obstacle table trước layers
                        if (version >= 4)
                            readObs(f, result->obstacles, y, x);

                        readLayers(f, result->tiles, y, x);
                    }
                }
            }

            ++done;
            if (total > 0)
                _loadProgress.store((float)done / (float)total);
        }

        result->success = true;
        printf("BG LOAD DONE: %s (%dx%d regions)\n", folderPath.c_str(), rw, rh);

        // Gửi kết quả về GUI thread
        {
            std::lock_guard<std::mutex> lk(_loadResultMutex);
            _pendingResult = std::move(result);
        }
        // _isLoading sẽ được clear bởi PollLoadResult() trên GUI thread
    });

    snprintf(_statusMsg, sizeof(_statusMsg), "Loading map: %s ...", fs::path(folderPath).filename().string().c_str());
    _statusTimer = 999.f; // sẽ bị reset khi load xong
    return true;
}

// ═══════════════════════════════════════════════════════════
// SaveMap / SaveMapTo / CreateNewMap
// ═══════════════════════════════════════════════════════════
bool MapEditor::SaveMap()
{
    if (_currentMapPath.empty()) { _dlgSaveAs = true; return false; }
    return SaveMapTo(_currentMapPath);
}


#define EXPORT_TYPE_S 0
#define EXPORT_TYPE_C 1
bool MapEditor::SaveMapTo(const std::string& folderPath)
{
    fs::create_directories(folderPath);
    std::string mapName = fs::path(folderPath).filename().string();
    std::string mapFile = folderPath + "/" + mapName + ".map";

    // _exportType: 0 = S, 1 = C
    const char* typeStr = (_exportType == 0) ? "S" : "C";

    {
        std::ofstream f(mapFile);
        if (!f) {
            snprintf(_statusMsg, sizeof(_statusMsg), "SAVE FAILED: %s", mapFile.c_str());
            _statusTimer = 5.f;
            return false;
        }
        f << "[MAIN]\n"
          << "type=" << typeStr << "\n"
          << "w=" << _mapWidth << "\n"
          << "h=" << _mapHeight << "\n"
          << "UnitSize=" << _unitSize << "\n";
    }

    int rw = (_unitSize > 0) ? _mapWidth  / _unitSize : 0;
    int rh = (_unitSize > 0) ? _mapHeight / _unitSize : 0;

    for (int y = 0; y < rh; y++)
    for (int x = 0; x < rw; x++)
    {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/Region/%d", folderPath.c_str(), x);
        fs::create_directories(dir);

        const auto& obsVec = (y < (int)_regionObstacles.size() && x < (int)_regionObstacles[y].size())
                           ? _regionObstacles[y][x] : std::vector<ObstacleEdit>{};
        int obsCount = (int)obsVec.size();

        // ── Always write region_S (obstacle data cho server) ─
        // Dù export type là S hay C, server đều cần region_S để check collision.
        if(_exportType == EXPORT_TYPE_S){
            char pathS[512];
            snprintf(pathS, sizeof(pathS), "%s/region_S_%d.dat", dir, y);
            std::ofstream f(pathS, std::ios::binary);
            if (f)
            {
                f.write("REGION_DATA_S", 13);
                int version = 1;
                f.write((char*)&version, sizeof(int));
                f.write((char*)&obsCount, sizeof(int));
                for (const auto& ob : obsVec)
                {
                    f.write((char*)&ob.x, sizeof(float));
                    f.write((char*)&ob.y, sizeof(float));
                    f.write((char*)&ob.w, sizeof(float));
                    f.write((char*)&ob.h, sizeof(float));
                }
            }
        }

        // ── Write region_C only when type=C ──────────────────
        if (_exportType == EXPORT_TYPE_C)
        {
            char pathC[512];
            snprintf(pathC, sizeof(pathC), "%s/region_C_%d.dat", dir, y);
            std::ofstream f(pathC, std::ios::binary);
            if (f)
            {
                f.write("REGION_DATA_C", 13);
                int version = 4;
                f.write((char*)&version, sizeof(int));

                // Obstacle table (version 4 includes obstacles in C as well)
                f.write((char*)&obsCount, sizeof(int));
                for (const auto& ob : obsVec)
                {
                    f.write((char*)&ob.x, sizeof(float));
                    f.write((char*)&ob.y, sizeof(float));
                    f.write((char*)&ob.w, sizeof(float));
                    f.write((char*)&ob.h, sizeof(float));
                }

                // Tile layers
                int layerCount = (y < (int)_regionTiles.size() && x < (int)_regionTiles[y].size())
                               ? (int)_regionTiles[y][x].size() : 0;
                f.write((char*)&layerCount, sizeof(int));
                for (int l = 0; l < layerCount; l++)
                {
                    const auto& tiles = _regionTiles[y][x][l];
                    int tileCount = (int)tiles.size();
                    f.write((char*)&tileCount, sizeof(int));
                    for (const auto& tile : tiles)
                    {
                        // ── Chuyển absolute → relative VFS path ──────────
                        // sprPath gốc có thể là absolute (D:\...\titleset\abc.spr)
                        // Nếu _vfsRoot được đặt, tính relative từ đó.
                        // Kết quả: "titleset/abc.spr" → client tìm ở res/paks/res/titleset/abc.spr
                        std::string savePath = tile.sprPath;
                        if (!_vfsRoot.empty() && !savePath.empty())
                        {
                            try {
                                fs::path absPath(savePath);
                                fs::path rootPath(_vfsRoot);
                                if (absPath.is_absolute())
                                {
                                    fs::path rel = fs::relative(absPath, rootPath);
                                    // Chỉ dùng nếu relative hợp lệ (không bắt đầu bằng "..")
                                    std::string relStr = rel.generic_string();
                                    if (!relStr.empty() && relStr[0] != '.')
                                        savePath = relStr;
                                }
                            } catch (...) { /* giữ nguyên nếu lỗi */ }
                        }

                        int plen = (int)savePath.size();
                        f.write((char*)&plen, sizeof(int));
                        f.write(savePath.data(), plen);
                        uint8_t ua = tile.useAnim ? 1 : 0;
                        f.write((char*)&ua, 1);
                        f.write((char*)&tile.animIdx,  sizeof(int));
                        f.write((char*)&tile.frameIdx, sizeof(int));
                        f.write((char*)&tile.x,        sizeof(float));
                        f.write((char*)&tile.y,        sizeof(float));
                    }
                }
            }
        }
    }

    _currentMapPath = folderPath;
    _unsavedChanges = false;
    snprintf(_statusMsg, sizeof(_statusMsg), "Saved [type=%s]: %s", typeStr, mapFile.c_str());
    _statusTimer = 4.f;
    return true;
}

bool MapEditor::CreateNewMap(const std::string& folderPath, int width, int height, int unitSize)
{
    fs::create_directories(folderPath);
    _map.reset();
    _mapWidth = width; _mapHeight = height; _unitSize = unitSize;
    _currentMapPath = folderPath;
    _unsavedChanges = true;
    ClearSelection();
    _viewScale = 1.f; _viewOffsetX = _viewOffsetY = 0.f;
    int rw = (unitSize > 0) ? width  / unitSize : 0;
    int rh = (unitSize > 0) ? height / unitSize : 0;
    EnsureRegionArrays(rw, rh);
    snprintf(_statusMsg, sizeof(_statusMsg), "New map: %dx%d unit=%d", width, height, unitSize);
    _statusTimer = 4.f;
    return true;
}

// ═══════════════════════════════════════════════════════════
// Public stubs
// ═══════════════════════════════════════════════════════════
void MapEditor::RenderRegionEditor()  {}
void MapEditor::RenderObstacleEditor(){}

// ═══════════════════════════════════════════════════════════
// RenderEditor  (main entry)
// ═══════════════════════════════════════════════════════════
void MapEditor::RenderEditor()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (_statusTimer > 0.f) _statusTimer -= dt;

    // ── Poll background map load result ─────────────────────
    PollLoadResult();

    TickPreviews(dt);

    ImGui::SetNextWindowSize(ImVec2(1400, 760), ImGuiCond_Once);
    ImGuiWindowFlags wf = ImGuiWindowFlags_MenuBar
                        | ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Map Editor", nullptr, wf)) { ImGui::End(); return; }

    RenderMenuBar();
    RenderToolbar();
    RenderDialogs();

    float totalW = ImGui::GetContentRegionAvail().x;
    float tsW    = 250.f;
    float rightW = 270.f;
    float canvasW = totalW - tsW - rightW - 8.f;
    float panelH  = ImGui::GetContentRegionAvail().y - 28.f; // leave room for status

    ImGui::BeginChild("##TS", ImVec2(tsW, panelH), true);
    RenderTilesetPanel();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##Canvas", ImVec2(canvasW, panelH), true);
    RenderCanvasPanel();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##Right", ImVec2(rightW, panelH), false);
    {
        float rh2 = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##ObsP",   ImVec2(0, rh2 * 0.5f), true);
        RenderObstaclePanel();
        ImGui::EndChild();
        ImGui::BeginChild("##LayerP", ImVec2(0, 0), true);
        RenderLayerPanel();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    RenderStatusBar();
    ImGui::End();
}

// ─────────────────────────────────────────────────────────
// RenderMenuBar
// ─────────────────────────────────────────────────────────
void MapEditor::RenderMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Map..."))  _dlgNewMap  = true;
        if (ImGui::MenuItem("Load Map...")) _dlgLoadMap = true;
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !_currentMapPath.empty())) SaveMap();
        if (ImGui::MenuItem("Save As..."))  _dlgSaveAs  = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tileset"))
    {
        if (ImGui::MenuItem("Add Tileset Folder...")) _dlgAddTilesetFolder = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Select",         nullptr, _editMode==EditMode::Select))        _editMode=EditMode::Select;
        if (ImGui::MenuItem("Paint Tile",     nullptr, _editMode==EditMode::PaintTile))     _editMode=EditMode::PaintTile;
        if (ImGui::MenuItem("Add Obstacle",   nullptr, _editMode==EditMode::AddObstacle))   _editMode=EditMode::AddObstacle;
        if (ImGui::MenuItem("Erase Obstacle", nullptr, _editMode==EditMode::EraseObstacle)) _editMode=EditMode::EraseObstacle;
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected Obstacle", nullptr, false, _selectedObstacleIndex>=0 && _selectedRegionX>=0))
            RemoveObstacleFromRegion(_selectedObstacleIndex);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Show Grid",      nullptr, &_showGrid);
        ImGui::MenuItem("Show Obstacles", nullptr, &_showObstacles);
        ImGui::MenuItem("Show Tiles",     nullptr, &_showTiles);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// ─────────────────────────────────────────────────────────
// RenderToolbar
// ─────────────────────────────────────────────────────────
void MapEditor::RenderToolbar()
{
    auto ModeBtn = [&](const char* lbl, EditMode m) {
        bool act = (_editMode == m);
        if (act) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.2f,.6f,.9f,1.f));
        if (ImGui::Button(lbl)) _editMode = m;
        if (act) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    if (ImGui::Button("New##tb"))  _dlgNewMap  = true; ImGui::SameLine();
    if (ImGui::Button("Load##tb")) _dlgLoadMap = true; ImGui::SameLine();
    if (!_currentMapPath.empty()) {
        if (ImGui::Button("Save##tb")) SaveMap();
    } else {
        ImGui::BeginDisabled(); ImGui::Button("Save##tb"); ImGui::EndDisabled();
    }
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ModeBtn("Select##tb",   EditMode::Select);
    ModeBtn("Paint##tb",    EditMode::PaintTile);
    ModeBtn("Obstacle##tb", EditMode::AddObstacle);
    ModeBtn("Erase##tb",    EditMode::EraseObstacle);
    ModeBtn("PaintMultiTile##tb",     EditMode::PaintMultiTile);
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::SetNextItemWidth(50);
    ImGui::InputInt("Snap##tb", &_obstacleSnapSize); if (_obstacleSnapSize<1) _obstacleSnapSize=1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Zoom##tb", &_viewScale, 0.05f, 8.f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(40);
    ImGui::InputInt("Layer##tb", &_paintLayer); if (_paintLayer<0) _paintLayer=0;
    if (_unsavedChanges) { ImGui::SameLine(0,20); ImGui::TextColored(ImVec4(1,.8f,.2f,1),"[unsaved]"); }
    if (_brush.valid && _brush.previewTex) {
        ImGui::SameLine(0,20); ImGui::Text("Brush:");
        ImGui::SameLine();
        ImGui::Image((ImTextureID)(uintptr_t)_brush.previewTex, ImVec2(22,22));
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s", fs::path(_brush.sprPath).filename().string().c_str(),
                            _brush.useAnim?"(anim)":"(frame)");
    }
    ImGui::Separator();
}

// ─────────────────────────────────────────────────────────
// RenderTilesetPanel  (cột trái)
// ─────────────────────────────────────────────────────────
void MapEditor::RenderTilesetPanel()
{
    ImGui::Text("Tilesets");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addts")) _dlgAddTilesetFolder = true;
    ImGui::Separator();

    for (int fi = 0; fi < (int)_tilesetFolders.size(); fi++)
    {
        TilesetFolder& folder = _tilesetFolders[fi];
        ImGui::PushID(fi);

        bool open = ImGui::TreeNodeEx(folder.name.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);

        if (ImGui::BeginPopupContextItem("##fctx"))
        {
            if (ImGui::MenuItem("Reload")) {
                for (auto& s : folder.sprs) { FreeTilesetSpr(s); }
            }
            if (ImGui::MenuItem("Remove Folder")) {
                RemoveTilesetFolder(fi);
                ImGui::EndPopup(); ImGui::PopID();
                if (open) ImGui::TreePop();
                break;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            for (int si = 0; si < (int)folder.sprs.size(); si++)
            {
                TilesetSprEntry& spr = folder.sprs[si];
                ImGui::PushID(si);

                float thumbSz = 36.f;
                // Lazy load when tree item is visible
                if (!spr.loaded) LoadTilesetSpr(spr);

                unsigned int thumbTex = 0;
                if (!spr.frameTex.empty())
                    thumbTex = spr.frameTex[spr.curFrame % (int)spr.frameTex.size()];

                ImVec2 cur = ImGui::GetCursorScreenPos();
                if (thumbTex)
                    ImGui::Image((ImTextureID)(uintptr_t)thumbTex, ImVec2(thumbSz,thumbSz));
                else {
                    ImGui::Dummy(ImVec2(thumbSz,thumbSz));
                    ImGui::GetWindowDrawList()->AddRect(cur, {cur.x+thumbSz,cur.y+thumbSz}, IM_COL32(80,80,80,200));
                }
                ImGui::SameLine();

                bool sprOpen = ImGui::TreeNodeEx(spr.name.c_str(),
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s", spr.name.c_str());

                if (sprOpen)
                {
                    // ── Animations ─────────────────────────
                    if (!spr.anims.empty())
                    {
                        ImGui::TextColored(ImVec4(.5f,1.f,.5f,1.f),"Animations:");
                        for (int ai = 0; ai < (int)spr.anims.size(); ai++)
                        {
                            ImGui::PushID(ai);
                            const auto& anim = spr.anims[ai];
                            unsigned int animThumb = (anim.startFrame < (int)spr.frameTex.size())
                                ? spr.frameTex[anim.startFrame] : 0u;
                            if (animThumb)
                                ImGui::Image((ImTextureID)(uintptr_t)animThumb, ImVec2(26,26));
                            else
                                ImGui::Dummy(ImVec2(26,26));
                            ImGui::SameLine();
                            bool sel = (_brush.valid && _brush.sprPath==spr.path
                                        && _brush.useAnim && _brush.animIdx==ai);
                            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f,.85f,.2f,1.f));
                            char lbl[128];
                            snprintf(lbl,sizeof(lbl),"[Anim] %s (%dfr %dfps)##as",
                                     anim.name.c_str(), anim.frameCount, anim.fps);
                            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0,26)))
                                SetBrushFromTileset(spr, true, ai);
                            if (sel) ImGui::PopStyleColor();
                            ImGui::PopID();
                        }
                    }

                    // ── Individual frames ──────────────────
                    if (!spr.frameTex.empty())
                    {
                        ImGui::TextColored(ImVec4(.7f,.85f,1.f,1.f),"Frames (%d):", spr.totalFrames);
                        // Show frames in a grid-like flow (4 per row)
                        int perRow = 4;
                        for (int fri = 0; fri < (int)spr.frameTex.size(); fri++)
                        {
                            ImGui::PushID(2000+fri);
                            bool sel = (_brush.valid && _brush.sprPath==spr.path
                                        && !_brush.useAnim && _brush.frameIdx==fri);
                            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.9f,.6f,.1f,1.f));

                            unsigned int fTex = spr.frameTex[fri];
                            char flbl[32]; snprintf(flbl,sizeof(flbl),"##fr%d",fri);
                            if (fTex) {
                                if (ImGui::ImageButton(flbl,(ImTextureID)(uintptr_t)fTex,ImVec2(26,26)))
                                    SetBrushFromTileset(spr, false, fri);
                            } else {
                                if (ImGui::Button(flbl, ImVec2(26,26)))
                                    SetBrushFromTileset(spr, false, fri);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Frame %d", fri);
                            if (sel) ImGui::PopStyleColor();
                            if ((fri+1) % perRow != 0) ImGui::SameLine();
                            ImGui::PopID();
                        }
                        ImGui::NewLine();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (_tilesetFolders.empty())
        ImGui::TextDisabled("(no tilesets)\nClick + to add folder\nwith .spr files");
}


void MapEditor::PlaceTileBlock(float wx, float wy, int layer, int bw, int bh)
{
    float snap = (float)_unitSize;

    float x0 = std::floor(wx / snap) * snap;
    float y0 = std::floor(wy / snap) * snap;
    float x1 = std::ceil ((wx + bw * _unitSize) / snap) * snap;
    float y1 = std::ceil ((wy + bh * _unitSize) / snap) * snap;

    x0 -= _brush.pivotX;
    y0 -= _brush.pivotY;

    for (float y = y0; y < y1; y += _unitSize)
    for (float x = x0; x < x1; x += _unitSize)
    {
        PlaceTileAt(x, y, layer);
    }
}

void MapEditor::EraseTileBlock(float wx, float wy, int layer, int bw, int bh)
{
    if (_unitSize <= 0) return;

    int baseX = (int)(wx / _unitSize);
    int baseY = (int)(wy / _unitSize);

    for (int y = 0; y < bh; y++)
    for (int x = 0; x < bw; x++)
    {
        float px = (baseX + x) * _unitSize;
        float py = (baseY + y) * _unitSize;

        EraseTileAt(px, py, layer);
    }
}

// ─────────────────────────────────────────────────────────
// RenderCanvasPanel
// ─────────────────────────────────────────────────────────
void MapEditor::RenderCanvasPanel()
{
    if (_mapWidth == 0 || _mapHeight == 0)
    {
        ImGui::TextDisabled("No map loaded.\nFile > New Map or File > Load Map");
        return;
    }

    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 avail    = ImGui::GetContentRegionAvail();
    if (avail.x < 16) avail.x = 16;
    if (avail.y < 16) avail.y = 16;
    ImVec2 canvasP1 = { canvasP0.x + avail.x, canvasP0.y + avail.y };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasP0, canvasP1, IM_COL32(30,30,30,255));

    // Invisible button to capture input
    ImGui::InvisibleButton("##canvas", avail,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    ImGuiIO& io  = ImGui::GetIO();

    // ── Pan with middle mouse or RMB ─────────────────────────
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f))
    {
        _viewOffsetX += io.MouseDelta.x;
        _viewOffsetY += io.MouseDelta.y;
    }
    if (hovered && io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f))
    {
        _viewOffsetX += io.MouseDelta.x;
        _viewOffsetY += io.MouseDelta.y;
    }

    // Zoom with mouse wheel
    if (hovered && io.MouseWheel != 0.f)
    {
        float oldScale = _viewScale;
        float factor   = (io.MouseWheel > 0) ? 1.15f : (1.f / 1.15f);
        _viewScale     = std::max(0.05f, std::min(8.f, _viewScale * factor));
        // Zoom towards mouse cursor
        float mx = io.MousePos.x - canvasP0.x;
        float my = io.MousePos.y - canvasP0.y;
        _viewOffsetX = mx - (_viewScale / oldScale) * (mx - _viewOffsetX);
        _viewOffsetY = my - (_viewScale / oldScale) * (my - _viewOffsetY);
    }

    // ── World<->Screen helpers ───────────────────────────────
    auto W2S = [&](float wx, float wy) -> ImVec2 {
        return { canvasP0.x + _viewOffsetX + wx * _viewScale,
                 canvasP0.y + _viewOffsetY + wy * _viewScale };
    };
    auto S2W = [&](float sx, float sy) -> ImVec2 {
        return { (sx - canvasP0.x - _viewOffsetX) / _viewScale,
                 (sy - canvasP0.y - _viewOffsetY) / _viewScale };
    };

    dl->PushClipRect(canvasP0, canvasP1, true);

    // ── Draw tiles ───────────────────────────────────────────
    // PollPendingUploads: nhận kết quả từ worker threads → upload GL texture
    PollPendingUploads();

    if (_showTiles)
    {
        // Viewport bounds trong world-space (tính 1 lần, dùng cho cả cull + queue)
        float vpX0 = -_viewOffsetX / _viewScale;
        float vpY0 = -_viewOffsetY / _viewScale;
        float vpX1 = vpX0 + avail.x / _viewScale;
        float vpY1 = vpY0 + avail.y / _viewScale;
        // Margin nhỏ để tránh pop-in khi scroll nhanh
        float margin = 64.f;
        float vpX0m = vpX0 - margin, vpY0m = vpY0 - margin;
        float vpX1m = vpX1 + margin, vpY1m = vpY1 + margin;

        int rw = _unitSize > 0 ? _mapWidth  / _unitSize : 0;
        int rh = _unitSize > 0 ? _mapHeight / _unitSize : 0;

        // Throttle: chỉ queue tối đa N tiles mới mỗi frame để tránh flood queue
        // Worker threads sẽ xử lý dần dần, mỗi frame upload thêm
        int queueBudget = 16; // tiles/frame tối đa được queue

        for (int ry = 0; ry < rh; ry++)
        for (int rx = 0; rx < rw; rx++)
        {
            if (ry >= (int)_regionTiles.size() || rx >= (int)_regionTiles[ry].size()) continue;

            // Region AABB culling (nhanh hơn tile-by-tile khi region trống)
            float regX0 = (float)(rx * _unitSize);
            float regY0 = (float)(ry * _unitSize);
            float regX1 = regX0 + _unitSize;
            float regY1 = regY0 + _unitSize;
            if (regX1 < vpX0m || regX0 > vpX1m || regY1 < vpY0m || regY0 > vpY1m)
                continue; // toàn bộ region nằm ngoài viewport

            for (std::vector<PlacedTile>& layer : _regionTiles[ry][rx])
            for (PlacedTile& tile : layer)
            {
                // World-space AABB của tile
                float tx0 = tile.x - tile.pivotX;
                float ty0 = tile.y - tile.pivotY;
                float tw  = tile.frameW > 0 ? (float)tile.frameW : 64.f;
                float th  = tile.frameH > 0 ? (float)tile.frameH : 64.f;
                float tx1 = tx0 + tw;
                float ty1 = ty0 + th;

                // Cull: tile nằm ngoài viewport+margin → skip render (KHÔNG free texture)
                if (tx1 < vpX0m || tx0 > vpX1m || ty1 < vpY0m || ty0 > vpY1m)
                    continue;

                // Queue load nếu chưa có texture, nhưng throttle
                if (tile.loadState == 0 && queueBudget > 0)
                {
                    QueueTileLoad(tile);
                    --queueBudget;
                }

                // Chỉ render nếu có texture hợp lệ VÀ tile nằm trong viewport thực
                if (!tile.previewTexID) continue;
                if (tx1 < vpX0 || tx0 > vpX1 || ty1 < vpY0 || ty0 > vpY1) continue;

                ImVec2 sp = W2S(tx0, ty0);
                ImVec2 ep = { sp.x + tw * _viewScale, sp.y + th * _viewScale };
                dl->AddImage((ImTextureID)(uintptr_t)tile.previewTexID, sp, ep);
            }
        }
    }

    // ── Draw grid ────────────────────────────────────────────
    if (_showGrid)
    {
        // Region grid
        int rw = _unitSize > 0 ? _mapWidth  / _unitSize : 0;
        int rh = _unitSize > 0 ? _mapHeight / _unitSize : 0;
        for (int rx = 0; rx <= rw; rx++)
        {
            ImVec2 a = W2S((float)(rx * _unitSize), 0.f);
            ImVec2 b = W2S((float)(rx * _unitSize), (float)_mapHeight);
            dl->AddLine(a, b, IM_COL32(60,60,60,200), 1.f);
        }
        for (int ry = 0; ry <= rh; ry++)
        {
            ImVec2 a = W2S(0.f,          (float)(ry * _unitSize));
            ImVec2 b = W2S((float)_mapWidth, (float)(ry * _unitSize));
            dl->AddLine(a, b, IM_COL32(60,60,60,200), 1.f);
        }
        // Map border
        ImVec2 mA = W2S(0.f, 0.f);
        ImVec2 mB = W2S((float)_mapWidth, (float)_mapHeight);
        dl->AddRect(mA, mB, IM_COL32(200,200,100,200), 0.f, 0, 2.f);
    }

    // ── Draw obstacles ───────────────────────────────────────
    if (_showObstacles)
    {
        int rw = _unitSize > 0 ? _mapWidth  / _unitSize : 0;
        int rh = _unitSize > 0 ? _mapHeight / _unitSize : 0;
        for (int ry = 0; ry < rh; ry++)
        for (int rx = 0; rx < rw; rx++)
        {
            if (ry >= (int)_regionObstacles.size() || rx >= (int)_regionObstacles[ry].size()) continue;
            float owx = (float)(rx * _unitSize);
            float owy = (float)(ry * _unitSize);
            for (int oi = 0; oi < (int)_regionObstacles[ry][rx].size(); oi++)
            {
                const auto& ob = _regionObstacles[ry][rx][oi];
                ImVec2 p0 = W2S(owx + ob.x, owy + ob.y);
                ImVec2 p1 = W2S(owx + ob.x + ob.w, owy + ob.y + ob.h);
                bool isSel = (rx == _selectedRegionX && ry == _selectedRegionY && oi == _selectedObstacleIndex);
                ImU32 col  = isSel ? IM_COL32(255,80,80,180) : IM_COL32(255,120,0,120);
                dl->AddRectFilled(p0, p1, col);
                dl->AddRect(p0, p1, isSel ? IM_COL32(255,255,0,255) : IM_COL32(255,150,50,200), 0.f, 0, isSel?2.f:1.f);
            }
        }
    }

    // ── Selected region highlight ────────────────────────────
    if (_selectedRegionX >= 0 && _selectedRegionY >= 0)
    {
        float ox = (float)(_selectedRegionX * _unitSize);
        float oy = (float)(_selectedRegionY * _unitSize);
        ImVec2 p0 = W2S(ox, oy);
        ImVec2 p1 = W2S(ox + _unitSize, oy + _unitSize);
        dl->AddRect(p0, p1, IM_COL32(0,200,255,200), 0.f, 0, 2.f);
    }

    // ── Obstacle drag preview ────────────────────────────────
    if (_isDragging && (_editMode == EditMode::AddObstacle))
    {
        ImVec2 mouseWS = S2W(io.MousePos.x, io.MousePos.y);
        float snap = (float)_obstacleSnapSize;
        float x0 = std::floor(std::min(_dragStartWX, mouseWS.x) / snap) * snap;
        float y0 = std::floor(std::min(_dragStartWY, mouseWS.y) / snap) * snap;
        float x1 = std::ceil (std::max(_dragStartWX, mouseWS.x) / snap) * snap;
        float y1 = std::ceil (std::max(_dragStartWY, mouseWS.y) / snap) * snap;
        if (x1 <= x0) x1 = x0 + snap;
        if (y1 <= y0) y1 = y0 + snap;
        ImVec2 p0 = W2S(x0, y0);
        ImVec2 p1 = W2S(x1, y1);
        dl->AddRectFilled(p0, p1, IM_COL32(255,100,0,80));
        dl->AddRect(p0, p1, IM_COL32(255,200,0,220), 0.f, 0, 2.f);
    }

    // ── Brush cursor preview ─────────────────────────────────
    if (hovered && _editMode == EditMode::PaintTile && _brush.valid && _brush.previewTex)
    {
        ImVec2 mouseWS = S2W(io.MousePos.x, io.MousePos.y);
        ImVec2 sp = W2S(mouseWS.x - _brush.pivotX, mouseWS.y - _brush.pivotY);
        ImVec2 ep = { sp.x + _brush.frameW * _viewScale, sp.y + _brush.frameH * _viewScale };
        dl->AddImage((ImTextureID)(uintptr_t)_brush.previewTex, sp, ep, {0,0},{1,1}, IM_COL32(255,255,255,160));
        dl->AddRect(sp, ep, IM_COL32(255,255,0,180));
    }

    if (_isDragging && _editMode == EditMode::PaintMultiTile && _brush.valid)
    {
        ImVec2 mouseWS = S2W(io.MousePos.x, io.MousePos.y);

        float snap = (float)_unitSize;

        float x0 = std::floor(std::min(_dragStartWX, mouseWS.x) / snap) * snap;
        float y0 = std::floor(std::min(_dragStartWY, mouseWS.y) / snap) * snap;
        float x1 = std::ceil (std::max(_dragStartWX, mouseWS.x) / snap) * snap;
        float y1 = std::ceil (std::max(_dragStartWY, mouseWS.y) / snap) * snap;

        // 🔥 trừ pivot (giống preview cũ)
        x0 -= _brush.pivotX;
        y0 -= _brush.pivotY;
        x1 -= _brush.pivotX;
        y1 -= _brush.pivotY;

        for (float y = y0; y < y1; y += _unitSize)
        for (float x = x0; x < x1; x += _unitSize)
        {
            ImVec2 sp = W2S(x, y);
            ImVec2 ep = {
                sp.x + _brush.frameW * _viewScale,
                sp.y + _brush.frameH * _viewScale
            };

            dl->AddImage((ImTextureID)(uintptr_t)_brush.previewTex,
                sp, ep, {0,0},{1,1}, IM_COL32(255,255,255,80));
        }

        // viền ngoài
        dl->AddRect(W2S(x0,y0), W2S(x1,y1), IM_COL32(255,255,0,200));
    }

    dl->PopClipRect();

    // ── Mouse input handling ─────────────────────────────────
    if (hovered || active)
    {
        ImVec2 mouseWS = S2W(io.MousePos.x, io.MousePos.y);

        // ── Paint mode ─────────────────────────────────────
        if (_editMode == EditMode::PaintTile)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                PlaceTileAt(mouseWS.x, mouseWS.y, _paintLayer);
                _isPainting = true;
            }
            else _isPainting = false;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                EraseTileAt(mouseWS.x, mouseWS.y, _paintLayer);
        }
        else if (_editMode == EditMode::PaintMultiTile)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                _dragStartWX = mouseWS.x;
                _dragStartWY = mouseWS.y;
                _isDragging  = true;
            }
            if (_isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                PlaceTileBlock(_dragStartWX, _dragStartWY, _paintLayer,
                               (int)((mouseWS.x - _dragStartWX) / _unitSize) + 1,
                               (int)((mouseWS.y - _dragStartWY) / _unitSize) + 1);
                _isDragging = false;
            }
        }
        // ── Add Obstacle mode ──────────────────────────────
        else if (_editMode == EditMode::AddObstacle)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                _dragStartWX = mouseWS.x;
                _dragStartWY = mouseWS.y;
                _isDragging  = true;
                // select region under cursor
                if (_unitSize > 0)
                {
                    int rx = (int)(mouseWS.x / _unitSize);
                    int ry = (int)(mouseWS.y / _unitSize);
                    int rw = _mapWidth  / _unitSize;
                    int rh = _mapHeight / _unitSize;
                    if (rx >= 0 && ry >= 0 && rx < rw && ry < rh)
                        SelectRegion(rx, ry);
                }
            }
            if (_isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                float snap = (float)_obstacleSnapSize;
                float x0 = std::floor(std::min(_dragStartWX, mouseWS.x) / snap) * snap;
                float y0 = std::floor(std::min(_dragStartWY, mouseWS.y) / snap) * snap;
                float x1 = std::ceil (std::max(_dragStartWX, mouseWS.x) / snap) * snap;
                float y1 = std::ceil (std::max(_dragStartWY, mouseWS.y) / snap) * snap;
                if (x1 <= x0) x1 = x0 + snap;
                if (y1 <= y0) y1 = y0 + snap;
                // convert to region-local coords
                if (_selectedRegionX >= 0 && _selectedRegionY >= 0)
                {
                    float owx = (float)(_selectedRegionX * _unitSize);
                    float owy = (float)(_selectedRegionY * _unitSize);
                    AddObstacleToRegion(x0 - owx, y0 - owy, x1 - x0, y1 - y0);
                }
                _isDragging = false;
            }
        }
        // ── Erase Obstacle mode ────────────────────────────
        else if (_editMode == EditMode::EraseObstacle)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && _unitSize > 0)
            {
                int rx = (int)(mouseWS.x / _unitSize);
                int ry = (int)(mouseWS.y / _unitSize);
                int rw = _mapWidth  / _unitSize;
                int rh = _mapHeight / _unitSize;
                if (rx >= 0 && ry >= 0 && rx < rw && ry < rh
                    && ry < (int)_regionObstacles.size()
                    && rx < (int)_regionObstacles[ry].size())
                {
                    float owx = (float)(rx * _unitSize);
                    float owy = (float)(ry * _unitSize);
                    auto& obsVec = _regionObstacles[ry][rx];
                    for (int i = (int)obsVec.size()-1; i >= 0; i--)
                    {
                        const auto& ob = obsVec[i];
                        float lx = mouseWS.x - owx, ly = mouseWS.y - owy;
                        if (lx >= ob.x && lx <= ob.x+ob.w && ly >= ob.y && ly <= ob.y+ob.h)
                        {
                            SelectRegion(rx, ry);
                            _selectedObstacleIndex = i;
                            RemoveObstacleFromRegion(i);
                            break;
                        }
                    }
                }
            }
        }
        // ── Select mode ────────────────────────────────────
        else if (_editMode == EditMode::Select)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && _unitSize > 0)
            {
                int rx = (int)(mouseWS.x / _unitSize);
                int ry = (int)(mouseWS.y / _unitSize);
                int rw = _mapWidth  / _unitSize;
                int rh = _mapHeight / _unitSize;
                if (rx >= 0 && ry >= 0 && rx < rw && ry < rh)
                    SelectRegion(rx, ry);
                else
                    ClearSelection();
            }
        }
    }

    // Coord info
    if (hovered)
    {
        ImVec2 ws = S2W(io.MousePos.x, io.MousePos.y);
        ImGui::SetTooltip("World (%.0f, %.0f)  Region (%d, %d)",
                          ws.x, ws.y,
                          (_unitSize>0?(int)(ws.x/_unitSize):-1),
                          (_unitSize>0?(int)(ws.y/_unitSize):-1));
    }
}

// ─────────────────────────────────────────────────────────
// RenderObstaclePanel
// ─────────────────────────────────────────────────────────
void MapEditor::RenderObstaclePanel()
{
    ImGui::Text("Obstacles");
    ImGui::Separator();
    if (_selectedRegionX < 0 || _selectedRegionY < 0)
    {
        ImGui::TextDisabled("Select a region on canvas");
        return;
    }
    ImGui::TextColored(ImVec4(0.5f,1.f,0.5f,1.f),"Region (%d,%d)", _selectedRegionX, _selectedRegionY);
    auto& obsVec = _regionObstacles[_selectedRegionY][_selectedRegionX];
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addob"))
    {
        // add a default 32x32 obstacle at region origin
        AddObstacleToRegion(0.f, 0.f, 32.f, 32.f);
        _selectedObstacleIndex = (int)obsVec.size()-1;
    }

    for (int i = 0; i < (int)obsVec.size(); i++)
    {
        ImGui::PushID(i);
        bool sel = (i == _selectedObstacleIndex);
        char lbl[64]; snprintf(lbl, sizeof(lbl), "[%d] (%.0f,%.0f) %.0fx%.0f##ob", i, obsVec[i].x, obsVec[i].y, obsVec[i].w, obsVec[i].h);
        if (ImGui::Selectable(lbl, sel))
            _selectedObstacleIndex = i;

        if (sel)
        {
            float tmpX = obsVec[i].x, tmpY = obsVec[i].y;
            float tmpW = obsVec[i].w, tmpH = obsVec[i].h;
            bool changed = false;
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputFloat("X##ox", &tmpX, 1.f, 8.f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputFloat("Y##oy", &tmpY, 1.f, 8.f, "%.0f");
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputFloat("W##ow", &tmpW, 1.f, 8.f, "%.0f"); if (tmpW < 1.f) tmpW = 1.f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100); changed |= ImGui::InputFloat("H##oh", &tmpH, 1.f, 8.f, "%.0f"); if (tmpH < 1.f) tmpH = 1.f;
            if (changed) ModifyObstacleInRegion(i, tmpX, tmpY, tmpW, tmpH);
            if (ImGui::SmallButton("Delete##del")) { RemoveObstacleFromRegion(i); ImGui::PopID(); break; }
        }
        ImGui::PopID();
    }
    if (obsVec.empty()) ImGui::TextDisabled("(none)");
}

// ─────────────────────────────────────────────────────────
// RenderLayerPanel
// ─────────────────────────────────────────────────────────
void MapEditor::RenderLayerPanel()
{
    ImGui::Text("Layers / Tiles");
    ImGui::Separator();

    ImGui::SetNextItemWidth(60);
    ImGui::InputInt("Paint Layer", &_paintLayer); if (_paintLayer < 0) _paintLayer = 0;

    if (_selectedRegionX < 0 || _selectedRegionY < 0)
    {
        ImGui::TextDisabled("Select a region");
        return;
    }
    if (_selectedRegionY >= (int)_regionTiles.size() || _selectedRegionX >= (int)_regionTiles[_selectedRegionY].size())
    { ImGui::TextDisabled("(no data)"); return; }

    const auto& layers = _regionTiles[_selectedRegionY][_selectedRegionX];
    for (int l = 0; l < (int)layers.size(); l++)
    {
        ImGui::PushID(l);
        char lbl[32]; snprintf(lbl, sizeof(lbl), "Layer %d (%d tiles)", l, (int)layers[l].size());
        if (ImGui::TreeNode(lbl))
        {
            for (int ti = 0; ti < (int)layers[l].size(); ti++)
            {
                ImGui::PushID(ti);
                const auto& tile = layers[l][ti];
                unsigned int tex = tile.previewTexID;
                if (tex) ImGui::Image((ImTextureID)(uintptr_t)tex, ImVec2(18,18));
                else     ImGui::Dummy(ImVec2(18,18));
                ImGui::SameLine();
                bool sel = (ti == _selectedTileIdx && l == _paintLayer);
                char tlbl[128]; snprintf(tlbl, sizeof(tlbl), "[%d] %s (%.0f,%.0f)##tl",
                                         ti, std::string(tile.sprPath).substr(std::min(tile.sprPath.rfind('/')+1,(size_t)tile.sprPath.size())).c_str(),
                                         tile.x, tile.y);
                if (ImGui::Selectable(tlbl, sel)) { _selectedTileIdx = ti; _paintLayer = l; }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (layers.empty()) ImGui::TextDisabled("(no tiles)");
}

// ─────────────────────────────────────────────────────────
// RenderStatusBar
// ─────────────────────────────────────────────────────────
void MapEditor::RenderStatusBar()
{
    ImGui::Separator();

    // ── Progress bar khi đang load map ────────────────────────
    if (_isLoading.load())
    {
        float prog = _loadProgress.load();
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "Loading... %.0f%%", prog * 100.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 1.0f, 1.f));
        ImGui::ProgressBar(prog, ImVec2(-1.f, 14.f), overlay);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.f, 1.f), "%s",
                           fs::path(_pendingLoadPath).filename().string().c_str());
        return;
    }

    if (_statusTimer > 0.f)
        ImGui::TextColored(ImVec4(.4f,1.f,.4f,1.f), "%s", _statusMsg);
    else
        ImGui::TextDisabled("Ready  |  %s  |  %dx%d  unit=%d  |  zoom=%.2fx  |  layer=%d",
                             _currentMapPath.empty() ? "(no map)" : _currentMapPath.c_str(),
                             _mapWidth, _mapHeight, _unitSize, _viewScale, _paintLayer);
}

// ─────────────────────────────────────────────────────────
// RenderMapPropertiesPanel
// ─────────────────────────────────────────────────────────
void MapEditor::RenderMapPropertiesPanel()
{
    if (_mapWidth == 0) return;
    int w = _mapWidth, h = _mapHeight, u = _unitSize;
    bool changed = false;
    ImGui::SetNextItemWidth(90); changed |= ImGui::InputInt("Map W##mp", &w); if (w < 64) w = 64;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90); changed |= ImGui::InputInt("Map H##mp", &h); if (h < 64) h = 64;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70); changed |= ImGui::InputInt("Unit##mp",  &u); if (u < 16) u = 16;
    if (changed) SetMapProperties(w, h, u);
}

// ─────────────────────────────────────────────────────────
// RenderDialogs
// ─────────────────────────────────────────────────────────
void MapEditor::RenderDialogs()
{
    // ── New Map ─────────────────────────────────────────────
    if (_dlgNewMap)
    {
        ImGui::OpenPopup("New Map##dlg");
        _dlgNewMap = false;
    }
    if (ImGui::BeginPopupModal("New Map##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Map Name##nm", _newMapName, sizeof(_newMapName));
        ImGui::InputText("Base Folder##nm", _newMapFolder, sizeof(_newMapFolder));
        ImGui::SameLine();
        if (ImGui::Button("Browse##nmb"))
        {
            std::string f = BrowseForFolder("Select base folder");
            if (!f.empty()) strncpy(_newMapFolder, f.c_str(), sizeof(_newMapFolder)-1);
        }
        ImGui::InputInt("Width##nm",  &_newMapW);    if (_newMapW    < 64)  _newMapW = 64;
        ImGui::InputInt("Height##nm", &_newMapH);    if (_newMapH    < 64)  _newMapH = 64;
        ImGui::InputInt("UnitSize##nm",&_newMapUnit); if (_newMapUnit < 16) _newMapUnit = 16;

        ImGui::Text("Export Type:");
        ImGui::SameLine();
        ImGui::RadioButton("S (server only)##nm", &_exportType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("C (client+server)##nm", &_exportType, 1);
        ImGui::TextDisabled("  S = region_S only (obstacles)\n  C = region_S + region_C (obstacles + tiles)");

        if (ImGui::Button("Create", ImVec2(100,0)))
        {
            if (_newMapName[0] && _newMapFolder[0])
            {
                std::string fp = std::string(_newMapFolder) + "/" + _newMapName;
                CreateNewMap(fp, _newMapW, _newMapH, _newMapUnit);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Load Map ────────────────────────────────────────────
    if (_dlgLoadMap)
    {
        ImGui::OpenPopup("Load Map##dlg");
        _dlgLoadMap = false;
    }
    if (ImGui::BeginPopupModal("Load Map##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("Chọn thư mục map (chứa region_C):");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##lmcp", _dlgBufC, sizeof(_dlgBufC));
        ImGui::SameLine();
        if (ImGui::Button("...##lmcbr"))
        {
            std::string f = BrowseForFolder("Select map folder");
            if (!f.empty()) strncpy(_dlgBufC, f.c_str(), sizeof(_dlgBufC)-1);
        }

        ImGui::Spacing();
        ImGui::Separator();

        bool canLoad = (_dlgBufC[0] != '\0');
        if (!canLoad) ImGui::BeginDisabled();
        if (ImGui::Button("Load", ImVec2(100,0)))
        {
            LoadMap(_dlgBufC);
            ImGui::CloseCurrentPopup();
        }
        if (!canLoad) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Save As ─────────────────────────────────────────────
    if (_dlgSaveAs)
    {
        ImGui::OpenPopup("Save Map As##dlg");
        _dlgSaveAs = false;
    }
    if (ImGui::BeginPopupModal("Save Map As##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!_currentMapPath.empty() && _dlgBuf[0] == '\0')
            strncpy(_dlgBuf, _currentMapPath.c_str(), sizeof(_dlgBuf)-1);
        ImGui::InputText("Map Folder##sa", _dlgBuf, sizeof(_dlgBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##sab"))
        {
            std::string f = BrowseForFolder("Save map to folder");
            if (!f.empty()) strncpy(_dlgBuf, f.c_str(), sizeof(_dlgBuf)-1);
        }
        ImGui::Text("Export Type:");
        ImGui::SameLine();
        ImGui::RadioButton("S (server only)##sa", &_exportType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("C (client+server)##sa", &_exportType, 1);
        ImGui::TextDisabled("  S = region_S only  |  C = region_C only");
        if (ImGui::Button("Save", ImVec2(100,0)))
        {
            if (_dlgBuf[0]) SaveMapTo(_dlgBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Add Tileset Folder ───────────────────────────────────
    if (_dlgAddTilesetFolder)
    {
        ImGui::OpenPopup("Add Tileset Folder##dlg");
        _dlgAddTilesetFolder = false;
    }
    if (ImGui::BeginPopupModal("Add Tileset Folder##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Folder##tf", _tilesetFolderBuf, sizeof(_tilesetFolderBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##tfb"))
        {
            std::string f = BrowseForFolder("Select tileset folder");
            if (!f.empty()) strncpy(_tilesetFolderBuf, f.c_str(), sizeof(_tilesetFolderBuf)-1);
        }
        if (ImGui::Button("Add", ImVec2(100,0)))
        {
            if (_tilesetFolderBuf[0]) AddTilesetFolder(_tilesetFolderBuf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ═══════════════════════════════════════════════════════════
// LoadFromConfig / SaveToConfig  — AppConfig integration
// ═══════════════════════════════════════════════════════════
void MapEditor::LoadFromConfig()
{
    AppConfig& cfg = AppConfig::Get();

    // ── VFS root ─────────────────────────────────────────────
    if (!cfg.mapVfsRoot.empty())
    {
        _vfsRoot = cfg.mapVfsRoot;
        strncpy(_vfsRootBuf, _vfsRoot.c_str(), sizeof(_vfsRootBuf) - 1);
    }

    // ── Export type ───────────────────────────────────────────
    _exportType = cfg.mapExportType;

    // ── Tileset folders ───────────────────────────────────────
    for (const auto& tf : cfg.mapTilesetFolders)
    {
        if (!tf.empty() && fs::is_directory(tf))
            AddTilesetFolder(tf);
    }

    // ── Auto-load last map ────────────────────────────────────
    if (!cfg.mapLastPath.empty() && fs::is_directory(cfg.mapLastPath))
        LoadMap(cfg.mapLastPath);
}

void MapEditor::SaveToConfig()
{
    AppConfig& cfg = AppConfig::Get();

    // ── Current map path ──────────────────────────────────────
    if (!_currentMapPath.empty())
        cfg.mapLastPath = _currentMapPath;

    // ── VFS root ──────────────────────────────────────────────
    cfg.mapVfsRoot   = _vfsRoot;

    // ── Export type ───────────────────────────────────────────
    cfg.mapExportType = _exportType;

    // ── Tileset folders ───────────────────────────────────────
    cfg.mapTilesetFolders.clear();
    for (const auto& folder : _tilesetFolders)
        cfg.mapTilesetFolders.push_back(folder.folderPath);
}
