#include "web/http.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace web {
namespace {

struct Socket {
    int fd;
    ~Socket() {
        if (fd >= 0)
            ::close(fd);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    explicit Socket(int value) : fd(value) {
        if (fd < 0)
            throw std::runtime_error("Не удалось создать соединение");
    }
};

void send_response(int fd, const Response& response) {
    std::string data =
        "HTTP/1.1 " + std::to_string(response.status) +
        " Response\r\n"
        "Content-Type: " +
        response.type + "\r\nContent-Length: " + std::to_string(response.body.size()) +
        "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    if (!response.location.empty())
        data += "Location: " + response.location + "\r\n";
    data += "\r\n" + response.body;
    std::size_t position = 0;
    while (position < data.size()) {
        const auto sent = ::send(fd, data.data() + position, data.size() - position, 0);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return;
        position += static_cast<std::size_t>(sent);
    }
}

std::string target(int fd) {
    std::string request;
    std::array<char, 4096> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos) {
        const auto size = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (size < 0 && errno == EINTR)
            continue;
        if (size <= 0)
            throw std::runtime_error("Не удалось прочитать HTTP-запрос");
        request.append(buffer.data(), static_cast<std::size_t>(size));
        if (request.size() > 65536)
            throw std::runtime_error("Слишком длинный HTTP-запрос");
    }
    const auto end = request.find("\r\n");
    const auto line = request.substr(0, end);
    const auto space = line.find(' ', 4);
    if (line.compare(0, 4, "GET ") != 0 || space == std::string::npos ||
        (line.substr(space + 1) != "HTTP/1.1" && line.substr(space + 1) != "HTTP/1.0")) {
        throw std::runtime_error("Поддерживаются только GET-запросы");
    }
    const auto result = line.substr(4, space - 4);
    if (result.empty() || result.front() != '/')
        throw std::runtime_error("Некорректный адрес");
    return result;
}

}

std::string decode_query(std::string_view value) {
    const auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        throw std::runtime_error("Некорректное кодирование запроса");
    };
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (value.size() - i < 3)
                throw std::runtime_error("Неполное кодирование запроса");
            const auto high = hex(value[i + 1]);
            const auto low = hex(value[i + 2]);
            result += static_cast<char>(high * 16 + low);
            i += 2;
        } else
            result += value[i] == '+' ? ' ' : value[i];
    }
    if (result.find('\0') != std::string::npos)
        throw std::runtime_error("Нулевой байт в запросе");
    return result;
}

std::string json_string(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
            result += static_cast<char>(ch);
        } else if (ch < 0x20) {
            result += "\\u00";
            result += hex[ch >> 4];
            result += hex[ch & 15];
        } else
            result += static_cast<char>(ch);
    }
    return result + '"';
}

void serve(std::uint16_t port, const std::function<Response(std::string_view)>& handle) {
    std::signal(SIGPIPE, SIG_IGN);
    Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
    const int reuse = 1;
    ::setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener.fd, 8) < 0) {
        throw std::runtime_error("Не удалось открыть порт " + std::to_string(port));
    }
    const auto accept_clients = [fd = listener.fd, handle] {
        for (;;) {
            const int accepted = ::accept(fd, nullptr, nullptr);
            if (accepted < 0 && errno == EINTR)
                continue;
            if (accepted < 0)
                return;
            Socket client(accepted);
            const timeval timeout{10, 0};
            ::setsockopt(client.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(client.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            try {
                send_response(client.fd, handle(target(client.fd)));
            } catch (const std::exception& error) {
                send_response(client.fd, {400, "text/plain; charset=utf-8", error.what(), {}});
            }
        }
    };
    for (unsigned i = 0; i < 3; ++i)
        std::thread(accept_clients).detach();
    accept_clients();
}

}
