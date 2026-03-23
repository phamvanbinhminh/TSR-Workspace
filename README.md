🎮 Overview
TSR Workspace là một mono-repo tích hợp:

Component	Mục đích
DaiGiangVoTan	Game client logic
SEngine	Core engine (rendering, ECS, resources)
Tools	Asset editors & pipeline tools
Pipeline: Assets → Tools → .pak files → Game Runtime

📂 Project Structure
Copy
TSR-Workspace/
├── DaiGiangVoTan/          # Game Client
│   ├── src/
│   ├── assets/
│   └── CMakeLists.txt
├── SEngine/                # Core Engine
│   ├── src/
│   ├── include/
│   └── CMakeLists.txt
├── Tools/                  # Development Tools
│   ├── MapEditor/
│   ├── SPREditor/
│   ├── PAKEditor/
│   └── CMakeLists.txt
├── CMakeLists.txt          # Root config
├── README.md
└── .gitignore
🔧 Components
DaiGiangVoTan (Game Client)
Gameplay logic & mechanics
Loads .pak resource bundles
Renders maps, entities, animations
Built on SEngine
SEngine (Engine)
Rendering: OpenGL backend
ECS: Entity Component System
Resources: Asset management & streaming
Animation: Sprite & sequence playback
Maps: Region-based grid system
Input: GLFW input handling
Tools (Pipeline)
Map Editor: Design level layouts
SPR Editor: Create sprites & animations
PAK Editor: Bundle & compress resources
🚀 Build
Requirements
Copy
C++17, CMake 3.16+, OpenGL 4.1+
GLFW, ImGui
Build Steps
bash
Copy
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)    # Linux/macOS
# or: cmake --build . --config Release  (Windows)
IDE Support
VS2022 + CMake
VSCode + CMake Tools extension
CLion
📊 Resource Pipeline
Copy
Raw Assets (PNG, JSON, etc)
    ↓
Editors (MapEditor, SPREditor)
    ↓
Processed Data
    ↓
PAK Packer
    ↓
.pak Files (Binary)
    ↓
Game Runtime (load via ResourceManager)
🗺️ Map System
Region-Based Architecture:

Copy
World = Grid of Regions
Each Region = Fixed Size (UnitSize × UnitSize)

Separation:
├── region_S (Server)
│   ├── Collision data
│   └── Logic/gameplay
└── region_C (Client)
    ├── Sprites & textures
    └── Render assets
Benefits:

Efficient streaming (load/unload by camera)
Separation of concerns
Scalable for large worlds
🏗️ Architecture Highlights
Modular Design
SEngine = Reusable core
DaiGiangVoTan = Game-specific layer
Tools = Development pipeline
Data-Driven
ECS for flexible entity behavior
Configuration files for gameplay
Hot-reloadable assets
Extensible
Add new ECS systems easily
Support multiple resource formats
Custom tool integration
📋 Roadmap
Engine
 Complete ECS implementation
 Region streaming optimization
 Advanced animation blending
Tools
 Drag & drop asset placement
 Undo/Redo system
 Integrated resource browser
Game
 Gameplay systems (combat, movement)
 AI & NPC behavior
 UI framework
🔮 Future Improvements
Modularization

Extract SEngine to separate repo (git submodule)
Reuse across multiple game projects
CI/CD

Automated builds on commit
Unit tests framework
Asset validation pipeline
Performance

Binary resource format + compression
Incremental streaming
Memory pooling
Developer Experience

Build scripts (shell/batch)
Hot-reload during development
Debug visualization tools
📝 Development Notes
Language: C++17
Rendering: OpenGL + GLFW
UI: ImGui (for tools)
Architecture: ECS + Component-based
Target: 2D Game (tile-based or sprite-based)

👤 Author
Pham Van Binh Minh
