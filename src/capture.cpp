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
#include "capture.hpp"
#include "io.hpp"
#include "i18n.hpp"
#include <ntddndis.h>
#include <stdexcept>
#include <string>

// 新增：报错类型枚举（便于分类处理）
enum class CaptureErrorType {
    DEBUG,
    WARNING,
    ERROR,
    FATAL
};

// 新增：统一报错函数（分级输出+日志）
void CaptureReportError(CaptureErrorType type, const std::string& msg, bool terminate = false) {
    switch (type) {
        case CaptureErrorType::DEBUG:
            IO::Debug("[DEBUG] " + msg);
            break;
        case CaptureErrorType::WARNING:
            IO::Warn("[WARNING] " + msg);
            break;
        case CaptureErrorType::ERROR:
            IO::Error("[ERROR] " + msg);
            break;
        case CaptureErrorType::FATAL:
            IO::Error("[FATAL] " + msg);
            if (terminate) {
                throw std::runtime_error("[FATAL] " + msg); // 抛异常便于上层捕获
            }
            break;
    }
}

CAPTURE::CAPTURE_RESULT *CAPTURE::global_capture_result = nullptr;
pcap_t *CAPTURE::global_pcap_handle = nullptr;

void CAPTURE::packet_handler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data) {
    CaptureReportError(CaptureErrorType::DEBUG, t("packet_received") + ": " + std::to_string(header->len));
    try {
        if (CAPTURE::IsWantedRequest(pkt_data, header->len, *CAPTURE::global_capture_result)) {
            CaptureReportError(CaptureErrorType::DEBUG, t("target_packet_found"));
            pcap_breakloop(CAPTURE::global_pcap_handle);
        }
    } catch (const std::exception& e) {
        // 新增：捕获单数据包解析异常，不终止整体捕获
        CaptureReportError(CaptureErrorType::ERROR, t("packet_parse_failed") + ": " + e.what());
    }
}

bool CAPTURE::IsWantedRequest_NetworkLayer(const u_char **buf, int &len) {
    // 1. 校验基础长度
    if (len < (int)(sizeof(eth_header) + sizeof(ip_header))) {
        CaptureReportError(CaptureErrorType::DEBUG, t("packet_too_small_network") + " (need: " + 
                           std::to_string(sizeof(eth_header) + sizeof(ip_header)) + ", got: " + std::to_string(len) + ")");
        return false;
    }

    // 2. 解析以太网头部
    const eth_header *eh = (const eth_header *)*buf;
    if (ntohs(eh->ether_type) != 0x0800) {
        CaptureReportError(CaptureErrorType::DEBUG, t("non_ipv4_packet") + " (ether_type: " + 
                           std::to_string(ntohs(eh->ether_type)) + ")");
        return false;
    }

    // 3. 偏移指针并更新长度（安全校验）
    if ((size_t)len < sizeof(eth_header)) {
        CaptureReportError(CaptureErrorType::ERROR, t("eth_header_len_exceed"));
        return false;
    }
    *buf += sizeof(eth_header);
    len -= sizeof(eth_header);

    // 4. 解析IP头部
    const ip_header *ih = (const ip_header *)*buf;
    // 修复：IP版本校验逻辑（正确提取高4位）
    if (((ih->ip_hl_v >> 4) & 0x0F) != 4) {
        CaptureReportError(CaptureErrorType::DEBUG, t("non_ipv4_packet_header") + " (version: " + 
                           std::to_string((ih->ip_hl_v >> 4) & 0x0F) + ")");
        return false;
    }
    if (ih->ip_p != IPPROTO_TCP) {
        CaptureReportError(CaptureErrorType::DEBUG, t("non_tcp_packet") + " (proto: " + 
                           std::to_string(ih->ip_p) + ")");
        return false;
    }

    // 5. IP头部长度偏移（增加安全校验）
    const size_t ip_header_len = (ih->ip_hl_v & 0x0F) * 4;
    if (ip_header_len > (size_t)len) {
        CaptureReportError(CaptureErrorType::ERROR, t("ip_header_len_exceed") + " (calc: " + 
                           std::to_string(ip_header_len) + ", remain: " + std::to_string(len) + ")");
        return false;
    }
    *buf += ip_header_len;
    len -= ip_header_len;

    CaptureReportError(CaptureErrorType::DEBUG, t("network_layer_passed"));
    return true;
}

bool CAPTURE::IsWantedRequest_TransportLayer(const u_char **buf, int &len) {
    // 1. 基础长度校验
    if (len < (int)sizeof(tcp_header)) {
        CaptureReportError(CaptureErrorType::DEBUG, t("packet_too_small_transport") + " (need: " + 
                           std::to_string(sizeof(tcp_header)) + ", got: " + std::to_string(len) + ")");
        return false;
    }

    // 2. 解析TCP头部
    const tcp_header *th = (const tcp_header *)*buf;
    if ((th->th_offx2 & 0xF0) == 0) {
        CaptureReportError(CaptureErrorType::DEBUG, t("invalid_tcp_header") + " (th_offx2: " + 
                           std::to_string(th->th_offx2) + ")");
        return false;
    }

    // 3. 校验HTTP端口（80）
    u_short src_port = ntohs(th->th_sport);
    u_short dst_port = ntohs(th->th_dport);
    if (src_port != 80 && dst_port != 80) {
        CaptureReportError(CaptureErrorType::DEBUG, t("non_http_port") + " (src: " + 
                           std::to_string(src_port) + ", dst: " + std::to_string(dst_port) + ")");
        return false;
    }

    // 4. TCP头部长度偏移（增加安全校验）
    const size_t tcp_header_len = ((th->th_offx2 & 0xF0) >> 4) * 4;
    if (tcp_header_len > (size_t)len) {
        CaptureReportError(CaptureErrorType::ERROR, t("tcp_header_len_exceed") + " (calc: " + 
                           std::to_string(tcp_header_len) + ", remain: " + std::to_string(len) + ")");
        return false;
    }
    *buf += tcp_header_len;
    len -= tcp_header_len;

    CaptureReportError(CaptureErrorType::DEBUG, t("transport_layer_passed") + ": " + std::to_string(len) + " " + t("bytes"));
    return true;
}

bool CAPTURE::IsWantedRequest(const u_char *pkt_data, int data_len, CAPTURE_RESULT &result) {
    CaptureReportError(CaptureErrorType::DEBUG, t("analyzing_packet") + " " + std::to_string(data_len) + " " + t("bytes"));

    // 修复：创建临时指针，避免篡改原始pkt_data导致的越界
    const u_char *tmp_buf = pkt_data;
    int tmp_len = data_len;

    // 分层校验（短路逻辑）
    if (!IsWantedRequest_NetworkLayer(&tmp_buf, tmp_len) ||
        !IsWantedRequest_TransportLayer(&tmp_buf, tmp_len) ||
        tmp_len <= 0) {
        CaptureReportError(CaptureErrorType::DEBUG, t("packet_rejected"));
        return false;
    }

    // 解析HTTP Payload
    const std::string payload((char *)tmp_buf, tmp_len);
    CaptureReportError(CaptureErrorType::DEBUG, t("http_payload_preview") + ": " + 
                       payload.substr(0, std::min(200, tmp_len)));

    // 正则匹配OTA请求
    std::smatch matches;
    const std::regex pattern(R"(^POST (/product/[0-9]+/[0-9a-f]+/ota/checkVersion) HTTP/[0-9.]+\r\n([^\r\n]*\r\n)*\r\n(.*)$)");
    if (std::regex_search(payload, matches, pattern)) {
        CaptureReportError(CaptureErrorType::DEBUG, t("ota_request_matched"));
        result.productUrl = matches[1].str();
        CaptureReportError(CaptureErrorType::DEBUG, t("product_url") + ": " + result.productUrl);

        // 解析JSON Body（修复：解析失败不终止程序，仅报错）
        try {
            result.request_body = nlohmann::json::parse(matches[3].str());
            CaptureReportError(CaptureErrorType::DEBUG, t("json_body_parsed"));
        } catch (const nlohmann::json::parse_error &e) {
            CaptureReportError(CaptureErrorType::ERROR, t("failed_parse_json") + ": " + e.what());
            return false; // 仅跳过该数据包，不终止整体捕获
        }
        return true;
    }

    CaptureReportError(CaptureErrorType::DEBUG, t("ota_request_not_matched"));
    return false;
}

void CAPTURE::capture(CAPTURE_RESULT &result) {
    CaptureReportError(CaptureErrorType::DEBUG, t("initializing_capture"));
    char errbuf[PCAP_ERRBUF_SIZE] = {0}; // 初始化错误缓冲区
    global_capture_result = &result;
    global_pcap_handle = nullptr;

    // 1. 查找网络设备
    CaptureReportError(CaptureErrorType::DEBUG, t("finding_devices"));
    pcap_if_t *devices = nullptr;
    if (pcap_findalldevs(&devices, errbuf) == -1) {
        CaptureReportError(CaptureErrorType::FATAL, t("error_finding_devices") + ": " + errbuf, true);
    }

    // 2. 筛选目标热点设备（192.168.137.1）
    CaptureReportError(CaptureErrorType::DEBUG, t("searching_hotspot"));
    pcap_if_t *selectedDevice = nullptr;
    for (pcap_if_t *device = devices; device && !selectedDevice; device = device->next) {
        CaptureReportError(CaptureErrorType::DEBUG, t("checking_device") + ": " + (device->name ? device->name : "unknown"));
        for (pcap_addr_t *addr = device->addresses; addr && !selectedDevice; addr = addr->next) {
            if (addr->addr && addr->addr->sa_family == AF_INET) {
                u_long ip_addr = ((struct sockaddr_in *)addr->addr)->sin_addr.s_addr;
                if (ip_addr == htonl((192 << 24) | (168 << 16) | (137 << 8) | 1)) {
                    CaptureReportError(CaptureErrorType::DEBUG, t("found_target_interface") + ": " + device->name);
                    selectedDevice = device;
                }
            }
        }
    }
    if (!selectedDevice) {
        pcap_freealldevs(devices); // 提前释放资源
        CaptureReportError(CaptureErrorType::FATAL, t("no_interface_found"), true);
    }

    // 3. 打开捕获句柄
    CaptureReportError(CaptureErrorType::DEBUG, t("opening_capture_handle") + ": " + selectedDevice->name);
    pcap_t *packetCaptureHandle = pcap_open_live(
        selectedDevice->name, 
        65536, 
        PCAP_OPENFLAG_PROMISCUOUS, 
        1000, 
        errbuf
    );
    if (!packetCaptureHandle) {
        pcap_freealldevs(devices);
        CaptureReportError(CaptureErrorType::FATAL, t("unable_open_adapter") + ": " + errbuf, true);
    }
    global_pcap_handle = packetCaptureHandle;

    // 4. 校验数据链路层
    if (pcap_datalink(packetCaptureHandle) != DLT_EN10MB) {
        CaptureReportError(CaptureErrorType::WARNING, t("non_ethernet_link") + " (datalink: " + 
                           std::to_string(pcap_datalink(packetCaptureHandle)) + ")");
    }

    // 5. 设置BPF过滤器
    CaptureReportError(CaptureErrorType::DEBUG, t("setting_packet_filter"));
    u_int netmask = 0xffffff00; // 修复：正确的24位子网掩码（255.255.255.0）
    if (selectedDevice->addresses != NULL && selectedDevice->addresses->netmask != NULL) {
        netmask = ((struct sockaddr_in *)(selectedDevice->addresses->netmask))->sin_addr.S_un.S_addr;
    }
    CaptureReportError(CaptureErrorType::DEBUG, t("netmask") + ": " + std::to_string(netmask));

    struct bpf_program fcode;
    std::string pcap_filter_string = "tcp port 80";
    CaptureReportError(CaptureErrorType::DEBUG, t("compiling_filter") + ": " + pcap_filter_string);
    if (pcap_compile(packetCaptureHandle, &fcode, pcap_filter_string.c_str(), 1, netmask) < 0) {
        pcap_close(packetCaptureHandle);
        pcap_freealldevs(devices);
        CaptureReportError(CaptureErrorType::FATAL, t("unable_compile_filter") + ": " + pcap_geterr(packetCaptureHandle), true);
    }

    if (pcap_setfilter(packetCaptureHandle, &fcode) < 0) {
        pcap_close(packetCaptureHandle);
        pcap_freealldevs(devices);
        CaptureReportError(CaptureErrorType::FATAL, t("error_setting_filter") + ": " + pcap_geterr(packetCaptureHandle), true);
    }
    pcap_freecode(&fcode); // 新增：释放过滤器编译结果

    // 6. 开始捕获
    CaptureReportError(CaptureErrorType::WARNING, t("waiting_update_packets"));
    CaptureReportError(CaptureErrorType::DEBUG, t("starting_capture_loop"));

    // 修复：调整资源释放顺序（先释放设备列表，再执行捕获）
    pcap_freealldevs(devices);
    int capture_ret = pcap_loop(packetCaptureHandle, -1, packet_handler, NULL);
    if (capture_ret == -1) {
        CaptureReportError(CaptureErrorType::ERROR, t("capture_loop_failed") + ": " + pcap_geterr(packetCaptureHandle));
    }

    // 7. 清理资源
    CaptureReportError(CaptureErrorType::INFO, t("captured_update_request") + ": " + result.productUrl);
    CaptureReportError(CaptureErrorType::DEBUG, t("closing_capture_handle"));
    pcap_close(packetCaptureHandle);
    global_pcap_handle = nullptr;
    global_capture_result = nullptr;
}
