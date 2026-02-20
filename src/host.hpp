#pragma once
#include <string>
#include <vector>

class HOST {
public:
    static const std::string HOST_ENTRY;
    static const std::string HOSTS_FILE_PATH;

    static bool IsAdmin(); // 新增：检查管理员权限
    static bool ReadHostsFile(std::vector<std::string> &lines);
    static bool WriteHostsFile(const std::vector<std::string> &lines);
    static bool ContainsHostEntry(const std::vector<std::string> &lines);
    static void RemoveHostEntry(std::vector<std::string> &lines);
    static void FlushDnsCache();
    static bool enable(); // 修改：返回bool表示执行结果
    static bool disable();// 修改：返回bool表示执行结果
};
