#pragma once

#ifdef SENGINE_EXPORTS
#define SENGINE_API __declspec(dllexport)
#else
#define SENGINE_API __declspec(dllimport)
#endif

// ── Debug disk-first mode ─────────────────────────────────────────────────────
// Khi build Debug (_DEBUG defined by MSVC) và KHÔNG có SENGINE_RELEASE,
// VFS::ReadFile sẽ ưu tiên đọc từ disk trước → tiện dev/hot-reload.
// Build Release/MinSizeRel nên define SENGINE_RELEASE để tắt hoàn toàn.
#if defined(_DEBUG) && !defined(SENGINE_RELEASE)
#   define SENGINE_DEBUG_DISK 1
#endif
