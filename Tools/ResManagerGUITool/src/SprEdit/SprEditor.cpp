#include "SprEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#include "Renderer/stb_image.h"

namespace fs = std::filesystem;

// ============================================================
// Helpers
// ============================================================

// Helper: integer input with − / + buttons, returns true if value changed
static bool IntStepper(const char* label, int* v, int step = 1)
{
    ImGui::PushID(label);
    float w = ImGui::GetContentRegionAvail().x - 60.f;
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputInt(label, v, 0, 0);
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) { *v -= step; changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) { *v += step; changed = true; }
    ImGui::PopID();
    return changed;
}

// Draw a labelled filled rectangle overlay (screen-space)
static void DrawOverlayRect(ImDrawList* dl,
                             ImVec2 origin, float scale,
                             int rx, int ry, int rw, int rh,
                             ImU32 col, const char* label)
{
    if (rw <= 0 || rh <= 0) return;
    float x0 = origin.x + rx * scale;
    float y0 = origin.y + ry * scale;
    float x1 = x0 + rw * scale;
    float y1 = y0 + rh * scale;
    ImU32 fill = (col & 0x00FFFFFFu) | 0x30000000u;
    dl->AddRectFilled(ImVec2(x0,y0), ImVec2(x1,y1), fill);
    dl->AddRect(ImVec2(x0,y0), ImVec2(x1,y1), col, 0.f, 0, 2.f);
    if (label && label[0])
        dl->AddText(ImVec2(x0 + 3.f, y0 + 2.f), col, label);
}

// ============================================================
// Constructor / Destructor
// ============================================================

SprEditor::SprEditor()
{
    memset(_importPathBuf,  0, sizeof(_importPathBuf));
    memset(_exportPathBuf,  0, sizeof(_exportPathBuf));
    memset(_loadPathBuf,    0, sizeof(_loadPathBuf));
    memset(_newAnimNameBuf, 0, sizeof(_newAnimNameBuf));
    memset(_statusMsg,      0, sizeof(_statusMsg));
    strncpy(_xorKeyHex, "53505230", sizeof(_xorKeyHex));
    _xorKey[0]=0x53; _xorKey[1]=0x50; _xorKey[2]=0x52; _xorKey[3]=0x30;
}

SprEditor::~SprEditor()
{
    for (auto& f : _frames)
        if (f.textureID) glDeleteTextures(1, &f.textureID);
}

// ============================================================
// ParseXorKey / MakeBuildOptions
// ============================================================

void SprEditor::ParseXorKey()
{
    char hex[9] = {}; strncpy(hex, _xorKeyHex, 8);
    unsigned int v = (unsigned int)strtoul(hex, nullptr, 16);
    _xorKey[0] = (uint8_t)(v >> 24);
    _xorKey[1] = (uint8_t)(v >> 16);
    _xorKey[2] = (uint8_t)(v >>  8);
    _xorKey[3] = (uint8_t)(v      );
}

SprBuildOptions SprEditor::MakeBuildOptions() const
{
    SprBuildOptions o{};
    if (_optRLE)        o.compressionFlags |= SPR_COMPRESS_RLE;
    if (_optZlib)       o.compressionFlags |= SPR_COMPRESS_ZLIB;
    if (_optPalette)    o.compressionFlags |= SPR_COMPRESS_PALETTE;
    if (_optAtlas)      o.compressionFlags |= SPR_COMPRESS_ATLAS;
    if (_optSkipAlpha)  o.compressionFlags |= SPR_COMPRESS_SKIP_ALPHA;
    if (_optEncryptXOR) o.encryptionMode    = SPR_ENCRYPT_XOR;
    o.paletteSize  = _paletteSize;
    o.atlasMaxWidth  = _atlasMaxW;
    o.atlasMaxHeight = _atlasMaxH;
    memcpy(o.xorKey, _xorKey, 4);
    return o;
}

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

std::string OpenFileDialog(const char* filter)
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
        return filename;

    return "";
}
#include <shlobj.h>
#include <combaseapi.h> // CoTaskMemFree

std::string OpenFolderDialog()
{
    BROWSEINFOA bi{};
    bi.lpszTitle = "Select Folder";

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";

    char path[MAX_PATH];
    std::string result;

    if (SHGetPathFromIDListA(pidl, path))
        result = path;

    CoTaskMemFree(pidl); // 🔥 FIX LEAK

    return result;
}

std::string SaveFileDialog(const char* filter, const char* defExt)
{
    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST;

    if (GetSaveFileNameA(&ofn))
        return filename;

    return "";
}
bool InputPathWithBrowse(
    const char* label,
    char* buffer,
    size_t bufferSize,
    const char* filter,
    const char* defExt = nullptr)
{
    bool changed = false;

    ImGui::InputText(label, buffer, bufferSize);

    ImGui::SameLine();

    std::string btn = std::string("Browse##") + label;

    if (ImGui::Button(btn.c_str()))
    {
        std::string p;

        if (defExt)
            p = SaveFileDialog(filter, defExt);
        else
            p = OpenFileDialog(filter);

        if (!p.empty())
        {
            strncpy(buffer, p.c_str(), bufferSize);
            buffer[bufferSize - 1] = 0;
            changed = true;
        }
    }

    return changed;
}
// ============================================================
// RenderEditor  (main entry)
// ============================================================
void SprEditor::RenderEditor()
{
    ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_Once);
    ImGuiWindowFlags wf = ImGuiWindowFlags_MenuBar
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Spr Editor", nullptr, wf)) { ImGui::End(); return; }

    // ───────────────── MENU BAR ─────────────────
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Import Folder / Image / GIF")) _showImportDialog = true;
            if (ImGui::MenuItem("Load SPR"))   _showLoadDialog = true;
            if (ImGui::MenuItem("Export SPR")) _showExportDialog = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Clear All"))
            {
                for (auto& f : _frames)
                    if (f.textureID)
                        glDeleteTextures(1, &f.textureID);

                _frames.clear();
                _animations.clear();
                _animationMap.clear();

                _selectedFrame = -1;
                _selectedAnimation = -1;

                Stop();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Add Frame")) _showImportDialog = true;

            if (ImGui::MenuItem("Remove Frame", nullptr, false, _selectedFrame >= 0))
                RemoveFrame(_selectedFrame);

            ImGui::Separator();

            if (ImGui::MenuItem("New Animation"))
                _showNewAnimDialog = true;

            if (ImGui::MenuItem("Remove Animation", nullptr, false, _selectedAnimation >= 0))
            {
                if (_selectedAnimation < (int)_animations.size())
                    RemoveAnimation(_animations[_selectedAnimation].name);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Playback"))
        {
            if (ImGui::MenuItem("Play"))  Play();
            if (ImGui::MenuItem("Pause")) Pause();
            if (ImGui::MenuItem("Stop"))  Stop();
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    // ───────────────── OPEN POPUPS FROM FLAGS ───────────────
    if (_showImportDialog)  { _showImportDialog  = false; ImGui::OpenPopup("Import##dlg"); }
    if (_showLoadDialog)    { _showLoadDialog    = false; ImGui::OpenPopup("Load SPR##dlg"); }
    if (_showExportDialog)  { _showExportDialog  = false; ImGui::OpenPopup("Export SPR##dlg"); }
    if (_showNewAnimDialog) { _showNewAnimDialog = false; ImGui::OpenPopup("New Animation##dlg"); }

        // ── Import popup ────────────────────────────────────────
    static bool useTileset = false;
    static int tileW = 32;
    static int tileH = 32;

    if (ImGui::BeginPopupModal("Import##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Import frames from:");
        ImGui::Spacing();

        // 🔥 Tileset option
        ImGui::Checkbox("Import as Tileset", &useTileset);
        if (useTileset)
        {
            ImGui::InputInt("Tile Width", &tileW);
            ImGui::InputInt("Tile Height", &tileH);
        }

        ImGui::Spacing();

        if (ImGui::Button("Image / GIF file...", ImVec2(220,0)))
        {
            ImGui::CloseCurrentPopup();
            std::string p = OpenFileDialog("Images\0*.png;*.jpg;*.jpeg;*.gif\0All\0*.*\0");

            if (!p.empty())
            {
                std::string ext = fs::path(p).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".gif")
                {
                    ImportGIF(p); // gif vẫn giữ nguyên
                }
                else
                {
                    if (useTileset && tileW > 0 && tileH > 0)
                        AddFramesFromTileset(p, tileW, tileH);
                    else
                        AddFrame(p);
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Folder (all images)...", ImVec2(200,0)))
        {
            ImGui::CloseCurrentPopup();
            std::string p = OpenFolderDialog();

            if (!p.empty())
            {
                if (useTileset && tileW > 0 && tileH > 0)
                {
                    // 👇 mỗi file trong folder coi như 1 tileset
                    for (auto& f : fs::directory_iterator(p))
                    {
                        if (!f.is_regular_file()) continue;

                        std::string ext = f.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                            AddFramesFromTileset(f.path().string(), tileW, tileH);
                    }
                }
                else
                {
                    ImportFolder(p);
                }
            }
        }

        ImGui::Spacing();

        if (ImGui::Button("Cancel", ImVec2(80,0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // ── Load SPR popup ───────────────────────────────────────
    if (ImGui::BeginPopupModal("Load SPR##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Load SPR file:");
        ImGui::Spacing();
        if (ImGui::Button("Browse...", ImVec2(120,0)))
        {
            std::string p = OpenFileDialog("SPR File\0*.spr\0All\0*.*\0");
            if (!p.empty())
            {
                strncpy(_loadPathBuf, p.c_str(), sizeof(_loadPathBuf)-1);
                _loadPathBuf[sizeof(_loadPathBuf)-1] = '\0';
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##loadpath", _loadPathBuf, sizeof(_loadPathBuf));
        ImGui::Spacing();
        bool hasPath = _loadPathBuf[0] != '\0';
        if (!hasPath) ImGui::BeginDisabled();
        if (ImGui::Button("Load", ImVec2(100,0)))
        {
            LoadSPR(_loadPathBuf);
            ImGui::CloseCurrentPopup();
        }
        if (!hasPath) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Export SPR popup ─────────────────────────────────────
    if (ImGui::BeginPopupModal("Export SPR##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Export SPR to:");
        ImGui::Spacing();
        if (ImGui::Button("Browse...", ImVec2(120,0)))
        {
            std::string p = SaveFileDialog("SPR File\0*.spr\0", "spr");
            if (!p.empty())
            {
                strncpy(_exportPathBuf, p.c_str(), sizeof(_exportPathBuf)-1);
                _exportPathBuf[sizeof(_exportPathBuf)-1] = '\0';
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##exportpath", _exportPathBuf, sizeof(_exportPathBuf));
        ImGui::Spacing();
        bool hasPath = _exportPathBuf[0] != '\0' && !_frames.empty();
        if (!hasPath) ImGui::BeginDisabled();
        if (ImGui::Button("Export", ImVec2(100,0)))
        {
            bool ok = ExportSPR(_exportPathBuf);
            snprintf(_statusMsg, sizeof(_statusMsg),
                     ok ? "Exported: %s" : "EXPORT FAILED: %s", _exportPathBuf);
            _statusTimer = 5.f;
            ImGui::CloseCurrentPopup();
        }
        if (!hasPath) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── New Animation popup ──────────────────────────────────
    if (ImGui::BeginPopupModal("New Animation##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Animation name:");
        ImGui::SetNextItemWidth(240);
        ImGui::InputText("##animname", _newAnimNameBuf, sizeof(_newAnimNameBuf));
        ImGui::Spacing();
        bool hasName = _newAnimNameBuf[0] != '\0';
        if (!hasName) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100,0)))
        {
            std::string n = _newAnimNameBuf;
            if (!n.empty() && !GetAnimation(n))
                CreateAnimation(n);
            _newAnimNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (!hasName) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80,0)))
        {
            _newAnimNameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ───────────────── MAIN LAYOUT ─────────────────
    ImGui::Columns(3, "sprcols", true);

    RenderFrameList();

    ImGui::NextColumn();

    RenderAnimationList();

    ImGui::NextColumn();

    float h = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("Preview", ImVec2(0, h * 0.5f), true);
    RenderPreview();
    ImGui::EndChild();

    ImGui::BeginChild("Props", ImVec2(0, h * 0.25f), true);
    RenderProperties();
    ImGui::EndChild();

    ImGui::BeginChild("Export", ImVec2(0, 0), true);
    RenderExportOptions();
    ImGui::EndChild();

    ImGui::Columns(1);

    HandleDragDrop();

    float dt = ImGui::GetIO().DeltaTime;

    if (_isPlaying)
        UpdatePlayback(dt);

    // ───────────────── STATUS BAR ─────────────────
    if (_statusTimer > 0.f)
    {
        _statusTimer -= dt;

        bool isErr =
            (strncmp(_statusMsg, "LOAD FAIL", 9) == 0) ||
            (strncmp(_statusMsg, "EXPORT FAIL", 11) == 0);

        ImVec4 col = isErr ?
            ImVec4(1.f, .4f, .4f, 1.f) :
            ImVec4(.4f, 1.f, .4f, 1.f);

        ImGui::TextColored(col, "%s", _statusMsg);
    }
    ImGui::End();
}

// ============================================================
// Drag & Drop
// ============================================================

void SprEditor::HandleDragDrop()
{
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("DND_FILE_PATH"))
        {
            std::string path((const char*)p->Data);
            if (fs::is_directory(path)) ImportFolder(path);
            else {
                std::string ext = fs::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".gif") ImportGIF(path);
                else               AddFrame(path);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

// ============================================================
// Import helpers
// ============================================================

bool SprEditor::ImportFolder(const std::string& folderPath)
{
    try
    {
        std::vector<fs::path> paths;
        for (const auto& e : fs::directory_iterator(folderPath))
        {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp")
                paths.push_back(e.path());
        }
        std::sort(paths.begin(), paths.end());
        for (auto& p : paths) AddFrame(p.string());
        snprintf(_statusMsg, sizeof(_statusMsg),
                 "Imported %d frames from folder", (int)paths.size());
        _statusTimer = 3.f;
        return true;
    }
    catch (const std::exception& e)
    { std::cerr << "ImportFolder: " << e.what() << "\n"; return false; }
}

bool SprEditor::ImportGIF(const std::string& gifPath)
{
    FILE* f = fopen(gifPath.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); int fsize = (int)ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf((size_t)fsize);
    fread(buf.data(), 1, fsize, f); fclose(f);

    int* delays = nullptr;
    int w, h, frameCount, comp;
    unsigned char* data = stbi_load_gif_from_memory(
        buf.data(), fsize, &delays, &w, &h, &frameCount, &comp, 4);
    if (!data) return false;

    std::string stem = fs::path(gifPath).stem().string();
    int prevCount = (int)_frames.size();

    for (int i = 0; i < frameCount; i++)
    {
        SprFrame fr;
        fr.name      = stem + "_" + std::to_string(i);
        fr.sourcePath = gifPath;
        fr.width = w; fr.height = h;
        fr.offsetX = fr.offsetY = 0;
        fr.duration = (delays && delays[i] > 0) ? delays[i]*10 : (1000/_fps);
        fr.metadata = {};
        unsigned char* src = data + (size_t)w*h*4*i;
        fr.pixels.assign(src, src + (size_t)w*h*4);

        glGenTextures(1, &fr.textureID);
        glBindTexture(GL_TEXTURE_2D, fr.textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fr.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        _frames.push_back(std::move(fr));
    }
    free(data);
    if (delays) free(delays);

    if (!GetAnimation(stem))
    {
        SprAnimation* anim = CreateAnimation(stem);
        for (int i = prevCount; i < (int)_frames.size(); i++)
            anim->frameIndices.push_back(i);
    }
    _selectedFrame = (int)_frames.size() - 1;
    snprintf(_statusMsg, sizeof(_statusMsg), "GIF imported: %d frames", frameCount);
    _statusTimer = 3.f;
    return true;
}

void SprEditor::AddFrame(const std::string& imagePath)
{
    int w=0,h=0,ch=0;
    unsigned char* data = stbi_load(imagePath.c_str(), &w, &h, &ch, 4);
    if (!data) { std::cerr << "stbi_load failed: " << imagePath << "\n"; return; }

    SprFrame fr;
    fr.name      = fs::path(imagePath).filename().string();
    fr.sourcePath = imagePath;
    fr.width=w; fr.height=h;
    fr.offsetX=fr.offsetY=0;
    fr.duration = (_fps > 0) ? 1000/_fps : 33;
    fr.metadata = {};
    fr.pixels.assign(data, data + (size_t)w*h*4);
    stbi_image_free(data);

    glGenTextures(1, &fr.textureID);
    glBindTexture(GL_TEXTURE_2D, fr.textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fr.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    _frames.push_back(std::move(fr));
    _selectedFrame = (int)_frames.size() - 1;
}

void SprEditor::AddFramesFromTileset(const std::string& imagePath, int tileW, int tileH)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(imagePath.c_str(), &w, &h, &ch, 4);
    if (!data)
    {
        std::cerr << "stbi_load failed: " << imagePath << "\n";
        return;
    }

    int cols = w / tileW;
    int rows = h / tileH;

    for (int y = 0; y < rows; y++)
    {
        for (int x = 0; x < cols; x++)
        {
            SprFrame fr;
            fr.name = fs::path(imagePath).filename().string() +
                "_" + std::to_string(y) + "_" + std::to_string(x);
            fr.sourcePath = imagePath;
            fr.width = tileW;
            fr.height = tileH;
            fr.offsetX = fr.offsetY = 0;
            fr.duration = (_fps > 0) ? 1000 / _fps : 33;
            fr.metadata = {};

            // copy pixel từng tile
            fr.pixels.resize((size_t)tileW * tileH * 4);

            for (int ty = 0; ty < tileH; ty++)
            {
                for (int tx = 0; tx < tileW; tx++)
                {
                    int srcX = x * tileW + tx;
                    int srcY = y * tileH + ty;

                    size_t srcIdx = ((size_t)srcY * w + srcX) * 4;
                    size_t dstIdx = ((size_t)ty * tileW + tx) * 4;

                    memcpy(&fr.pixels[dstIdx], &data[srcIdx], 4);
                }
            }

            // tạo texture
            glGenTextures(1, &fr.textureID);
            glBindTexture(GL_TEXTURE_2D, fr.textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tileW, tileH, 0, GL_RGBA, GL_UNSIGNED_BYTE, fr.pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

            _frames.push_back(std::move(fr));
        }
    }

    stbi_image_free(data);

    if (!_frames.empty())
        _selectedFrame = (int)_frames.size() - 1;
}

// ============================================================
// Frame management
// ============================================================

void SprEditor::RemoveFrame(int idx)
{
    if (idx<0||idx>=(int)_frames.size()) return;
    if (_frames[idx].textureID) glDeleteTextures(1, &_frames[idx].textureID);
    _frames.erase(_frames.begin()+idx);
    if (_selectedFrame>=(int)_frames.size())
        _selectedFrame=(int)_frames.size()-1;
    for (auto& anim : _animations)
        for (int i=(int)anim.frameIndices.size()-1; i>=0; --i)
        {
            if (anim.frameIndices[i]==idx)
                anim.frameIndices.erase(anim.frameIndices.begin()+i);
            else if (anim.frameIndices[i]>idx)
                anim.frameIndices[i]--;
        }
}

void SprEditor::MoveFrame(int from, int to)
{
    if (from==to||from<0||to<0||
        from>=(int)_frames.size()||to>=(int)_frames.size()) return;
    SprFrame tmp = _frames[from];
    _frames.erase(_frames.begin()+from);
    _frames.insert(_frames.begin()+to, tmp);
    if (_selectedFrame==from) _selectedFrame=to;
}

// ============================================================
// Animation management
// ============================================================

SprAnimation* SprEditor::CreateAnimation(const std::string& name)
{
    SprAnimation a; a.name=name; a.fps=_fps; a.loop=_isLooping;
    _animations.push_back(a);
    _animationMap[name]=(int)_animations.size()-1;
    return &_animations.back();
}

SprAnimation* SprEditor::GetAnimation(const std::string& name)
{
    auto it=_animationMap.find(name);
    return (it!=_animationMap.end()) ? &_animations[it->second] : nullptr;
}

void SprEditor::RemoveAnimation(const std::string& name)
{
    auto it=_animationMap.find(name);
    if (it==_animationMap.end()) return;
    int idx=it->second;
    _animations.erase(_animations.begin()+idx);
    _animationMap.clear();
    for (int i=0;i<(int)_animations.size();i++)
        _animationMap[_animations[i].name]=i;
    if (_selectedAnimation>=(int)_animations.size())
        _selectedAnimation=(int)_animations.size()-1;
}

void SprEditor::AddFrameToAnimation(const std::string& n, int fi)
{
    SprAnimation* a=GetAnimation(n);
    if (a&&fi>=0&&fi<(int)_frames.size()) a->frameIndices.push_back(fi);
}

void SprEditor::RemoveFrameFromAnimation(const std::string& n, int fi)
{
    SprAnimation* a=GetAnimation(n);
    if (!a) return;
    auto it=std::find(a->frameIndices.begin(),a->frameIndices.end(),fi);
    if (it!=a->frameIndices.end()) a->frameIndices.erase(it);
}

void SprEditor::SetFrameMetadata(int idx, const SprFrameMeta& m)
{ if (idx>=0&&idx<(int)_frames.size()) _frames[idx].metadata=m; }

void SprEditor::SetPivotPoint(int x, int y) { _pivotX=x; _pivotY=y; }

// ============================================================
// LoadSPR
// ============================================================

bool SprEditor::LoadSPR(const std::string& sprPath)
{
    SprReader reader;
    SprLoadedData data;
    const uint8_t* key = _optEncryptXOR ? _xorKey : nullptr;

    if (!reader.LoadFromFile(sprPath, data, key))
    {
        snprintf(_statusMsg, sizeof(_statusMsg), "LOAD FAILED: %s", sprPath.c_str());
        _statusTimer = 4.f;
        return false;
    }

    for (auto& fr : _frames)
        if (fr.textureID) glDeleteTextures(1, &fr.textureID);
    _frames.clear(); _animations.clear(); _animationMap.clear();
    _selectedFrame=_selectedAnimation=-1; Stop();

    _pivotX=data.header.pivotX; _pivotY=data.header.pivotY;
    _optRLE        = (data.header.compression & SPR_COMPRESS_RLE)        != 0;
    _optZlib       = (data.header.compression & SPR_COMPRESS_ZLIB)       != 0;
    _optPalette    = (data.header.compression & SPR_COMPRESS_PALETTE)    != 0;
    _optAtlas      = (data.header.compression & SPR_COMPRESS_ATLAS)      != 0;
    _optSkipAlpha  = (data.header.compression & SPR_COMPRESS_SKIP_ALPHA) != 0;
    _optEncryptXOR = (data.header.encryption  == SPR_ENCRYPT_XOR);

    std::string stem = fs::path(sprPath).stem().string();

    for (int i=0; i<(int)data.frames.size(); i++)
    {
        const SprLoadedFrame& lf = data.frames[i];
        SprFrame fr{};
        fr.name=stem+"_"+std::to_string(i);
        fr.sourcePath=sprPath;
        fr.width=lf.width; fr.height=lf.height;
        fr.offsetX=lf.offsetX; fr.offsetY=lf.offsetY;
        fr.duration=lf.duration;
        fr.metadata=lf.metadata;
        fr.pixels=lf.pixels;

        if (!fr.pixels.empty()&&fr.width>0&&fr.height>0)
        {
            glGenTextures(1, &fr.textureID);
            glBindTexture(GL_TEXTURE_2D, fr.textureID);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,fr.width,fr.height,0,
                         GL_RGBA,GL_UNSIGNED_BYTE,fr.pixels.data());
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
        }
        _frames.push_back(std::move(fr));
    }

    for (const SprLoadedAnim& la : data.animations)
    {
        SprAnimation anim;
        anim.name=la.name; anim.fps=la.fps>0?la.fps:_fps; anim.loop=la.loop!=0;
        for (int i=0;i<(int)la.frameCount;i++)
        {
            int fi=(int)la.startFrame+i;
            if (fi<(int)_frames.size()) anim.frameIndices.push_back(fi);
        }
        _animationMap[anim.name]=(int)_animations.size();
        _animations.push_back(std::move(anim));
    }

    if (!_frames.empty())     _selectedFrame=0;
    if (!_animations.empty()) { _selectedAnimation=0; _fps=_animations[0].fps; }

    snprintf(_statusMsg, sizeof(_statusMsg),
             "Loaded: %s  (%d frames, %d animations)",
             fs::path(sprPath).filename().string().c_str(),
             (int)_frames.size(), (int)_animations.size());
    _statusTimer=5.f;
    return true;
}

// ============================================================
// ExportSPR
// ============================================================

bool SprEditor::ExportSPR(const std::string& outputPath)
{
    if (_frames.empty()) return false;

    std::vector<SprFrameData> frameData;
    for (const SprFrame& f : _frames)
    {
        if (f.pixels.empty()) continue;
        SprFrameData fd{};
        fd.width=f.width; fd.height=f.height;
        fd.offsetX=f.offsetX; fd.offsetY=f.offsetY;
        fd.duration=f.duration; fd.metadata=f.metadata;
        fd.pixels=f.pixels.data();
        frameData.push_back(fd);
    }
    if (frameData.empty()) return false;

    std::vector<SprAnimData> animData;
    for (const SprAnimation& a : _animations)
    {
        if (a.frameIndices.empty()) continue;
        SprAnimData ad{};
        strncpy(ad.name, a.name.c_str(), 31); ad.name[31]='\0';
        ad.startFrame=(uint16_t)a.frameIndices.front();
        ad.frameCount=(uint16_t)a.frameIndices.size();
        ad.fps=(uint16_t)(a.fps>0?a.fps:_fps);
        ad.loop=a.loop?1:0;
        animData.push_back(ad);
    }

    fs::path outDir=fs::path(outputPath).parent_path();
    if (!outDir.empty()) { std::error_code ec; fs::create_directories(outDir,ec); }

    SprBuildOptions opts=MakeBuildOptions();
    SprWriter writer;
    return writer.BuildFromMemory(frameData, animData, outputPath,
                                  _pivotX, _pivotY, _fps>0?_fps:30, opts);
}

// ============================================================
// Playback
// ============================================================

void SprEditor::SetCurrentAnimation(const std::string& name)
{ _currentAnimation=name; _currentFrameIndex=0; _frameTimer=0.f; }

void SprEditor::Play()  { _isPlaying=true; }
void SprEditor::Pause() { _isPlaying=false; }
void SprEditor::Stop()  { _isPlaying=false; _currentFrameIndex=0; _frameTimer=0.f; }

void SprEditor::SetFPS(int fps)
{ _fps=fps; for (auto& a:_animations) a.fps=fps; }

void SprEditor::SetLoop(bool loop)
{ _isLooping=loop; for (auto& a:_animations) a.loop=loop; }

void SprEditor::UpdatePlayback(float dt)
{
    if (_currentAnimation.empty()) return;
    SprAnimation* anim=GetAnimation(_currentAnimation);
    if (!anim||anim->frameIndices.empty()) return;

    _frameTimer+=dt;
    float ft=1.f/(float)(anim->fps>0?anim->fps:30);
    if (_frameTimer>=ft)
    {
        _frameTimer-=ft;
        _currentFrameIndex++;
        if (_currentFrameIndex>=(int)anim->frameIndices.size())
        {
            if (anim->loop) _currentFrameIndex=0;
            else { _isPlaying=false; _currentFrameIndex=(int)anim->frameIndices.size()-1; }
        }
    }
}

// ============================================================
// LoadTextureFromFile  (utility)
// ============================================================

unsigned int SprEditor::LoadTextureFromFile(const std::string& path, int& outW, int& outH)
{
    int ch;
    unsigned char* data=stbi_load(path.c_str(),&outW,&outH,&ch,4);
    if (!data) { outW=outH=0; return 0; }
    unsigned int id=0;
    glGenTextures(1,&id);
    glBindTexture(GL_TEXTURE_2D,id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,outW,outH,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
    stbi_image_free(data);
    return id;
}

void SprEditor::UpdateFrameThumbnails() {}

// ============================================================
// RenderFrameList
// ============================================================

void SprEditor::RenderFrameList()
{
    ImGui::BeginChild("FrameList", ImVec2(0,0), true);
    ImGui::Text("Frames (%d)", (int)_frames.size());
    ImGui::Separator();

    if (ImGui::Button("Add##fr"))   _showImportDialog=true;
    ImGui::SameLine();
    if (ImGui::Button("Remove##fr") && _selectedFrame>=0) RemoveFrame(_selectedFrame);
    ImGui::SameLine();
    if (ImGui::Button("Up##fr") && _selectedFrame>0)
        MoveFrame(_selectedFrame, _selectedFrame-1);
    ImGui::SameLine();
    if (ImGui::Button("Down##fr") && _selectedFrame>=0 &&
        _selectedFrame<(int)_frames.size()-1)
        MoveFrame(_selectedFrame, _selectedFrame+1);

    ImGui::Separator();

    for (int i=0; i<(int)_frames.size(); i++)
    {
        ImGui::PushID(i);
        if (_frames[i].textureID)
        {
            ImGui::Image((ImTextureID)(uintptr_t)_frames[i].textureID, ImVec2(32,32));
            ImGui::SameLine();
        }
        char label[64];
        snprintf(label, sizeof(label), "[%d] %s", i, _frames[i].name.c_str());
        if (ImGui::Selectable(label, _selectedFrame==i, 0, ImVec2(0,32)))
            _selectedFrame=i;

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("SPR_FRAME",&i,sizeof(int));
            ImGui::Text("%s",_frames[i].name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ============================================================
// RenderAnimationList
// ============================================================

void SprEditor::RenderAnimationList()
{
    ImGui::BeginChild("AnimList", ImVec2(0,0), true);
    ImGui::Text("Animations (%d)", (int)_animations.size());
    ImGui::Separator();

    if (ImGui::Button("New##an"))   _showNewAnimDialog=true;
    ImGui::SameLine();
    if (ImGui::Button("Remove##an") && _selectedAnimation>=0)
    {
        RemoveAnimation(_animations[_selectedAnimation].name);
        ImGui::EndChild(); return;
    }
    ImGui::Separator();

    for (int i=0; i<(int)_animations.size(); i++)
    {
        ImGui::PushID(i);
        SprAnimation& anim=_animations[i];

        if (ImGui::Selectable(anim.name.c_str(), _selectedAnimation==i))
        { _selectedAnimation=i; SetCurrentAnimation(anim.name); }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p=ImGui::AcceptDragDropPayload("SPR_FRAME"))
                AddFrameToAnimation(anim.name, *(const int*)p->Data);
            ImGui::EndDragDropTarget();
        }

        if (_selectedAnimation==i)
        {
            ImGui::Indent();
            int afps=anim.fps;
            if (ImGui::InputInt("FPS##an",&afps) && afps>0) anim.fps=afps;
            bool al=anim.loop;
            if (ImGui::Checkbox("Loop##an",&al)) anim.loop=al;
            ImGui::Text("Frame count: %d",(int)anim.frameIndices.size());

            for (int j=0; j<(int)anim.frameIndices.size(); j++)
            {
                int fi=anim.frameIndices[j];
                const char* fname=(fi<(int)_frames.size())?_frames[fi].name.c_str():"?";
                ImGui::Text("  %d: [%d] %s", j, fi, fname);
                ImGui::SameLine();
                ImGui::PushID(j);
                if (ImGui::SmallButton("x")) RemoveFrameFromAnimation(anim.name,fi);
                ImGui::PopID();
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ============================================================
// RenderPreview
// ============================================================

void SprEditor::RenderPreview()
{
    // Determine displayed frame index
    int showFrame = _selectedFrame;
    if (_isPlaying && _selectedAnimation>=0 && _selectedAnimation<(int)_animations.size())
    {
        const SprAnimation& anim=_animations[_selectedAnimation];
        if (!anim.frameIndices.empty())
            showFrame=anim.frameIndices[_currentFrameIndex%(int)anim.frameIndices.size()];
    }

    ImGui::Text("Preview");
    ImGui::Separator();

    // Playback controls
    if (ImGui::Button("Play"))  Play();  ImGui::SameLine();
    if (ImGui::Button("Pause")) Pause(); ImGui::SameLine();
    if (ImGui::Button("Stop"))  Stop();  ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    ImGui::SliderFloat("Scale##pv", &_previewScale, 0.1f, 8.0f);

    // Pivot edit (global)
    ImGui::Text("Pivot:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60); ImGui::InputInt("PX##pv",&_pivotX);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60); ImGui::InputInt("PY##pv",&_pivotY);

    // Overlay toggles
    ImGui::Checkbox("Pivot##ov",    &_showPivot);    ImGui::SameLine();
    ImGui::Checkbox("Hitbox##ov",   &_showHitbox);   ImGui::SameLine();
    ImGui::Checkbox("Attack##ov",   &_showAttackBox); ImGui::SameLine();
    ImGui::Checkbox("Offset##ov",   &_showOffset);

    ImGui::Separator();

    if (showFrame >= 0 && showFrame < (int)_frames.size())
    {
        const SprFrame& fr = _frames[showFrame];
        float fw = fr.width  * _previewScale;
        float fh = fr.height * _previewScale;

        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Checkerboard background
        {
            const float cs = 8.f;
            dl->AddRectFilled(imgPos, ImVec2(imgPos.x+fw, imgPos.y+fh), IM_COL32(60,60,60,255));
            for (float cy=0; cy<fh; cy+=cs)
                for (float cx=0; cx<fw; cx+=cs)
                    if (((int)(cx/cs)+(int)(cy/cs))%2==0)
                        dl->AddRectFilled(
                            ImVec2(imgPos.x+cx,                imgPos.y+cy),
                            ImVec2(imgPos.x+std::min(cx+cs,fw), imgPos.y+std::min(cy+cs,fh)),
                            IM_COL32(90,90,90,255));
        }

        // Image
        if (fr.textureID)
            ImGui::Image((ImTextureID)(uintptr_t)fr.textureID, ImVec2(fw,fh));
        else
            ImGui::Dummy(ImVec2(fw,fh));

        const SprFrameMeta& m = fr.metadata;

        // Overlay: Hitbox (green)
        if (_showHitbox)
            DrawOverlayRect(dl, imgPos, _previewScale,
                            m.hitboxX, m.hitboxY, m.hitboxWidth, m.hitboxHeight,
                            IM_COL32(0,230,60,240), "HB");

        // Overlay: Attack box (red/orange)
        if (_showAttackBox)
            DrawOverlayRect(dl, imgPos, _previewScale,
                            m.attackBoxX, m.attackBoxY, m.attackBoxWidth, m.attackBoxHeight,
                            IM_COL32(255,70,40,240), "ATK");

        // Overlay: Offset cross (cyan)
        if (_showOffset)
        {
            float ox = imgPos.x + fw*0.5f + fr.offsetX*_previewScale;
            float oy = imgPos.y + fh*0.5f + fr.offsetY*_previewScale;
            float cr = 6.f;
            dl->AddLine(ImVec2(ox-cr,oy), ImVec2(ox+cr,oy), IM_COL32(0,200,255,220), 1.5f);
            dl->AddLine(ImVec2(ox,oy-cr), ImVec2(ox,oy+cr), IM_COL32(0,200,255,220), 1.5f);
            dl->AddText(ImVec2(ox+4,oy-14), IM_COL32(0,200,255,255), "OFS");
        }

        // Overlay: Pivot (yellow cross + bull's-eye)
        if (_showPivot)
        {
            float px = imgPos.x + fw*0.5f + _pivotX*_previewScale;
            float py = imgPos.y + fh*0.5f + _pivotY*_previewScale;
            const float pr=5.f;
            dl->AddCircleFilled(ImVec2(px,py), pr-1.f, IM_COL32(255,0,0,200));
            dl->AddCircle(ImVec2(px,py), pr+1.f, IM_COL32(255,220,0,255), 12, 1.5f);
            dl->AddLine(ImVec2(px-pr*2.5f,py), ImVec2(px+pr*2.5f,py), IM_COL32(255,220,0,180), 1.f);
            dl->AddLine(ImVec2(px,py-pr*2.5f), ImVec2(px,py+pr*2.5f), IM_COL32(255,220,0,180), 1.f);
            dl->AddText(ImVec2(px+7,py-14), IM_COL32(255,220,0,255), "PVT");
        }

        ImGui::TextDisabled("[%d] %s  %dx%d  dur=%dms  ofs=(%d,%d)",
                    showFrame, fr.name.c_str(),
                    fr.width, fr.height, fr.duration,
                    fr.offsetX, fr.offsetY);
    }
    else
    {
        ImGui::TextDisabled("(no frame selected)");
    }
}

// ============================================================
// RenderProperties
// ============================================================

void SprEditor::RenderProperties()
{
    ImGui::Text("Frame Properties");
    ImGui::Separator();

    const bool hasSel = (_selectedFrame>=0 && _selectedFrame<(int)_frames.size());

    // ── Selected frame ──────────────────────────────────────
    if (hasSel)
    {
        SprFrame& fr = _frames[_selectedFrame];
        ImGui::Text("Name: %s", fr.name.c_str());
        ImGui::Text("Size: %dx%d   Pixels: %zu bytes", fr.width, fr.height, fr.pixels.size());

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f,0.8f,1.f,1.f), "Transform (this frame)");
        IntStepper("Offset X##fp", &fr.offsetX);
        IntStepper("Offset Y##fp", &fr.offsetY);
        IntStepper("Duration ms##fp", &fr.duration, 10);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,1.f,0.5f,1.f), "Hitbox (this frame)");
        int hx=fr.metadata.hitboxX,  hy=fr.metadata.hitboxY;
        int hw=fr.metadata.hitboxWidth, hh=fr.metadata.hitboxHeight;
        if (IntStepper("HB X##fp",&hx)) fr.metadata.hitboxX     =(int16_t)hx;
        if (IntStepper("HB Y##fp",&hy)) fr.metadata.hitboxY     =(int16_t)hy;
        if (IntStepper("HB W##fp",&hw)) fr.metadata.hitboxWidth  =(int16_t)hw;
        if (IntStepper("HB H##fp",&hh)) fr.metadata.hitboxHeight =(int16_t)hh;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.f,0.5f,0.4f,1.f), "Attack Box (this frame)");
        int ax=fr.metadata.attackBoxX,  ay=fr.metadata.attackBoxY;
        int aw=fr.metadata.attackBoxWidth, ah=fr.metadata.attackBoxHeight;
        if (IntStepper("AB X##fp",&ax)) fr.metadata.attackBoxX    =(int16_t)ax;
        if (IntStepper("AB Y##fp",&ay)) fr.metadata.attackBoxY    =(int16_t)ay;
        if (IntStepper("AB W##fp",&aw)) fr.metadata.attackBoxWidth =(int16_t)aw;
        if (IntStepper("AB H##fp",&ah)) fr.metadata.attackBoxHeight=(int16_t)ah;
    }
    else
    {
        ImGui::TextDisabled("(no frame selected)");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "Apply to ALL Frames");

    if (ImGui::BeginTable("bulk_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("X");
        ImGui::TableSetupColumn("Y");
        ImGui::TableSetupColumn("W");
        ImGui::TableSetupColumn("H");
        ImGui::TableHeadersRow();

        // Offset
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Offset");

        ImGui::TableSetColumnIndex(1);
        ImGui::InputInt("##ofsx", &_bulkOffsetX);

        ImGui::TableSetColumnIndex(2);
        ImGui::InputInt("##ofsy", &_bulkOffsetY);

        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button("Apply##ofs"))
            for (auto& f : _frames)
            {
                f.offsetX = _bulkOffsetX;
                f.offsetY = _bulkOffsetY;
            }

        // Hitbox
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Hitbox");

        ImGui::TableSetColumnIndex(1);
        ImGui::InputInt("##hbx", &_bulkHbX);

        ImGui::TableSetColumnIndex(2);
        ImGui::InputInt("##hby", &_bulkHbY);

        ImGui::TableSetColumnIndex(3);
        ImGui::InputInt("##hbw", &_bulkHbW);

        ImGui::TableSetColumnIndex(4);
        ImGui::InputInt("##hbh", &_bulkHbH);

        if (ImGui::Button("Apply##hb"))
            for (auto& f : _frames)
            {
                f.metadata.hitboxX = _bulkHbX;
                f.metadata.hitboxY = _bulkHbY;
                f.metadata.hitboxWidth = _bulkHbW;
                f.metadata.hitboxHeight = _bulkHbH;
            }

        // Attack
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Attack");

        ImGui::TableSetColumnIndex(1);
        ImGui::InputInt("##abx", &_bulkAbX);

        ImGui::TableSetColumnIndex(2);
        ImGui::InputInt("##aby", &_bulkAbY);

        ImGui::TableSetColumnIndex(3);
        ImGui::InputInt("##abw", &_bulkAbW);

        ImGui::TableSetColumnIndex(4);
        ImGui::InputInt("##abh", &_bulkAbH);

        if (ImGui::Button("Apply##ab"))
            for (auto& f : _frames)
            {
                f.metadata.attackBoxX = _bulkAbX;
                f.metadata.attackBoxY = _bulkAbY;
                f.metadata.attackBoxWidth = _bulkAbW;
                f.metadata.attackBoxHeight = _bulkAbH;
            }

        ImGui::EndTable();
    }
}

// ============================================================
// RenderExportOptions
// ============================================================

void SprEditor::RenderExportOptions()
{
    ImGui::Text("Export Options");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.4f,0.8f,1.f,1.f), "Compression");
    ImGui::Checkbox("RLE (Run-Length Encoding)",       &_optRLE);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Encode runs of identical pixels.\nBest for solid-color sprite regions.");
    ImGui::Checkbox("ZLIB (Deflate)",                  &_optZlib);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apply zlib deflate after RLE/palette.");
    ImGui::Checkbox("Palette (8-bit indexed, max 256)",&_optPalette);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Quantize RGBA8 to 1-byte-per-pixel palette.\n4x smaller pixel data (good for pixel-art).");
    if (_optPalette)
    {
        ImGui::Indent();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Palette size##eo",&_paletteSize);
        _paletteSize=std::clamp(_paletteSize,2,256);
        ImGui::Unindent();
    }
    ImGui::Checkbox("Texture Atlas (sheet packing)",   &_optAtlas);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pack all frames into one texture atlas.");
    if (_optAtlas)
    {
        ImGui::Indent();
        ImGui::SetNextItemWidth(80); ImGui::InputInt("Max W##eo",&_atlasMaxW); ImGui::SameLine();
        ImGui::SetNextItemWidth(80); ImGui::InputInt("Max H##eo",&_atlasMaxH);
        _atlasMaxW=std::max(_atlasMaxW,64);
        _atlasMaxH=std::max(_atlasMaxH,64);
        ImGui::Unindent();
    }
    ImGui::Checkbox("Skip Transparent (alpha=0)",      &_optSkipAlpha);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("RLE: encode fully-transparent runs as compact skip tokens.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.f,0.7f,0.2f,1.f), "Encryption");
    ImGui::Checkbox("XOR encrypt pixel data",          &_optEncryptXOR);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("XOR each compressed byte with a 4-byte rolling key.");
    if (_optEncryptXOR)
    {
        ImGui::Indent();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("Key (8 hex)##eo",_xorKeyHex,sizeof(_xorKeyHex)))
            ParseXorKey();
        ImGui::SameLine();
        ImGui::TextDisabled("= %02X %02X %02X %02X",
                            _xorKey[0],_xorKey[1],_xorKey[2],_xorKey[3]);
        if (ImGui::Button("Reset##eo")) { strncpy(_xorKeyHex,"53505230",sizeof(_xorKeyHex)); ParseXorKey(); }
        ImGui::Unindent();
    }

    // Summary
    ImGui::Separator();
    std::string summary;
    if (_optRLE)        summary+="[RLE] ";
    if (_optZlib)       summary+="[ZLIB] ";
    if (_optPalette)    summary+="[PAL-"+std::to_string(_paletteSize)+"] ";
    if (_optAtlas)      summary+="[ATLAS] ";
    if (_optSkipAlpha)  summary+="[SKIP_A] ";
    if (_optEncryptXOR) summary+="[XOR] ";
    if (summary.empty()) summary="(none)";
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.f),"Active: %s",summary.c_str());

    // Quick export bar
    ImGui::Separator();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##qexport",_exportPathBuf,sizeof(_exportPathBuf));
    ImGui::SameLine();
    bool hasFrames=!_frames.empty();
    if (!hasFrames) ImGui::BeginDisabled();
    if (ImGui::Button("Export##qeo",ImVec2(-1,0)))
    {
        std::string p=_exportPathBuf;
        if (!p.empty())
        {
            bool ok=ExportSPR(p);
            snprintf(_statusMsg,sizeof(_statusMsg),
                     ok ? "Exported: %s" : "EXPORT FAILED: %s",p.c_str());
            _statusTimer=5.f;
        }
    }
    if (!hasFrames) ImGui::EndDisabled();
}
