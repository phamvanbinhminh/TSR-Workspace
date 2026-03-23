#include "PakEditor.h"
#include "ResManager/Pak/Pak.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iostream>

// stb_image – header only (implementation is elsewhere)
#include "Renderer/stb_image.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────
// CRC-32 (ISO 3309 / ethernet polynomial)
// ─────────────────────────────────────────────────────────────
static const uint32_t CRC32_TABLE[256] = {
#define P 0xEDB88320u
    // precompute at runtime via a lambda so we don't need a 256-entry literal
    0 // filled in PakEditor::CalcCRC32
#undef P
};

uint32_t PakEditor::CalcCRC32(const uint8_t* buf, size_t len)
{
    static bool init = false;
    static uint32_t tbl[256];
    if (!init)
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = tbl[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
PakEditor::PakEditor()
{
    memset(_searchBuf,  0, sizeof(_searchBuf));
    memset(_dlgBufA,    0, sizeof(_dlgBufA));
    memset(_dlgBufB,    0, sizeof(_dlgBufB));
    memset(_statusMsg,  0, sizeof(_statusMsg));
    _root.name = "/";
}

// ─────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────
std::string PakEditor::SizeStr(uint64_t b)
{
    char buf[32];
    if      (b < 1024ULL)              snprintf(buf, sizeof(buf), "%llu B",   (unsigned long long)b);
    else if (b < 1024ULL*1024)         snprintf(buf, sizeof(buf), "%.1f KB",  b/1024.0);
    else if (b < 1024ULL*1024*1024)    snprintf(buf, sizeof(buf), "%.1f MB",  b/(1024.0*1024));
    else                               snprintf(buf, sizeof(buf), "%.2f GB",  b/(1024.0*1024*1024));
    return buf;
}

std::string PakEditor::ExtIcon(const std::string& name)
{
    std::string ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".gif"||ext==".tga") return "[IMG]";
    if (ext==".spr")  return "[SPR]";
    if (ext==".pak")  return "[PAK]";
    if (ext==".lua")  return "[LUA]";
    if (ext==".txt"||ext==".json"||ext==".xml"||ext==".csv") return "[TXT]";
    if (ext==".wav"||ext==".ogg"||ext==".mp3") return "[SND]";
    if (ext==".ttf"||ext==".otf") return "[FNT]";
    if (ext==".glsl"||ext==".vert"||ext==".frag") return "[SHD]";
    return "[---]";
}

int PakEditor::SelectedCount() const
{
    int n = 0;
    for (const auto& e : _entries) if (e.selected) n++;
    return n;
}

uint64_t PakEditor::GetTotalSize() const
{
    uint64_t t = 0;
    for (const auto& e : _entries) t += e.size;
    return t;
}

// ─────────────────────────────────────────────────────────────
// Texture preview helpers
// ─────────────────────────────────────────────────────────────
void PakEditor::FreePreviewTex()
{
    if (_previewTexID) { glDeleteTextures(1, &_previewTexID); _previewTexID = 0; }
    _previewTexW = _previewTexH = 0;
    _previewText.clear();
}

void PakEditor::LoadPreviewFor(int idx)
{
    FreePreviewTex();
    if (idx < 0 || idx >= (int)_entries.size()) return;
    PakEditorEntry& e = _entries[idx];

    // ensure data is loaded
    if (e.data.empty()) return;

    std::string ext = fs::path(e.name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Image preview
    if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tga"||ext==".gif")
    {
        int w=0,h=0,ch=0;
        unsigned char* pix = stbi_load_from_memory(
            e.data.data(), (int)e.data.size(), &w, &h, &ch, 4);
        if (pix)
        {
            glGenTextures(1, &_previewTexID);
            glBindTexture(GL_TEXTURE_2D, _previewTexID);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pix);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
            stbi_image_free(pix);
            _previewTexW = w; _previewTexH = h;
        }
        return;
    }

    // Text preview (first 4 KB)
    if (ext==".txt"||ext==".json"||ext==".xml"||ext==".lua"||
        ext==".glsl"||ext==".vert"||ext==".frag"||ext==".csv"||ext==".ini")
    {
        size_t n = std::min(e.data.size(), (size_t)4096);
        _previewText.assign((char*)e.data.data(), n);
        return;
    }
}

// ─────────────────────────────────────────────────────────────
// Rebuild virtual directory tree
// ─────────────────────────────────────────────────────────────
void PakEditor::RebuildTree()
{
    _root.children.clear();
    _root.fileIndices.clear();

    for (int i = 0; i < (int)_entries.size(); i++)
    {
        const std::string& path = _entries[i].name;
        std::vector<std::string> parts;
        {
            std::stringstream ss(path);
            std::string part;
            while (std::getline(ss, part, '/'))
                if (!part.empty()) parts.push_back(part);
        }
        if (parts.empty()) continue;

        PakTreeNode* cur = &_root;
        for (int d = 0; d < (int)parts.size()-1; d++)
        {
            bool found = false;
            for (auto& c : cur->children)
                if (c.name == parts[d]) { cur = &c; found = true; break; }
            if (!found)
            {
                cur->children.push_back(PakTreeNode{parts[d]});
                cur = &cur->children.back();
            }
        }
        cur->fileIndices.push_back(i);
    }
}

// ─────────────────────────────────────────────────────────────
// Sort entries
// ─────────────────────────────────────────────────────────────
void PakEditor::SortEntries()
{
    std::sort(_entries.begin(), _entries.end(),
        [this](const PakEditorEntry& a, const PakEditorEntry& b)
        {
            bool lt = false;
            switch (_sortCol)
            {
                case 0: lt = a.name < b.name; break;
                case 1: lt = a.size < b.size; break;
                case 2: lt = a.compSize < b.compSize; break;
                case 3: lt = a.crc32 < b.crc32; break;
                default: lt = a.name < b.name;
            }
            return _sortAsc ? lt : !lt;
        });
    _nameIndex.clear();
    for (int i=0;i<(int)_entries.size();i++)
        _nameIndex[_entries[i].name]=i;
    RebuildTree();
}

// ─────────────────────────────────────────────────────────────
// Selection helpers
// ─────────────────────────────────────────────────────────────
void PakEditor::SelectAll()   { for (auto& e:_entries) e.selected=true;  }
void PakEditor::SelectNone()  { for (auto& e:_entries) e.selected=false; _selectedIdx=-1; }
void PakEditor::InvertSelection() { for (auto& e:_entries) e.selected=!e.selected; }

// ─────────────────────────────────────────────────────────────
// LoadPak
// ─────────────────────────────────────────────────────────────
bool PakEditor::LoadPak(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        snprintf(_statusMsg,sizeof(_statusMsg),"LOAD FAILED: %s",path.c_str());
        _statusTimer=5.f; return false;
    }

    PakHeader hdr{};
    in.read((char*)&hdr, sizeof(hdr));
    if (strncmp(hdr.magic,"PAK0",4)!=0)
    {
        snprintf(_statusMsg,sizeof(_statusMsg),"Not a PAK0 file: %s",path.c_str());
        _statusTimer=5.f; return false;
    }

    std::vector<PakFileEntry> raw(hdr.fileCount);
    in.seekg(hdr.tableOffset);
    in.read((char*)raw.data(), hdr.fileCount*sizeof(PakFileEntry));

    uint32_t strSize = hdr.dataOffset - hdr.stringTableOffset;
    std::vector<char> strTbl(strSize+1, 0);
    in.seekg(hdr.stringTableOffset);
    in.read(strTbl.data(), strSize);

    FreePreviewTex();
    _entries.clear(); _nameIndex.clear(); _selectedIdx=-1;

    for (auto& re : raw)
    {
        PakEditorEntry e;
        e.name        = &strTbl[re.nameOffset];
        e.displayName = fs::path(e.name).filename().string();
        e.size        = re.size;
        e.compSize    = re.compressedSize;
        e.crc32       = re.crc32;
        e.compression = re.compression;
        e.encryption  = re.encryption;

        // load data
        e.data.resize((size_t)re.size);
        in.seekg((std::streamoff)re.offset);
        in.read((char*)e.data.data(), (std::streamsize)re.size);

        _nameIndex[e.name] = (int)_entries.size();
        _entries.push_back(std::move(e));
    }

    _pakPath = path;
    _dirty   = false;
    RebuildTree();
    snprintf(_statusMsg,sizeof(_statusMsg),
             "Loaded: %s  (%d files, %s total)",
             fs::path(path).filename().string().c_str(),
             (int)_entries.size(),
             SizeStr(GetTotalSize()).c_str());
    _statusTimer=5.f;
    return true;
}

// ─────────────────────────────────────────────────────────────
// SavePak
// ─────────────────────────────────────────────────────────────
bool PakEditor::SavePak(const std::string& path)
{
    fs::path outDir = fs::path(path).parent_path();
    if (!outDir.empty()) { std::error_code ec; fs::create_directories(outDir,ec); }

    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        snprintf(_statusMsg,sizeof(_statusMsg),"SAVE FAILED: %s",path.c_str());
        _statusTimer=5.f; return false;
    }

    // build string table
    std::string strTbl;
    std::vector<uint32_t> nameOffsets(_entries.size());
    for (int i=0;i<(int)_entries.size();i++)
    {
        nameOffsets[i]=(uint32_t)strTbl.size();
        strTbl += _entries[i].name;
        strTbl.push_back('\0');
    }

    PakHeader hdr{};
    memcpy(hdr.magic,"PAK0",4);
    hdr.version      = 1;
    hdr.fileCount    = (uint32_t)_entries.size();
    hdr.tableOffset  = sizeof(PakHeader);
    hdr.stringTableOffset = hdr.tableOffset + (uint32_t)(_entries.size()*sizeof(PakFileEntry));
    hdr.dataOffset   = hdr.stringTableOffset + (uint32_t)strTbl.size();

    // compute data offsets
    uint64_t cursor = hdr.dataOffset;
    std::vector<PakFileEntry> rawEntries(_entries.size());
    for (int i=0;i<(int)_entries.size();i++)
    {
        PakFileEntry& re = rawEntries[i];
        re.nameOffset    = nameOffsets[i];
        re.offset        = cursor;
        re.size          = _entries[i].data.size();
        re.compressedSize= _entries[i].data.size();
        re.crc32         = CalcCRC32(_entries[i].data.data(), _entries[i].data.size());
        re.compression   = 0;
        re.encryption    = 0;
        re.flags         = 0;
        cursor          += re.size;
    }

    out.write((char*)&hdr,         sizeof(hdr));
    out.write((char*)rawEntries.data(), rawEntries.size()*sizeof(PakFileEntry));
    out.write(strTbl.data(),        strTbl.size());
    for (auto& e : _entries)
        out.write((char*)e.data.data(), e.data.size());

    _pakPath=path; _dirty=false;
    // update crc & compSize from saved
    for (int i=0;i<(int)_entries.size();i++)
    {
        _entries[i].crc32    = rawEntries[i].crc32;
        _entries[i].compSize = rawEntries[i].compressedSize;
        _entries[i].dirty    = false;
    }
    snprintf(_statusMsg,sizeof(_statusMsg),"Saved: %s",fs::path(path).filename().string().c_str());
    _statusTimer=4.f;
    return true;
}

// ─────────────────────────────────────────────────────────────
// PackFolder  (replaces everything)
// ─────────────────────────────────────────────────────────────
bool PakEditor::PackFolder(const std::string& folder, const std::string& pakPath)
{
    FreePreviewTex();
    _entries.clear(); _nameIndex.clear(); _selectedIdx=-1;
    _pakPath=pakPath; _dirty=true;
    AddFolderRecursive(folder, "");
    RebuildTree();
    snprintf(_statusMsg,sizeof(_statusMsg),
             "Packed %d files from: %s",
             (int)_entries.size(), folder.c_str());
    _statusTimer=4.f;
    return !_entries.empty();
}

// ─────────────────────────────────────────────────────────────
// AddFile
// ─────────────────────────────────────────────────────────────
bool PakEditor::AddFile(const std::string& diskPath, const std::string& virtualPath)
{
    std::ifstream f(diskPath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    std::string vp = virtualPath.empty() ? fs::path(diskPath).filename().string() : virtualPath;

    PakEditorEntry e;
    e.name        = vp;
    e.displayName = fs::path(vp).filename().string();
    e.size        = data.size();
    e.compSize    = data.size();
    e.crc32       = CalcCRC32(data.data(), data.size());
    e.dirty       = true;
    e.data        = std::move(data);

    auto it = _nameIndex.find(vp);
    if (it != _nameIndex.end())
    {
        _entries[it->second] = std::move(e);
    }
    else
    {
        _nameIndex[vp] = (int)_entries.size();
        _entries.push_back(std::move(e));
    }
    MarkDirty();
    RebuildTree();
    return true;
}

void PakEditor::AddFolderRecursive(const std::string& folder, const std::string& prefix)
{
    try
    {
        for (const auto& entry : fs::directory_iterator(folder))
        {
            if (entry.is_regular_file())
            {
                std::string rel = prefix.empty()
                    ? entry.path().filename().string()
                    : (prefix + "/" + entry.path().filename().string());
                AddFile(entry.path().string(), rel);
            }
            else if (entry.is_directory() && _optSubfolders)
            {
                std::string sub = prefix.empty()
                    ? entry.path().filename().string()
                    : (prefix + "/" + entry.path().filename().string());
                AddFolderRecursive(entry.path().string(), sub);
            }
        }
    }
    catch (...) {}
}

// ─────────────────────────────────────────────────────────────
// RemoveEntry / RemoveSelected
// ─────────────────────────────────────────────────────────────
void PakEditor::RemoveEntry(int idx)
{
    if (idx<0||idx>=(int)_entries.size()) return;
    _entries.erase(_entries.begin()+idx);
    _nameIndex.clear();
    for (int i=0;i<(int)_entries.size();i++)
        _nameIndex[_entries[i].name]=i;
    if (_selectedIdx==idx)     { FreePreviewTex(); _selectedIdx=-1; }
    else if (_selectedIdx>idx)  _selectedIdx--;
    MarkDirty(); RebuildTree();
}

void PakEditor::RemoveSelected()
{
    FreePreviewTex(); _selectedIdx=-1;
    _entries.erase(
        std::remove_if(_entries.begin(),_entries.end(),
            [](const PakEditorEntry& e){ return e.selected; }),
        _entries.end());
    _nameIndex.clear();
    for (int i=0;i<(int)_entries.size();i++)
        _nameIndex[_entries[i].name]=i;
    MarkDirty(); RebuildTree();
}

// ─────────────────────────────────────────────────────────────
// Extract helpers
// ─────────────────────────────────────────────────────────────
static bool WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    o.write((char*)data.data(), data.size());
    return true;
}

bool PakEditor::ExtractAll(const std::string& outDir)
{
    bool ok = true;
    for (const auto& e : _entries)
        if (!e.data.empty())
            ok &= WriteFile(outDir+"/"+e.name, e.data);
    snprintf(_statusMsg,sizeof(_statusMsg),"Extracted %d files to: %s",
             (int)_entries.size(), outDir.c_str());
    _statusTimer=4.f;
    return ok;
}

bool PakEditor::ExtractSelected(const std::string& outDir)
{
    int n=0; bool ok=true;
    for (const auto& e : _entries)
        if (e.selected&&!e.data.empty())
        { ok &= WriteFile(outDir+"/"+e.name, e.data); n++; }
    snprintf(_statusMsg,sizeof(_statusMsg),"Extracted %d file(s) to: %s",n,outDir.c_str());
    _statusTimer=4.f;
    return ok;
}

// ─────────────────────────────────────────────────────────────
// RenderEditor  (main entry point)
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderEditor()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (_statusTimer > 0.f) _statusTimer -= dt;

    RenderMenuBar();
    RenderToolbar();
    RenderDialogs();

    // Three-column layout: tree | file table | preview
    float totalW    = ImGui::GetContentRegionAvail().x;
    float tableW    = totalW - (_showTree ? _treePanelW : 0.f)
                             - (_showPreview ? _previewPanelW : 0.f);

    if (_showTree)
    {
        ImGui::BeginChild("##TreePanel", ImVec2(_treePanelW, 0), true);
        RenderTreePanel();
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGui::BeginChild("##FileTablePanel", ImVec2(tableW, 0), true);
    RenderFileTable();
    ImGui::EndChild();

    if (_showPreview)
    {
        ImGui::SameLine();
        ImGui::BeginChild("##PreviewPanel", ImVec2(_previewPanelW, 0), true);
        RenderPreviewPanel();
        ImGui::EndChild();
    }

    RenderStatusBar();
}

// ─────────────────────────────────────────────────────────────
// RenderMenuBar
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New PAK"))
        {
            FreePreviewTex();
            _entries.clear(); _nameIndex.clear(); _pakPath.clear();
            _dirty=false; _selectedIdx=-1; RebuildTree();
        }
        if (ImGui::MenuItem("Open PAK..."))         _dlgLoad=true;
        if (ImGui::MenuItem("Save",    "Ctrl+S", false, !_pakPath.empty()))
            SavePak(_pakPath);
        if (ImGui::MenuItem("Save As..."))          _dlgSave=true;
        ImGui::Separator();
        if (ImGui::MenuItem("Pack Folder..."))      _dlgPackFolder=true;
        if (ImGui::MenuItem("Extract All..."))      _dlgExtract=true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Add File(s)..."))      _dlgAddFile=true;
        if (ImGui::MenuItem("Remove Selected", nullptr, false, SelectedCount()>0))
            RemoveSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Select All",    "Ctrl+A")) SelectAll();
        if (ImGui::MenuItem("Select None",   "Ctrl+D")) SelectNone();
        if (ImGui::MenuItem("Invert",        "Ctrl+I")) InvertSelection();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Directory Tree",    nullptr, &_showTree);
        ImGui::MenuItem("Preview Panel",     nullptr, &_showPreview);
        ImGui::Separator();
        ImGui::Checkbox("Include Subfolders",&_optSubfolders);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ─────────────────────────────────────────────────────────────
// RenderToolbar
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderToolbar()
{
    if (ImGui::Button("Open##tb"))      _dlgLoad=true;       ImGui::SameLine();
    if (ImGui::Button("Save##tb") && !_pakPath.empty()) SavePak(_pakPath); ImGui::SameLine();
    if (ImGui::Button("Add File##tb"))  _dlgAddFile=true;    ImGui::SameLine();
    if (ImGui::Button("Pack Folder##tb")) _dlgPackFolder=true; ImGui::SameLine();
    if (ImGui::Button("Extract All##tb")) _dlgExtract=true;  ImGui::SameLine();

    bool hasSel = SelectedCount()>0;
    if (!hasSel) ImGui::BeginDisabled();
    if (ImGui::Button("Remove Sel##tb")) RemoveSelected();
    if (!hasSel) ImGui::EndDisabled();
    ImGui::SameLine();

    if (!hasSel) ImGui::BeginDisabled();
    if (ImGui::Button("Extract Sel##tb")) { _dlgExtract=true; } // reuse same dialog
    if (!hasSel) ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("Sel All##tb"))  SelectAll();  ImGui::SameLine();
    if (ImGui::Button("Sel None##tb")) SelectNone();

    ImGui::SameLine(0,20);
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Search##tb", _searchBuf, sizeof(_searchBuf));
    ImGui::SameLine();
    if (ImGui::SmallButton("x##clsrch")) _searchBuf[0]='\0';

    if (_dirty)
    {
        ImGui::SameLine(0,20);
        ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f),"[unsaved]");
    }
    ImGui::Separator();
}

// ─────────────────────────────────────────────────────────────
// Helper: recursive tree render
// ─────────────────────────────────────────────────────────────
static void RenderTreeNodeRec(PakTreeNode& node, std::vector<PakEditorEntry>& entries,
                               int& selectedIdx, bool& changed)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.open) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
    node.open = open;
    if (open)
    {
        for (int fi : node.fileIndices)
        {
            if (fi < 0 || fi >= (int)entries.size()) continue;
            ImGuiTreeNodeFlags lf = ImGuiTreeNodeFlags_Leaf
                                  | ImGuiTreeNodeFlags_SpanAvailWidth
                                  | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (selectedIdx==fi) lf |= ImGuiTreeNodeFlags_Selected;
            ImGui::TreeNodeEx(entries[fi].displayName.c_str(), lf);
            if (ImGui::IsItemClicked()) { selectedIdx=fi; changed=true; }
        }
        for (auto& c : node.children)
            RenderTreeNodeRec(c, entries, selectedIdx, changed);
        ImGui::TreePop();
    }
}

// ─────────────────────────────────────────────────────────────
// RenderTreePanel
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderTreePanel()
{
    ImGui::Text("Directory Tree");
    ImGui::Separator();
    if (_entries.empty()) { ImGui::TextDisabled("(empty)"); return; }

    bool changed = false;
    int prev = _selectedIdx;
    for (auto& c : _root.children)
        RenderTreeNodeRec(c, _entries, _selectedIdx, changed);

    // files at root level
    for (int fi : _root.fileIndices)
    {
        if (fi<0||fi>=(int)_entries.size()) continue;
        ImGuiTreeNodeFlags lf = ImGuiTreeNodeFlags_Leaf
                              | ImGuiTreeNodeFlags_SpanAvailWidth
                              | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (_selectedIdx==fi) lf |= ImGuiTreeNodeFlags_Selected;
        ImGui::TreeNodeEx(_entries[fi].displayName.c_str(), lf);
        if (ImGui::IsItemClicked()) { _selectedIdx=fi; changed=true; }
    }

    if (changed && _selectedIdx != prev)
        LoadPreviewFor(_selectedIdx);
}

// ─────────────────────────────────────────────────────────────
// RenderFileTable
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderFileTable()
{
    ImGui::Text("Files in PAK: %d  |  Total: %s",
                (int)_entries.size(), SizeStr(GetTotalSize()).c_str());
    ImGui::Separator();

    if (_entries.empty())
    {
        ImGui::TextDisabled("(no files — drag & drop files here, or use Add File / Pack Folder)");
        // accept drag-drop
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* pl=ImGui::AcceptDragDropPayload("DND_FILE_PATH"))
                AddFile(std::string((const char*)pl->Data));
            ImGui::EndDragDropTarget();
        }
        return;
    }

    // Column headers
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.25f,.25f,.25f,1.f));
    float cw[5] = {28, avail*0.45f, 90, 90, 70};
    // checkbox col
    ImGui::Dummy(ImVec2(cw[0],0)); ImGui::SameLine();
    auto ColHdr=[&](const char* lbl, int col, float w)
    {
        ImGui::SetNextItemWidth(w);
        bool active = _sortCol==col;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.3f,.6f,.9f,1.f));
        if (ImGui::Button(lbl, ImVec2(w,0)))
        {
            if (_sortCol==col) _sortAsc=!_sortAsc;
            else { _sortCol=col; _sortAsc=true; }
            SortEntries();
        }
        if (active) ImGui::PopStyleColor();
    };
    ColHdr("Name",    0, cw[1]); ImGui::SameLine();
    ColHdr("Size",    1, cw[2]); ImGui::SameLine();
    ColHdr("Comp",    2, cw[3]); ImGui::SameLine();
    ColHdr("CRC32",   3, cw[4]);
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Rows
    std::string searchLo = _searchBuf;
    std::transform(searchLo.begin(),searchLo.end(),searchLo.begin(),::tolower);

    ImGui::BeginChild("##rows", ImVec2(0,0), false);
    for (int i=0;i<(int)_entries.size();i++)
    {
        PakEditorEntry& e = _entries[i];

        // search filter
        if (!searchLo.empty())
        {
            std::string lo = e.name;
            std::transform(lo.begin(),lo.end(),lo.begin(),::tolower);
            if (lo.find(searchLo)==std::string::npos) continue;
        }

        ImGui::PushID(i);

        // checkbox
        ImGui::Checkbox("##chk",&e.selected);
        ImGui::SameLine();

        // icon + name
        bool sel = _selectedIdx==i;
        std::string label = ExtIcon(e.name)+" "+e.name + (e.dirty?"*":"");
        ImVec4 col = e.dirty ? ImVec4(1.f,0.9f,0.4f,1.f) : ImVec4(1.f,1.f,1.f,1.f);
        ImGui::SetNextItemWidth(cw[1]);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::Selectable(label.c_str(), sel,
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(cw[1],0)))
        {
            _selectedIdx=i;
            for (auto& e2:_entries) e2.selected=false;
            e.selected=true;
            LoadPreviewFor(i);
        }
        ImGui::PopStyleColor();

                // right-click context
        if (ImGui::BeginPopupContextItem("##ctx"))
        {
            if (ImGui::MenuItem("Select"))    { for(auto& e2:_entries)e2.selected=false; e.selected=true; }
            if (ImGui::MenuItem("Deselect"))  e.selected=false;
            ImGui::Separator();
            if (ImGui::MenuItem("Extract...")) { _dlgExtract=true; }
            if (ImGui::MenuItem("Remove"))    { ImGui::EndPopup(); ImGui::PopID(); ImGui::EndChild(); RemoveEntry(i); return; }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename..."))
            {
                _renamingIdx = i;
                strncpy(_renameBuf, e.name.c_str(), sizeof(_renameBuf)-1);
                ImGui::OpenPopup("##rename");
            }
            ImGui::EndPopup();
        }

        // Rename inline popup
        if (_renamingIdx == i)
        {
            if (ImGui::BeginPopupModal("Rename Entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("New virtual path:");
                ImGui::SetNextItemWidth(320);
                ImGui::InputText("##rn", _renameBuf, sizeof(_renameBuf));
                if (ImGui::Button("OK", ImVec2(80,0)))
                {
                    std::string newName = _renameBuf;
                    if (!newName.empty() && newName != e.name)
                    {
                        _nameIndex.erase(e.name);
                        e.name = newName;
                        e.displayName = fs::path(newName).filename().string();
                        e.dirty = true;
                        _nameIndex[newName] = i;
                        MarkDirty(); RebuildTree();
                    }
                    _renamingIdx = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(80,0))) { _renamingIdx=-1; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            else
            {
                ImGui::OpenPopup("Rename Entry");
            }
        }

        // Size / Comp / CRC columns (same line as selectable)
        ImGui::SameLine(cw[0] + cw[1] + 4);
        ImGui::Text("%s", SizeStr(e.size).c_str());
        ImGui::SameLine(cw[0] + cw[1] + cw[2] + 8);
        ImGui::Text("%s", SizeStr(e.compSize).c_str());
        ImGui::SameLine(cw[0] + cw[1] + cw[2] + cw[3] + 12);
        ImGui::Text("%08X", e.crc32);

        ImGui::PopID();
    }
    ImGui::EndChild();

    // Drag-drop onto the file table (even when non-empty)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* pl=ImGui::AcceptDragDropPayload("DND_FILE_PATH"))
            AddFile(std::string((const char*)pl->Data));
        ImGui::EndDragDropTarget();
    }
}

// ─────────────────────────────────────────────────────────────
// RenderPreviewPanel
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderPreviewPanel()
{
    ImGui::Text("Preview");
    ImGui::Separator();

    if (_selectedIdx < 0 || _selectedIdx >= (int)_entries.size())
    {
        ImGui::TextDisabled("(select a file)");
        return;
    }

    const PakEditorEntry& e = _entries[_selectedIdx];

    // File info
    ImGui::TextColored(ImVec4(0.7f,0.9f,1.f,1.f), "%s", e.displayName.c_str());
    ImGui::Text("Path : %s", e.name.c_str());
    ImGui::Text("Size : %s  (%llu B)", SizeStr(e.size).c_str(), (unsigned long long)e.size);
    ImGui::Text("Comp : %s", SizeStr(e.compSize).c_str());
    ImGui::Text("CRC32: %08X", e.crc32);
    if (e.compression) ImGui::Text("Compress: %u", e.compression);
    if (e.encryption)  ImGui::Text("Encrypt : %u", e.encryption);
    if (e.dirty) ImGui::TextColored(ImVec4(1.f,0.8f,0.2f,1.f),"[unsaved changes]");
    ImGui::Separator();

    // Image preview
    if (_previewTexID)
    {
        ImGui::Text("Image: %d x %d", _previewTexW, _previewTexH);
        ImGui::SliderFloat("Scale", &_previewScale, 0.1f, 4.f, "%.1f");

        float maxW = ImGui::GetContentRegionAvail().x;
        float dispW = std::min((float)_previewTexW * _previewScale, maxW);
        float dispH = dispW * ((float)_previewTexH / std::max(_previewTexW,1));

        ImGui::BeginChild("##imgscroll", ImVec2(0, 0), false,
            ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::Image((ImTextureID)(uintptr_t)_previewTexID, ImVec2(dispW, dispH));
        ImGui::EndChild();
        return;
    }

    // Text preview
    if (!_previewText.empty())
    {
        ImGui::BeginChild("##txtpreview", ImVec2(0,0), false);
        ImGui::InputTextMultiline("##ptxt",
            const_cast<char*>(_previewText.c_str()),
            _previewText.size()+1,
            ImVec2(-1,-1),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
        return;
    }

    // Hex dump (first 256 bytes)
    if (!e.data.empty())
    {
        ImGui::TextDisabled("Hex dump (first 256 B):");
        ImGui::BeginChild("##hexdump", ImVec2(0,0), false);
        size_t limit = std::min(e.data.size(), (size_t)256);
        for (size_t row = 0; row < limit; row += 16)
        {
            // offset
            ImGui::Text("%04X: ", (unsigned)row);
            ImGui::SameLine();
            // hex bytes
            for (size_t col = 0; col < 16; col++)
            {
                if (row+col < limit)
                    ImGui::Text("%02X ", e.data[row+col]);
                else
                    ImGui::Text("   ");
                if (col < 15) ImGui::SameLine();
            }
            ImGui::SameLine();
            // ASCII
            char ascii[17]{};
            for (size_t col=0; col<16 && row+col<limit; col++)
            {
                uint8_t c2 = e.data[row+col];
                ascii[col] = (c2>=32&&c2<127)?(char)c2:'.';
            }
            ImGui::Text(" %s", ascii);
        }
        ImGui::EndChild();
        return;
    }

    ImGui::TextDisabled("(no data)");
}

// ─────────────────────────────────────────────────────────────
// RenderStatusBar
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderStatusBar()
{
    ImGui::Separator();

    // PAK path
    if (!_pakPath.empty())
        ImGui::TextDisabled("%s", _pakPath.c_str());
    else
        ImGui::TextDisabled("(no file)");

    ImGui::SameLine();

    float rightEdge = ImGui::GetContentRegionAvail().x;
    (void)rightEdge;

    // Status message (fades after timer)
    if (_statusTimer > 0.f)
    {
        float alpha = std::min(_statusTimer, 1.f);
        ImGui::SameLine(0, 30);
        ImGui::TextColored(ImVec4(0.4f,1.f,0.4f,alpha), "%s", _statusMsg);
    }

    // Selection count
    int selN = SelectedCount();
    if (selN > 0)
    {
        ImGui::SameLine(0, 30);
        ImGui::TextColored(ImVec4(1.f,0.8f,0.4f,1.f), "%d selected", selN);
    }
}

// ─────────────────────────────────────────────────────────────
// RenderDialogs  – all modal / inline input dialogs
// ─────────────────────────────────────────────────────────────
void PakEditor::RenderDialogs()
{
    // ── Open PAK ────────────────────────────────────────────
    if (_dlgLoad)
    {
        _dlgLoad = false;
        ImGui::OpenPopup("Open PAK##dlg");
        strncpy(_dlgBufA, _pakPath.c_str(), sizeof(_dlgBufA)-1);
    }
    if (ImGui::BeginPopupModal("Open PAK##dlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("PAK file path:");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##lpth", _dlgBufA, sizeof(_dlgBufA));
        ImGui::Spacing();
        if (ImGui::Button("Open", ImVec2(100,0)))
        {
            LoadPak(_dlgBufA);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Save As ──────────────────────────────────────────────
    if (_dlgSave)
    {
        _dlgSave = false;
        ImGui::OpenPopup("Save PAK As##dlg");
        strncpy(_dlgBufA, _pakPath.c_str(), sizeof(_dlgBufA)-1);
    }
    if (ImGui::BeginPopupModal("Save PAK As##dlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Save to path:");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##spth", _dlgBufA, sizeof(_dlgBufA));
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(100,0)))
        {
            SavePak(_dlgBufA);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Add File ─────────────────────────────────────────────
    if (_dlgAddFile)
    {
        _dlgAddFile = false;
        ImGui::OpenPopup("Add File##dlg");
        memset(_dlgBufA, 0, sizeof(_dlgBufA));
        memset(_dlgBufB, 0, sizeof(_dlgBufB));
    }
    if (ImGui::BeginPopupModal("Add File##dlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Disk path (source file):");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##afpth", _dlgBufA, sizeof(_dlgBufA));
        ImGui::Spacing();
        ImGui::Text("Virtual path in PAK (leave blank = filename only):");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##afvp", _dlgBufB, sizeof(_dlgBufB));
        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(100,0)))
        {
            if (_dlgBufA[0])
                AddFile(_dlgBufA, _dlgBufB);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Pack Folder ──────────────────────────────────────────
    if (_dlgPackFolder)
    {
        _dlgPackFolder = false;
        ImGui::OpenPopup("Pack Folder##dlg");
        memset(_dlgBufA, 0, sizeof(_dlgBufA));
        strncpy(_dlgBufB, _pakPath.c_str(), sizeof(_dlgBufB)-1);
    }
    if (ImGui::BeginPopupModal("Pack Folder##dlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Source folder:");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##pfdir", _dlgBufA, sizeof(_dlgBufA));
        ImGui::Spacing();
        ImGui::Text("Output PAK path:");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##pfout", _dlgBufB, sizeof(_dlgBufB));
        ImGui::Spacing();
        ImGui::Checkbox("Include subfolders", &_optSubfolders);
        ImGui::Spacing();
        if (ImGui::Button("Pack", ImVec2(100,0)))
        {
            if (_dlgBufA[0] && _dlgBufB[0])
                PackFolder(_dlgBufA, _dlgBufB);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100,0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Extract ──────────────────────────────────────────────
    if (_dlgExtract)
    {
        _dlgExtract = false;
        ImGui::OpenPopup("Extract##dlg");
        memset(_dlgBufA, 0, sizeof(_dlgBufA));
        // default: folder next to the pak
        if (!_pakPath.empty())
        {
            std::string def = fs::path(_pakPath).parent_path().string();
            strncpy(_dlgBufA, def.c_str(), sizeof(_dlgBufA)-1);
        }
    }
    if (ImGui::BeginPopupModal("Extract##dlg", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        bool hasSel = SelectedCount() > 0;
        ImGui::Text("Extract to folder:");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##exdir", _dlgBufA, sizeof(_dlgBufA));
        ImGui::Spacing();
        if (ImGui::Button("Extract All", ImVec2(120,0)))
        {
            if (_dlgBufA[0]) ExtractAll(_dlgBufA);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (!hasSel) ImGui::BeginDisabled();
        if (ImGui::Button("Extract Selected", ImVec2(140,0)))
        {
            if (_dlgBufA[0]) ExtractSelected(_dlgBufA);
            ImGui::CloseCurrentPopup();
        }
        if (!hasSel) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80,0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Keyboard shortcuts ───────────────────────────────────
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            if (!_pakPath.empty()) SavePak(_pakPath);
            else _dlgSave = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) SelectAll();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) SelectNone();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) InvertSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && SelectedCount()>0)
            RemoveSelected();
    }
}
