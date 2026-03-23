# 📦 Client Resource Structure

```
Client/
├── DGClient.exe
└── res/
    └── paks/
        ├── maps.pak
        ├── script.pak
        └── res.pak
```

---

# 🗺️ maps.pak

Chứa toàn bộ dữ liệu map (tile, object, spawn, navigation)

```
maps.pak/
├── maps/
│   ├── các map
│   │   ├─cấu trúc map trong resmanager
```

---

# 🧠 script.pak

Chứa toàn bộ logic game (Lua)

```
script.pak/
├── core/
│   ├── init.lua              # entry point
│
├── entities/
		tên entities/
			các script
│
├── maps/
│   ├── town01.lua           # logic riêng map
│
├── events/
│   ├── on_hit.lua
│   ├── on_death.lua
│   └── on_interact.lua
│
└── ui/
    ├── hud.lua
    └── inventory.lua
```

---

# 🎨 res.pak

Chứa toàn bộ tài nguyên (render/audio/ui)

```
res.pak/
├── textures/
│   ├── characters/
│   │   ├── player.png
│   │   ├── npc.png
│   │   └── enemies/
│   │       ├── slime.png
│   │       └── goblin.png
│   │
│   ├── environment/
│   │   ├── trees.png
│   │   ├── rocks.png
│   │   └── props.png
│   │
│   └── ui/
│       ├── button.png
│       ├── panel.png
│       └── icons.png
│
├── sprites/
│   ├── nhóm sprite của thực thể/
		tên thực thể/
			các spr của thực thể
│
├── audio/
│   ├── bgm/
│   │   ├── town.mp3
│   │   └── dungeon.mp3
│   │
│   ├── sfx/
│   │   ├── hit.wav
│   │   ├── attack.wav
│   │   └── ui_click.wav
│
├── fonts/
│   ├── arial.ttf
│   └── pixel.ttf
│
└── shaders/
    ├── sprite.hlsl
    └── postprocess.hlsl
```

---

# 🧩 Ghi chú thiết kế

* `.pak` là file nén (custom) → mount vào VFS
* cực kỳ lưu ý script chỉ chứa những logic hành vi hoặc các trigger cơ bản để tránh hack client và đảm bảo hiệu năng
* một lưu ý khác là ở chế độ debug thì ưu tiên load trong thư mục cùng tên trước nếu không có item đó thì load trong pak
---

# 🚀 Gợi ý thêm

* maps.pak → chỉ data map (không chứa texture lớn nếu reuse)
* res.pak → dùng chung toàn game
* script.pak → có thể hot-reload (rất quan trọng dev)

---
