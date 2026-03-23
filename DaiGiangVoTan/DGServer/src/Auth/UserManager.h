#pragma once
#include <string>
#include "../../CommonProtocol/LoginProtocol.h"

// Quản lý tài khoản người dùng, lưu trữ bằng file text
// Định dạng file: mỗi dòng là "username:password"
class UserManager
{
public:
    // Đường dẫn mặc định tới file tài khoản
    UserManager(const std::string& filePath = "users.txt");

    // Xác thực tài khoản, trả về LoginResult
    LoginResult Authenticate(const std::string& username, const std::string& password);

    // Đăng ký tài khoản mới (thêm vào cuối file)
    // Trả về true nếu thành công, false nếu username đã tồn tại
    bool Register(const std::string& username, const std::string& password);

private:
    std::string _filePath;

    // Tìm kiếm username trong file, trả về password nếu có, rỗng nếu không
    std::string FindUser(const std::string& username);
};
