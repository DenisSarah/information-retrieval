#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace web {

struct Response {
    int status = 200;
    std::string type = "text/plain; charset=utf-8";
    std::string body;
    std::string location;
};

std::string decode_query(std::string_view value);
std::string json_string(std::string_view value);
void serve(std::uint16_t port, const std::function<Response(std::string_view)>& handle);

}
