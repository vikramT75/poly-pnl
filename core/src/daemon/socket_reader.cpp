#include "socket_reader.h"
#include <iostream>
#include <vector>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK closesocket
#else
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  using socket_t = int;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCK close
#endif

namespace gravia {
namespace daemon {

SocketReader::SocketReader(std::string socket_path,
                           decoder::Decoder& decoder,
                           engine::PnLEngine& engine)
    : socket_path_(std::move(socket_path)),
      decoder_(decoder),
      engine_(engine) {}

SocketReader::~SocketReader() {
    stop();
}

void SocketReader::start() {
    running_ = true;
    worker_thread_ = std::thread(&SocketReader::run, this);
}

void SocketReader::stop() {
    if (running_.exchange(false)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
}

void SocketReader::run() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCK) {
        std::cerr << "[SocketReader] Failed to create socket" << std::endl;
        return;
    }

    int opt = 1;
#ifndef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Listen on localhost port 9999 or unix domain socket
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9999);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[SocketReader] Failed to bind to port 9999" << std::endl;
        CLOSE_SOCK(server_fd);
        return;
    }

    if (listen(server_fd, 5) != 0) {
        std::cerr << "[SocketReader] Failed to listen" << std::endl;
        CLOSE_SOCK(server_fd);
        return;
    }

    std::cout << "[SocketReader] Engine listening on 127.0.0.1:9999 for ingestion stream" << std::endl;

    NormalizedEvent decoded_events[decoder::Decoder::MAX_EVENTS_PER_LOG];

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

        std::cout << "[SocketReader] Ingestor connected" << std::endl;

        while (running_) {
            uint32_t frame_len = 0;
            // Read 4-byte LE frame length
            int bytes_read = recv(client_fd, reinterpret_cast<char*>(&frame_len), 4, 0);
            if (bytes_read <= 0) break;

            if (frame_len == 0 || frame_len > 10 * 1024 * 1024) {
                // Invalid frame length guard
                break;
            }

            std::vector<uint8_t> buffer(frame_len);
            size_t total_received = 0;
            while (total_received < frame_len) {
                int r = recv(client_fd, reinterpret_cast<char*>(buffer.data() + total_received),
                             static_cast<int>(frame_len - total_received), 0);
                if (r <= 0) break;
                total_received += r;
            }

            if (total_received < frame_len) break;

            // Check frame type:
            // 1. RawLog struct (sizeof(RawLog))
            // 2. NormalizedEvent batch (multiple of 64 bytes)
            // 3. Rollback command: sentinel starting with magic 0x524F4C4C ("ROLL")
            if (frame_len >= 8 && std::memcmp(buffer.data(), "ROLL", 4) == 0) {
                uint64_t rollback_block = 0;
                std::memcpy(&rollback_block, buffer.data() + 4, sizeof(uint64_t));
                std::cout << "[SocketReader] Rollback command received for block " << rollback_block << std::endl;
                engine_.rollback(rollback_block);
                continue;
            }

            if (frame_len == sizeof(RawLog)) {
                RawLog raw_log{};
                std::memcpy(&raw_log, buffer.data(), sizeof(RawLog));
                int count = decoder_.decode(raw_log, decoded_events, decoder::Decoder::MAX_EVENTS_PER_LOG);
                for (int i = 0; i < count; ++i) {
                    engine_.process_event(decoded_events[i]);
                }
            } else if (frame_len % sizeof(NormalizedEvent) == 0) {
                size_t num_events = frame_len / sizeof(NormalizedEvent);
                const auto* ev_ptr = reinterpret_cast<const NormalizedEvent*>(buffer.data());
                engine_.process_events(ev_ptr, static_cast<int>(num_events));
            }
        }

        CLOSE_SOCK(client_fd);
        std::cout << "[SocketReader] Ingestor disconnected" << std::endl;
    }

    CLOSE_SOCK(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace daemon
} // namespace gravia
