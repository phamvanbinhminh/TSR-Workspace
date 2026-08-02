# TSR Workspace

TSR Workspace là mono-repo chứa toàn bộ hệ thống phát triển game, bao gồm:

- Game client  
- Game engine  
- Tool chỉnh sửa resource  

Mục tiêu là cung cấp một pipeline hoàn chỉnh từ tạo asset → build → runtime.

---

## Project Structure

'''
TSR-Workspace/
├── DaiGiangVoTan/ # Game client
├── SEngine/ # Core engine (rendering, ECS, resource...)
├── Tools/ # Toolchain (editor, packer, etc)
├── CMakeLists.txt # Root build config
└── .gitignore
'''

---

## Components

### DaiGiangVoTan (Game Client)

- Chứa logic game  
- Sử dụng SEngine làm nền tảng  
- Load resource từ `.pak`  
- Render map, entity, animation  

---

### SEngine (Engine)

Core engine chịu trách nhiệm:

- Rendering (OpenGL)  
- Resource management  
- ECS (Entity Component System)  
- Animation system  
- Map & region streaming  
- Input handling  

---

### Tools

Các tool hỗ trợ development:

- Map Editor  
- SPR Editor (sprite/animation)  
- PAK Editor (đóng gói resource)  

Dùng để tạo dữ liệu cho game runtime.

---

## Build

### Requirements

- C++17  
- CMake  
- OpenGL  
- GLFW  
- ImGui  

---

### Build Steps

```bash
mkdir build
cd build
cmake ..
make
```

(Có thể dùng toolchain VS2022 + VSCode để build)

Resource Pipeline

Assets (images, data)
↓
Tools (SPR / MAP / PAK)
↓
.pak files
↓
Game Client (runtime load)

Map System
Map được chia thành grid region
Mỗi region có kích thước cố định (UnitSize x UnitSize)
Server và Client sử dụng dữ liệu riêng:
region_S: logic, collision
region_C: render, asset
Development Notes
Kiến trúc tách biệt:
Engine (SEngine)
Game (DaiGiangVoTan)
Tools
Dễ mở rộng:
thêm system mới vào engine
thêm format resource
Hỗ trợ phát triển game 2D theo hướng data-driven
Roadmap
Engine
ECS hoàn chỉnh
Resource streaming theo region
Animation system tối ưu
Tools
Drag & drop asset
Undo / Redo
Resource browser
Game
Gameplay systems
AI / NPC
UI
Future Improvements
Tách SEngine thành repo riêng (submodule)
CI/CD build tự động
Binary resource format + compression
Streaming world (load/unload region theo camera)
License

Private project.

Author

Pham Van Binh Minh
