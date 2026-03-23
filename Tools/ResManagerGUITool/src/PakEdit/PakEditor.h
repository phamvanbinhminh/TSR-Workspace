#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// PakEditor  –  full GUI tool for browsing / editing .pak files
// ──────────────────────────────────────────────────────────────

struct PakEditorEntry
{
    std::string name;        // virtual path inside pak  e.g. "ui/icon.png"
    std::string displayName; // basename only             e.g. "icon.png"
    uint64_t    size     = 0;
    uint64_t    compSize = 0;
    uint32_t    crc32    = 0;
    uint16_t    compression = 0;
    uint16_t    encryption  = 0;
    bool        selected = false;
    bool        dirty    = false;  // added/modified but not yet saved
    std::vector<uint8_t> data;     // raw (uncompressed) bytes; empty = not loaded yet
};

// Minimal tree node for directory display
struct PakTreeNode
{
    std::string            name;
    std::vector<PakTreeNode> children;
    std::vector<int>       fileIndices; // indices into _entries
    bool                   open = false;
};

class PakEditor
{
public:
    PakEditor();
    ~PakEditor() = default;

    // ── Main GUI entry point ───────────────────────────────
    void RenderEditor();

    // ── PAK I/O ────────────────────────────────────────────
    bool LoadPak(const std::string& path);
    bool SavePak(const std::string& path);
    bool PackFolder(const std::string& folder, const std::string& pakPath);
    bool ExtractAll(const std::string& outDir);
    bool ExtractSelected(const std::string& outDir);

    // ── Entry management ───────────────────────────────────
    bool AddFile(const std::string& diskPath, const std::string& virtualPath = "");
    void AddFolderRecursive(const std::string& folder, const std::string& prefix = "");
    void RemoveSelected();
    void RemoveEntry(int idx);

    // ── Selection helpers ──────────────────────────────────
    void SelectAll();
    void SelectNone();
    void InvertSelection();

    // ── Queries ────────────────────────────────────────────
    bool        HasUnsavedChanges()    const { return _dirty; }
    size_t      GetFileCount()         const { return _entries.size(); }
    uint64_t    GetTotalSize()         const;
    const std::string& GetCurrentPath() const { return _pakPath; }

private:
    // ── Sub-panels ─────────────────────────────────────────
    void RenderMenuBar();
    void RenderToolbar();
    void RenderFileTable();
    void RenderTreePanel();
    void RenderPreviewPanel();
    void RenderStatusBar();
    void RenderDialogs();

    // ── Helpers ────────────────────────────────────────────
    void  RebuildTree();
    void  SortEntries();
    void  MarkDirty()       { _dirty = true; }
    int   SelectedCount()   const;
    static std::string SizeStr(uint64_t bytes);
    static std::string ExtIcon(const std::string& name);
    static uint32_t    CalcCRC32(const uint8_t* data, size_t len);

    // ── Data ───────────────────────────────────────────────
    std::vector<PakEditorEntry> _entries;
    std::unordered_map<std::string, int> _nameIndex; // virtual path → index
    PakTreeNode  _root;
    std::string  _pakPath;
    bool         _dirty = false;

    // ── UI state ───────────────────────────────────────────
    int   _selectedIdx   = -1;
    bool  _showTree      = true;
    bool  _showPreview   = true;
    float _treePanelW    = 220.f;
    float _previewPanelW = 260.f;
    float _previewScale  = 1.f;
    int   _sortCol       = 0;   // 0=name 1=size 2=comp 3=crc
    bool  _sortAsc       = true;
    char  _searchBuf[256]{};

    // ── Preview ────────────────────────────────────────────
    unsigned int _previewTexID = 0;
    int          _previewTexW  = 0;
    int          _previewTexH  = 0;
    std::string  _previewText;   // for text files

    // ── Dialogs ────────────────────────────────────────────
    bool _dlgLoad       = false;
    bool _dlgSave       = false;
    bool _dlgPackFolder = false;
    bool _dlgExtract    = false;
    bool _dlgAddFile    = false;
    char _dlgBufA[512]{};  // path A
    char _dlgBufB[512]{};  // path B (virtual / output dir)
    char _statusMsg[256]{};
    float _statusTimer  = 0.f;
   // ── Rename dialog
    int  _renamingIdx   = -1;
    char _renameBuf[512]{};

 

    // ── Pack options ───────────────────────────────────────
    bool _optSubfolders   = true;
    bool _optOverwrite    = true;

    // ── Texture preview registry (needs GL) ───────────────
    void LoadPreviewFor(int idx);
    void FreePreviewTex();
};
