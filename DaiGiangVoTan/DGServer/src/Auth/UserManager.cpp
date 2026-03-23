#include "UserManager.h"
#include <fstream>
#include <sstream>
#include <iostream>

UserManager::UserManager(const std::string& filePath)
    : _filePath(filePath)
{
}

LoginResult UserManager::Authenticate(const std::string& username, const std::string& password)
{
    std::string storedPass = FindUser(username);

    if (storedPass.empty())
    {
        std::cout << "[Auth] Tài khoản không tồn tại: " << username << "\n";
        return LOGIN_FAIL_NOT_FOUND;
    }

    if (storedPass != password)
    {
        std::cout << "[Auth] Sai mật khẩu cho tài khoản: " << username << "\n";
        return LOGIN_FAIL_WRONG_PASS;
    }

    std::cout << "[Auth] Đăng nhập thành công: " << username << "\n";
    return LOGIN_SUCCESS;
}

bool UserManager::Register(const std::string& username, const std::string& password)
{
    // Kiểm tra tài khoản đã tồn tại chưa
    if (!FindUser(username).empty())
    {
        std::cout << "[Auth] Tài khoản đã tồn tại: " << username << "\n";
        return false;
    }

    // Ghi thêm vào cuối file
    std::ofstream file(_filePath, std::ios::app);
    if (!file.is_open())
    {
        std::cerr << "[Auth] Không thể mở file: " << _filePath << "\n";
        return false;
    }

    file << username << ":" << password << "\n";
    file.close();

    std::cout << "[Auth] Đã đăng ký tài khoản mới: " << username << "\n";
    return true;
}

std::string UserManager::FindUser(const std::string& username)
{
    std::ifstream file(_filePath);
    if (!file.is_open())
    {
        // File chưa tồn tại → chưa có tài khoản nào
        return "";
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        // Tách username:password
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;

        std::string fileUser = line.substr(0, pos);
        std::string filePass = line.substr(pos + 1);

        if (fileUser == username)
        {
            file.close();
            return filePass;
        }
    }

    file.close();
    return "";
}
