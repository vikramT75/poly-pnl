#include "query_server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <iomanip>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCK close
#endif

namespace gravia {
namespace daemon {

QueryServer::QueryServer(int port,
                         engine::PnLEngine& engine,
                         WalletInternTable& wallet_table)
    : port_(port), engine_(engine), wallet_table_(wallet_table) {}

QueryServer::~QueryServer() {
    stop();
}

void QueryServer::start() {
    running_ = true;
    worker_thread_ = std::thread(&QueryServer::run, this);
}

void QueryServer::stop() {
    if (running_.exchange(false)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

static std::string to_hex_addr(const uint8_t addr[20]) {
    std::ostringstream oss;
    oss << "0x";
    for (int i = 0; i < 20; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(addr[i]);
    }
    return oss.str();
}

void QueryServer::run() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCK) {
        std::cerr << "[QueryServer] Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
#ifndef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[QueryServer] Failed to bind to port " << port_ << std::endl;
        CLOSE_SOCK(server_fd);
        return;
    }

    if (listen(server_fd, 10) != 0) {
        std::cerr << "[QueryServer] Failed to listen" << std::endl;
        CLOSE_SOCK(server_fd);
        return;
    }

    std::cout << "[QueryServer] Query API listening on port " << port_ << std::endl;

    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == INVALID_SOCK) {
            if (!running_) break;
            continue;
        }

        char buffer[1024] = {0};
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            CLOSE_SOCK(client_fd);
            continue;
        }

        std::string req(buffer, bytes);
        std::istringstream iss(req);
        std::string cmd;
        iss >> cmd;

        std::string resp;
        if (cmd == "PING") {
            resp = "PONG\n";
        } else if (cmd == "STATS") {
            std::ostringstream ss;
            ss << "{"
               << "\"events_processed\":" << engine_.events_processed() << ","
               << "\"fills_processed\":" << engine_.fills_processed() << ","
               << "\"rollbacks_applied\":" << engine_.rollbacks_applied() << ","
               << "\"last_block\":" << engine_.last_processed_block() << ","
               << "\"incomplete_history_events\":" << engine_.incomplete_hist()
               << "}\n";
            resp = ss.str();
        } else if (cmd == "LEADERBOARD") {
            int k = 100;
            iss >> k;
            auto lb = engine_.get_leaderboard(k);
            std::ostringstream ss;
            ss << "[";
            for (size_t i = 0; i < lb.size(); ++i) {
                if (i > 0) ss << ",";
                uint8_t raw_addr[20] = {0};
                wallet_table_.reverse(lb[i].wallet_idx, raw_addr);
                std::string hex_addr = to_hex_addr(raw_addr);

                ss << "{"
                   << "\"wallet_idx\":" << lb[i].wallet_idx << ","
                   << "\"wallet\":\"" << hex_addr << "\","
                   << "\"total_pnl\":" << (lb[i].total_pnl / 1000000.0) << ","
                   << "\"realized_pnl\":" << (lb[i].realized_pnl / 1000000.0) << ","
                   << "\"unrealized_pnl\":" << (lb[i].unrealized_pnl / 1000000.0) << ","
                   << "\"volume\":" << (lb[i].volume / 1000000.0) << ","
                   << "\"winning_markets\":" << lb[i].winning_markets << ","
                   << "\"total_markets\":" << lb[i].total_markets
                   << "}";
            }
            ss << "]\n";
            resp = ss.str();
        } else if (cmd == "WALLET") {
            std::string addr_hex;
            iss >> addr_hex;
            uint32_t w_idx = 0;
            if (addr_hex.rfind("0x", 0) == 0 && addr_hex.length() == 42) {
                uint8_t raw[20];
                for (int i = 0; i < 20; ++i) {
                    raw[i] = static_cast<uint8_t>(std::stoi(addr_hex.substr(2 + i * 2, 2), nullptr, 16));
                }
                w_idx = wallet_table_.find(raw);
            } else {
                try { w_idx = std::stoul(addr_hex); } catch (...) { w_idx = 0xFFFFFFFF; }
            }

            if (w_idx != 0xFFFFFFFF) {
                auto s = engine_.get_wallet_summary(w_idx);
                std::ostringstream ss;
                ss << "{"
                   << "\"wallet_idx\":" << s.wallet_idx << ","
                   << "\"realized_pnl\":" << (s.realized_pnl / 1000000.0) << ","
                   << "\"unrealized_pnl\":" << (s.unrealized_pnl / 1000000.0) << ","
                   << "\"total_pnl\":" << (s.total_pnl / 1000000.0) << ","
                   << "\"volume\":" << (s.volume / 1000000.0) << ","
                   << "\"position_count\":" << s.position_count << ","
                   << "\"winning_markets\":" << s.winning_markets << ","
                   << "\"losing_markets\":" << s.losing_markets
                   << "}\n";
                resp = ss.str();
            } else {
                resp = "{\"error\":\"wallet not found\"}\n";
            }
        } else {
            resp = "{\"error\":\"unknown command\"}\n";
        }

        send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
        CLOSE_SOCK(client_fd);
    }

    CLOSE_SOCK(server_fd);
}

} // namespace daemon
} // namespace gravia
