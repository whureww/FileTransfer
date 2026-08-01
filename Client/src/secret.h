#ifndef SECRET_H
#define SECRET_H

#include <string>
#include <vector>

namespace ft {

// 解密并返回中继服务器地址 (host:port 格式)
// 地址以 XOR 加密形式硬编码, 防止明文出现在 exe 字符串表中
// 注意: 这只是基础混淆, 无法抵御动态调试; 真正安全需密钥不在程序内
std::string get_relay_addr();

// 解析 get_relay_addr() 返回的 "host:port"
bool parse_relay_addr(std::string& host, unsigned short& port);

// 获取本机所有可用 IPv4 地址 (排除回环、APIPA、未启用网卡)
std::vector<std::string> get_local_ipv4_addresses();

} // namespace ft

#endif // SECRET_H
