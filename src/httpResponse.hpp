#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <map>
#include <atomic>

// 前置声明
class HTTP_REQUEST;
class HTTP_RESPONSE;

class HTTP_SERVER {
public:
    // 静态常量（缓冲区大小）
    static const int BUFFER_SIZE;

    // 构造/析构
    HTTP_SERVER(int port, std::string imagePath, std::string otaData, std::string otaUrl);
    ~HTTP_SERVER() {
        if (isRunning) {
            stop();
        }
    }

    // 核心方法
    void start();
    void stop();
    void handleRequest(SOCKET clientSocket, HTTP_REQUEST request);
    void sendHttpResponse(SOCKET clientSocket, int statusCode, std::string contentType,
                          std::string body, std::string extraHeaders = "");
    void sendFileResponse(SOCKET clientSocket, std::map<std::string, std::string> headers);

private:
    int serverPort;
    std::string imagePath;
    std::string otaData;
    std::string otaUrl;
    std::atomic<bool> isRunning; // 原子变量，保证线程安全
    SOCKET serverSocket;
    std::thread serverThread;
};
