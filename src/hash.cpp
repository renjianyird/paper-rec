// Copyright (C) 2025 Langning Chen
//
// This file is part of paper.
//
// paper is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// paper is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with paper.  If not, see <https://www.gnu.org/licenses/>.

#include "define.hpp"
#include "io.hpp"
#include "hash.hpp"
#include "i18n.hpp"
#include <fstream>
#include <picohash.h>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <atomic>

// 自定义异常类：区分不同错误类型
class HashException : public std::runtime_error {
public:
    enum ErrorType {
        FILE_OPEN_FAILED,
        FILE_SEEK_FAILED,
        FILE_WRITE_FAILED,
        HASH_NOT_FOUND,
        MULTIPLE_HASH_PATTERNS,
        INVALID_HASH_SEQUENCE,
        EMPTY_PASSWORD,
        BUFFER_OVERFLOW,
        FILE_READ_FAILED,
        INVALID_OFFSET
    };

    HashException(ErrorType type, const std::string& msg) 
        : std::runtime_error(msg), error_type(type) {}

    ErrorType getErrorType() const { return error_type; }

private:
    ErrorType error_type;
};

// 工具函数：安全文件写入（原子操作，先写临时文件）
static bool safeWriteToFile(const std::string& filename, size_t offset, const std::string& data) {
    namespace fs = std::filesystem;
    std::string temp_filename = filename + ".tmp." + std::to_string(std::atomic_uint64_t::operator++(new std::atomic_uint64_t(0)));
    
    // 复制原文件到临时文件
    try {
        std::ifstream src(filename, std::ios::binary);
        if (!src.is_open()) {
            throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_file") + ": " + filename);
        }
        std::ofstream dst(temp_filename, std::ios::binary);
        if (!dst.is_open()) {
            src.close();
            throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_create_temp_file") + ": " + temp_filename);
        }
        dst << src.rdbuf();
        src.close();
        dst.close();

        // 修改临时文件
        FILE* temp_file = fopen(temp_filename.c_str(), "r+b");
        if (!temp_file) {
            fs::remove(temp_filename);
            throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_temp_file") + ": " + temp_filename);
        }
        if (fseek(temp_file, static_cast<long>(offset), SEEK_SET) != 0) {
            fclose(temp_file);
            fs::remove(temp_filename);
            throw HashException(HashException::FILE_SEEK_FAILED, t("seek_failed_at_offset") + ": " + std::to_string(offset));
        }
        size_t written = fwrite(data.c_str(), 1, data.size(), temp_file);
        fclose(temp_file);
        if (written != data.size()) {
            fs::remove(temp_filename);
            throw HashException(HashException::FILE_WRITE_FAILED, 
                t("write_failed") + ": " + std::to_string(written) + "/" + std::to_string(data.size()) + " bytes");
        }

        // 替换原文件（原子操作）
        fs::rename(temp_filename, filename);
        return true;
    } catch (...) {
        fs::remove(temp_filename);
        throw;
    }
}

bool HASH::isHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

std::vector<std::pair<size_t, size_t>> HASH::findHashPatterns(const std::string &filename) {
    IO::Debug(t("searching_hash_patterns") + ": " + filename);
    std::vector<std::pair<size_t, size_t>> positions;
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_file") + ": " + filename);
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < 0) {
        file.close();
        throw HashException(HashException::FILE_READ_FAILED, t("cannot_get_file_size") + ": " + filename);
    }
    IO::Debug(t("file_size") + ": " + std::to_string(fileSize) + " " + t("bytes"));
    file.seekg(0, std::ios::beg);

    const size_t bufferSize = 1024 * 1024;
    const size_t overlapSize = 70;
    std::vector<char> buffer(bufferSize);
    size_t totalBytesRead = 0;
    IO::Debug(t("using_buffer_size") + ": " + std::to_string(bufferSize) + " " + t("bytes") + " " + t("with_overlap") + ": " + std::to_string(overlapSize));

    while (totalBytesRead < static_cast<size_t>(fileSize)) {
        // 修复：计算实际要读取的字节数（避免重叠区重复计算）
        size_t bytesToRead = std::min(bufferSize - overlapSize, static_cast<size_t>(fileSize) - totalBytesRead);
        size_t readOffset = 0;

        if (totalBytesRead > 0) {
            // 移动重叠区数据到缓冲区头部
            std::memmove(buffer.data(), buffer.data() + (bufferSize - overlapSize), overlapSize);
            readOffset = overlapSize;
        }

        // 读取新数据
        file.read(buffer.data() + readOffset, static_cast<std::streamsize>(bytesToRead));
        std::streamsize actualRead = file.gcount();
        if (actualRead < 0) {
            file.close();
            throw HashException(HashException::FILE_READ_FAILED, t("file_read_error") + ": " + filename);
        }
        if (actualRead == 0 && totalBytesRead < static_cast<size_t>(fileSize)) {
            break; // 提前EOF
        }

        // 实际处理的缓冲区长度
        size_t processSize = readOffset + static_cast<size_t>(actualRead);
        if (processSize < overlapSize) {
            break; // 剩余数据不足重叠区，无需处理
        }

        // 扫描哈希模式（修复：循环终止条件避免越界）
        for (size_t i = 0; i <= processSize - overlapSize; i++) {
            // 检测SHA256哈希模式：# + 64位十六进制 + 两个空格 + -
            if (buffer[i] == '#' && (i + 67) <= processSize) {
                if (isValidHashSequence(buffer.data(), i + 1, processSize, 64) &&
                    buffer[i + 65] == ' ' && buffer[i + 66] == ' ' && buffer[i + 67] == '-') {
                    
                    // 修复：绝对偏移计算（总已读 - 重叠区 + 当前i + 1）
                    size_t absolutePos = (totalBytesRead - (totalBytesRead > 0 ? overlapSize : 0)) + i + 1;
                    if (absolutePos + 64 > static_cast<size_t>(fileSize)) {
                        throw HashException(HashException::INVALID_OFFSET, t("hash_offset_out_of_file") + ": " + std::to_string(absolutePos));
                    }
                    IO::Debug(t("found_sha256_hash_at") + ": " + std::to_string(absolutePos));
                    positions.push_back({absolutePos, 64});
                }
            }

            // 检测MD5哈希模式：= + 空格 + " + 32位十六进制 + 两个空格 + - + "
            if (buffer[i] == '=' && (i + 38) <= processSize) {
                if (buffer[i + 1] == ' ' && buffer[i + 2] == '"' &&
                    isValidHashSequence(buffer.data(), i + 3, processSize, 32) &&
                    buffer[i + 35] == ' ' && buffer[i + 36] == ' ' && buffer[i + 37] == '-' && buffer[i + 38] == '"') {
                    
                    size_t absolutePos = (totalBytesRead - (totalBytesRead > 0 ? overlapSize : 0)) + i + 3;
                    if (absolutePos + 32 > static_cast<size_t>(fileSize)) {
                        throw HashException(HashException::INVALID_OFFSET, t("hash_offset_out_of_file") + ": " + std::to_string(absolutePos));
                    }
                    IO::Debug(t("found_md5_hash_at") + ": " + std::to_string(absolutePos));
                    positions.push_back({absolutePos, 32});
                }
            }
        }

        // 更新总读取字节数（仅累加新读取的部分）
        totalBytesRead += static_cast<size_t>(actualRead);
    }

    file.close();
    IO::Debug(t("hash_pattern_search_completed") + " " + std::to_string(positions.size()) + " " + t("patterns"));
    return positions;
}

bool HASH::isValidHashSequence(const char *data, size_t pos, size_t dataSize, size_t hashLength) {
    // 强化边界检查
    if (data == nullptr || pos >= dataSize || (pos + hashLength) > dataSize) {
        IO::Debug(t("invalid_hash_sequence_boundary") + ": pos=" + std::to_string(pos) + ", size=" + std::to_string(dataSize) + ", len=" + std::to_string(hashLength));
        return false;
    }

    for (size_t i = 0; i < hashLength; ++i) {
        if (!isHexChar(data[pos + i])) {
            IO::Debug(t("invalid_hex_char") + ": " + std::to_string(data[pos + i]) + " at pos=" + std::to_string(pos + i));
            return false;
        }
    }
    return true;
}

std::string HASH::toHex(const unsigned char *data, size_t length) {
    if (data == nullptr || length == 0) {
        throw HashException(HashException::BUFFER_OVERFLOW, t("empty_data_for_hex_conversion"));
    }
    std::string hexString;
    hexString.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        char hex[3];
        int ret = snprintf(hex, sizeof(hex), "%02x", data[i]);
        if (ret != 2) {
            throw HashException(HashException::BUFFER_OVERFLOW, t("hex_conversion_failed") + ": " + std::to_string(i));
        }
        hexString.append(hex);
    }
    return hexString;
}

std::string HASH::MD5(const std::string &input) {
    picohash_ctx_t ctx;
    unsigned char digest[PICOHASH_MD5_DIGEST_LENGTH] = {0};
    picohash_init_md5(&ctx);
    picohash_update(&ctx, input.c_str(), input.size());
    picohash_final(&ctx, digest);
    return toHex(digest, PICOHASH_MD5_DIGEST_LENGTH);
}

std::string HASH::MD5File(const std::string &filename) {
    IO::Debug(t("calculating_md5_for_file") + ": " + filename);
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_file") + ": " + filename);
    }

    const size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    picohash_ctx_t ctx;
    picohash_init_md5(&ctx);
    size_t totalBytes = 0;

    while (true) {
        file.read(buffer.data(), bufferSize);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead < 0) {
            file.close();
            throw HashException(HashException::FILE_READ_FAILED, t("file_read_error") + ": " + filename);
        }
        if (bytesRead == 0) break;
        
        picohash_update(&ctx, buffer.data(), static_cast<size_t>(bytesRead));
        totalBytes += static_cast<size_t>(bytesRead);
    }

    file.close();
    unsigned char digest[PICOHASH_MD5_DIGEST_LENGTH] = {0};
    picohash_final(&ctx, digest);
    std::string result = toHex(digest, PICOHASH_MD5_DIGEST_LENGTH);
    IO::Debug(t("md5_calculated_for") + " " + std::to_string(totalBytes) + " " + t("bytes") + ": " + result);
    return result;
}

std::string HASH::MD5FileSegment(const std::string &filename, size_t start, size_t end) {
    IO::Debug(t("calculating_md5_segment") + ": " + filename + " [" + std::to_string(start) + "-" + std::to_string(end) + "]");
    if (start >= end) {
        throw HashException(HashException::INVALID_OFFSET, t("invalid_segment_range") + ": " + std::to_string(start) + " >= " + std::to_string(end));
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_file") + ": " + filename);
    }

    const size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    picohash_ctx_t ctx;
    picohash_init_md5(&ctx);
    
    // 定位到起始位置
    file.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    if (file.fail()) {
        file.close();
        throw HashException(HashException::FILE_SEEK_FAILED, t("seek_failed_to_start") + ": " + std::to_string(start));
    }

    size_t currentPos = start;
    size_t totalProcessed = 0;
    while (currentPos < end) {
        size_t bytesToRead = std::min(bufferSize, end - currentPos);
        file.read(buffer.data(), static_cast<std::streamsize>(bytesToRead));
        std::streamsize bytesRead = file.gcount();
        
        if (bytesRead < 0) {
            file.close();
            throw HashException(HashException::FILE_READ_FAILED, t("segment_read_error") + ": " + filename);
        }
        if (bytesRead == 0) break;

        picohash_update(&ctx, buffer.data(), static_cast<size_t>(bytesRead));
        currentPos += static_cast<size_t>(bytesRead);
        totalProcessed += static_cast<size_t>(bytesRead);
    }

    file.close();
    unsigned char digest[PICOHASH_MD5_DIGEST_LENGTH] = {0};
    picohash_final(&ctx, digest);
    std::string result = toHex(digest, PICOHASH_MD5_DIGEST_LENGTH);
    IO::Debug(t("segment_md5_calculated") + " " + std::to_string(totalProcessed) + " " + t("bytes") + ": " + result);
    return result;
}

std::string HASH::SHA1File(const std::string &filename) {
    IO::Debug(t("calculating_sha1_for_file") + ": " + filename);
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw HashException(HashException::FILE_OPEN_FAILED, t("cannot_open_file") + ": " + filename);
    }

    const size_t bufferSize = 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    picohash_ctx_t ctx;
    picohash_init_sha1(&ctx);
    size_t totalBytes = 0;

    while (true) {
        file.read(buffer.data(), bufferSize);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead < 0) {
            file.close();
            throw HashException(HashException::FILE_READ_FAILED, t("file_read_error") + ": " + filename);
        }
        if (bytesRead == 0) break;
        
        picohash_update(&ctx, buffer.data(), static_cast<size_t>(bytesRead));
        totalBytes += static_cast<size_t>(bytesRead);
    }

    file.close();
    unsigned char digest[PICOHASH_SHA1_DIGEST_LENGTH] = {0};
    picohash_final(&ctx, digest);
    std::string result = toHex(digest, PICOHASH_SHA1_DIGEST_LENGTH);
    IO::Debug(t("sha1_calculated_for") + " " + std::to_string(totalBytes) + " " + t("bytes") + ": " + result);
    return result;
}

std::string HASH::SHA256(const std::string &input) {
    picohash_ctx_t ctx;
    unsigned char digest[PICOHASH_SHA256_DIGEST_LENGTH] = {0};
    picohash_init_sha256(&ctx);
    picohash_update(&ctx, input.c_str(), input.size());
    picohash_final(&ctx, digest);
    return toHex(digest, PICOHASH_SHA256_DIGEST_LENGTH);
}

void HASH::replaceHash(const std::string &filename) {
    try {
        IO::Info(t("finding_password"));
        IO::Debug(t("starting_password_search") + ": " + filename);
        
        const std::vector<std::pair<size_t, size_t>> positions = HASH::findHashPatterns(filename);
        if (positions.empty()) {
            throw HashException(HashException::HASH_NOT_FOUND, t("no_passwords_found"));
        }
        if (positions.size() > 1) {
            throw HashException(HashException::MULTIPLE_HASH_PATTERNS, t("multiple_password_patterns"));
        }

        size_t hashOffset = positions[0].first;
        size_t hashLength = positions[0].second;
        IO::Debug(t("found_password_at_offset") + " " + std::to_string(hashOffset));
        IO::Debug(t("hash_length") + ": " + std::to_string(hashLength) + " " + t("characters"));

        // 获取新密码（强化空值检查）
        std::string newPassword;
        while (true) {
            IO::Input(t("input_new_password") + ": ", newPassword);
            if (!newPassword.empty()) break;
            IO::Warn(t("password_cannot_be_empty"));
        }

        // 生成新哈希（MD5拼接\n需注释说明场景）
        IO::Debug(t("generating_hash_for_password"));
        std::string newHash;
        if (hashLength == 32) {
            // 注：此处拼接\n是适配特定场景（如密码文件行尾换行），若无需可移除
            newHash = HASH::MD5(newPassword + '\n');
        } else if (hashLength == 64) {
            newHash = HASH::SHA256(newPassword);
        } else {
            throw HashException(HashException::INVALID_HASH_SEQUENCE, 
                t("unsupported_hash_length") + ": " + std::to_string(hashLength));
        }

        if (newHash.size() != hashLength) {
            throw HashException(HashException::INVALID_HASH_SEQUENCE, 
                t("hash_length_mismatch") + ": " + std::to_string(newHash.size()) + " vs " + std::to_string(hashLength));
        }

        IO::Debug(t("new_hash_generated") + ": " + newHash);
        
        // 安全替换哈希（原子操作+错误检查）
        if (!safeWriteToFile(filename, hashOffset, newHash)) {
            throw HashException(HashException::FILE_WRITE_FAILED, t("hash_replacement_failed"));
        }

        IO::Info(t("password_hash_replacement_completed"));
        IO::Debug(t("password_hash_replacement_completed") + ": " + filename);
    } catch (const HashException& e) {
        IO::Error(t("hash_operation_failed") + ": " + e.what());
        // 可根据错误类型做特殊处理（如日志上报、回滚等）
        switch (e.getErrorType()) {
            case HashException::FILE_OPEN_FAILED:
                IO::Error(t("suggest_check_file_permission"));
                break;
            case HashException::HASH_NOT_FOUND:
                IO::Error(t("suggest_check_hash_pattern_format"));
                break;
            default:
                break;
        }
        throw; // 向上抛异常，让上层决定是否终止程序
    } catch (const std::exception& e) {
        IO::Error(t("unknown_error") + ": " + e.what());
        throw;
    }
}
