#include <iostream>
#include <thread>
#include "../Network/LoginServer.h"
#include "../Network/GameServer.h"

int main()
{
    // ── Login Server (port 9000, blocking, trên thread riêng) ──
    LoginServer loginServer(9000, "users.txt");
    if (!loginServer.Start())
    {
        std::cerr << "[DGServer] Không thể khởi động Login Server!\n";
        return 1;
    }

    // ── Game Server (port 9001, non-blocking poll) ─────────────
    GameServer gameServer(9001);

    // Load map mặc định (maps/Home) — thay đổi tên folder tùy theo map muốn dùng
    gameServer.LoadMap("maps/Home");

    if (!gameServer.Start())
    {
        std::cerr << "[DGServer] Không thể khởi động Game Server!\n";
        return 1;
    }

    std::cout << "[DGServer] Đại Giang Võ Tan Server đang chạy...\n";
    std::cout << "[DGServer]   LoginServer port 9000\n";
    std::cout << "[DGServer]   GameServer  port 9001\n";
    std::cout << "[DGServer] Nhấn Ctrl+C để dừng.\n";

    // LoginServer chạy trên thread riêng (blocking accept)
    std::thread loginThread([&loginServer]()
    {
        while (loginServer.IsRunning())
            loginServer.AcceptOnce();
    });
    loginThread.detach();

    // Main thread: poll GameServer liên tục
    while (gameServer.IsRunning())
    {
        gameServer.Poll();
        // Sleep nhỏ tránh busy-loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
