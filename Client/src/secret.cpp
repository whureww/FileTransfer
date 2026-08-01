#include "secret.h"

#include <cstring>

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
    for (PIP_ADAPTER_ADDRESSES pCur = pAddrs; pCur; pCur = pCur->Next) {
        if (pCur->OperStatus != IfOperStatusUp) continue;
        if (pCur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (PIP_ADAPTER_UNICAST_ADDRESS pUni = pCur->FirstUnicastAddress; pUni; pUni = pUni->Next) {
            sockaddr* sa = pUni->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET) continue;
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
            char ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            std::string ipstr(ip);
            // 跳过 APIPA (169.254.x.x)
            if (ipstr.rfind("169.254.", 0) == 0) continue;
            result.push_back(ipstr);
        }
    }
    std::free(pAddrs);
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
