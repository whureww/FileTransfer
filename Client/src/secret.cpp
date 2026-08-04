#include "secret.h"

#include <cstring>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#endif

namespace ft {

// 开源版本: 不含真实中继服务器地址
// 如需使用自建中继服务器, 请参考 secret_local.cpp.example 模板
std::string get_relay_addr() {
    return "0.0.0.0:9091";  // 占位地址, 请替换为你的服务器
}

bool parse_relay_addr(std::string& host, unsigned short& port) {
    std::string addr = get_relay_addr();
    auto pos = addr.rfind(':');
    if (pos == std::string::npos) return false;
    host = addr.substr(0, pos);
    try {
        long p = std::stol(addr.substr(pos + 1));
        if (p < 1 || p > 65535) return false;
        port = static_cast<unsigned short>(p);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<std::string> get_local_ipv4_addresses() {
    std::vector<std::string> result;
#ifdef _WIN32
    ULONG bufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(std::malloc(bufLen));
    if (!pAddrs) return result;
    DWORD ret = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, pAddrs, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        std::free(pAddrs);
        pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(std::malloc(bufLen));
        if (!pAddrs) return result;
        ret = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, pAddrs, &bufLen);
    }
    if (ret != NO_ERROR) {
        std::free(pAddrs);
        return result;
    }

    // 分两层收集:
    //   tier1: 真实上网网卡 (非虚拟, 优先有默认网关) —— WiFi/以太网
    //   tier2: 无默认网关的非虚拟网卡 (兜底保留)
    // 虚拟网卡 (VPN/虚拟机/热点/隧道等) 一律过滤掉
    std::vector<std::string> tier1, tier2;

    // 虚拟网卡识别关键词 (描述/友好名, 已转小写)
    static const wchar_t* virtual_keywords[] = {
        L"vmware", L"virtualbox", L"virtual", L"hyper-v", L"wsl",
        L"docker", L"loopback", L"tunnel", L"bluetooth", L"vpn",
        L"wi-fi direct", L"wifi direct", L"mobile hotspot", L"wintun",
        L"npcap", L"tap-", L"wan miniport", L"isatap", L"teredo",
        L"6to4", L"wireguard", L"openvpn", L"nordvpn", L"zerotier",
        L"tailscale", L"hamachi", L"radmin", L"pptp", L"l2tp"
    };

    for (PIP_ADAPTER_ADDRESSES pCur = pAddrs; pCur; pCur = pCur->Next) {
        if (pCur->OperStatus != IfOperStatusUp) continue;
        if (pCur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        // 收集该网卡的全部 IPv4 地址
        std::vector<std::string> ips;
        for (PIP_ADAPTER_UNICAST_ADDRESS pUni = pCur->FirstUnicastAddress; pUni; pUni = pUni->Next) {
            sockaddr* sa = pUni->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET) continue;
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
            char ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            std::string ipstr(ip);
            if (ipstr.rfind("169.254.", 0) == 0) continue;   // 跳过 APIPA 链路本地地址
            ips.push_back(ipstr);
        }
        if (ips.empty()) continue;

        // 是否有默认网关 (真实上网网卡的标志)
        bool has_gateway = false;
        for (PIP_ADAPTER_GATEWAY_ADDRESS pGw = pCur->FirstGatewayAddress; pGw; pGw = pGw->Next) {
            if (pGw->Address.lpSockaddr && pGw->Address.lpSockaddr->sa_family == AF_INET) {
                sockaddr_in* gw = reinterpret_cast<sockaddr_in*>(pGw->Address.lpSockaddr);
                if (gw->sin_addr.s_addr != 0) { has_gateway = true; break; }
            }
        }

        // 虚拟网卡识别
        std::wstring combined = pCur->Description ? pCur->Description : L"";
        combined += L" ";
        if (pCur->FriendlyName) combined += pCur->FriendlyName;
        std::transform(combined.begin(), combined.end(), combined.begin(), ::towlower);
        bool is_virtual = false;
        for (auto kw : virtual_keywords) {
            if (combined.find(kw) != std::wstring::npos) { is_virtual = true; break; }
        }
        if (is_virtual) continue;   // 过滤掉虚拟网卡 (VPN/虚拟机/热点等)

        if (has_gateway) tier1.insert(tier1.end(), ips.begin(), ips.end());
        else             tier2.insert(tier2.end(), ips.begin(), ips.end());
    }
    std::free(pAddrs);

    // 真实上网网卡 IP 优先, 其余物理网卡在后
    result.insert(result.end(), tier1.begin(), tier1.end());
    result.insert(result.end(), tier2.begin(), tier2.end());
#else
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return result;
    for (struct ifaddrs* p = ifap; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (p->ifa_flags & IFF_LOOPBACK) continue;
        if (!(p->ifa_flags & IFF_UP)) continue;
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr,
                  ip, sizeof(ip));
        std::string ipstr(ip);
        if (ipstr.rfind("169.254.", 0) == 0) continue;
        result.push_back(ipstr);
    }
    freeifaddrs(ifap);
#endif
    return result;
}

} // namespace ft
