# ResManager GUI Tool

Được tạo từ imgui + engine để tạo, chỉnh sửa Map, Spr, Pak files.

## Cấu trúc dự án

```
ResManagerGUITool/
├── Core/
│   └── main.cpp              # Main application entry point
├── MapEdit/
│   ├── MapEditor.h           # Map editor header
│   └── MapEditor.cpp         # Map editor implementation
├── PakEdit/
│   ├── PakEditor.h           # PAK editor header
│   └── PakEditor.cpp         # PAK editor implementation
├── SprEdit/
│   ├── SprEditor.h           # SPR editor header
│   └── SprEditor.cpp         # SPR editor implementation
└── ReadMe.md                 # This file
```

## Tính năng

### Map Editor
- Tạo, tải, lưu file map
- Chỉnh sửa kích thước map (width, height, unitSize)
- Quản lý regions (khu vực)
- Thêm, xóa, sửa đổi vật cản (obstacles) trong regions
- Hiển thị lưới preview
- Chọn và thao tác với từng region

### PAK Editor
- Tạo, tải, lưu file PAK
- Quản lý file trong PAK (thêm, xóa, trích xuất)
- Pack folder vào PAK
- Unpack PAK ra folder
- Tìm kiếm và lọc file
- Cài đặt nén và mã hóa
- Quản lý lựa chọn file (chọn tất cả, đảo ngược lựa chọn)

### SPR Editor
- Tạo, tải, lưu file SPR
- Quản lý frames (khung hình)
- Quản lý animations (hoạt ảnh)
- Chỉnh sửa metadata (hitbox, attack box)
- Thiết lập pivot point
- Preview frames và animations
- Build từ folder ảnh

## Cách sử dụng

1. **Chạy ứng dụng**: 
   ```bash
   ./ResManagerGUITool
   ```

2. **Chọn editor**:
   - Vào menu "Editors" để chọn Map Editor, PAK Editor, hoặc SPR Editor

3. **Map Editor**:
   - File → New Map / Load Map / Save Map
   - Chỉnh sửa properties: width, height, unitSize
   - Quản lý regions và obstacles
   - Preview lưới map

4. **PAK Editor**:
   - File → New PAK / Load PAK / Save PAK
   - Quản lý file: Add File, Add Folder, Remove, Extract
   - Pack/Unpack folders
   - Tìm kiếm và lọc file

5. **SPR Editor**:
   - File → New SPR / Load SPR / Save SPR
   - Quản lý frames và animations
   - Chỉnh sửa metadata cho gameplay
   - Preview frames và animations
   - Build từ folder ảnh

## Yêu cầu hệ thống

- C++17 compatible compiler
- GLFW
- OpenGL
- ImGui
- CMake (để build)

## Build

```bash
mkdir build
cd build
cmake ..
make
```

## Ghi chú phát triển

- Các editor được thiết kế để có thể mở rộng và tùy chỉnh
- Sử dụng ImGui cho giao diện người dùng
- Hỗ trợ drag & drop (có thể thêm sau)
- Có thể thêm các định dạng file khác trong tương lai



## __KẾ HOẠCH THỰC HIỆN FULL FEATURE__

### __Phase 1: SprEditor - Drag & Drop & GIF Support (2-3 ngày)__

__1. Drag & Drop Folder Import:__

- Thêm `ImGui::BeginDragDropTarget()` để nhận folder
- Scan toàn bộ ảnh trong folder (PNG, JPG, BMP)
- Tự động tạo frame cho từng ảnh
- Preview thumbnail cho từng frame

__2. GIF Import System:__

- Sử dụng `stb_image` hoặc `giflib` để parse GIF
- Tách từng frame từ GIF animation
- Tự động detect FPS từ GIF
- Preview animation trong editor

__3. Frame Management:__

- Grid view hiển thị tất cả frame
- Drag & drop để sắp xếp frame
- Context menu: Delete, Duplicate, Properties
- Preview frame lớn ở bên phải

__4. Animation Editor:__

- Tạo multiple animations từ cùng set frame
- Thiết lập FPS cho từng animation
- Preview animation với loop
- Export SPR file hoàn chỉnh

### __Phase 2: MapEditor - TitleSet & Region System (3-4 ngày)__

__1. TitleSet Manager:__

- Danh sách các SPR file có thể dùng
- Preview thumbnail cho từng SPR
- Filter theo tên, loại sprite
- Drag & drop SPR vào region

__2. Region Editor:__

- Vẽ region trên map grid (click & drag)
- Edit obstacle bằng polygon tool
- Layer management: Move sprite lên/xuống
- Assign sprite từ TitleSet vào region

__3. Sprite Properties:__

- Position trong region (X, Y)
- Rotation, Scale
- Animation selection
- Layer assignment

__4. Map Features:__

- Grid editor với kích thước tùy chỉnh
- Preview region với sprite thực
- Save/Load map hoàn chỉnh

### __Phase 3: Advanced Features (2-3 ngày)__

__1. Resource Browser:__

- Tree view cho folders
- Preview resource trước khi import
- Batch operations

__2. Undo/Redo System:__

- Command pattern cho các thao tác
- History panel
- Ctrl+Z/Ctrl+Y support

__3. Performance Optimization:__

- Texture caching
- Lazy loading
- Memory management

__4. Polish & UX:__

- Context menus
- Tooltips
- Keyboard shortcuts
- Error handling

## __Implementation Strategy:__

__Ngày 1-2:__ SprEditor drag & drop folder __Ngày 3-4:__ SprEditor GIF support\
__Ngày 5-6:__ SprEditor animation editor __Ngày 7-8:__ MapEditor TitleSet system __Ngày 9-10:__ MapEditor region editor __Ngày 11-12:__ Layer management & sprite assignment __Ngày 13-14:__ Advanced features & polish

## __Công nghệ sử dụng:__

- __ImGui:__ UI framework (đã có)
- __stb_image:__ Image loading (đã có)
- __FreeType:__ Text rendering (đã có)
- __OpenGL:__ Rendering (đã có)
- __C++17:__ Modern C++ features
