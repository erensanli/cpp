#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

// ---------------------- AYARLAR ----------------------
struct Config {
    std::string token = "BURAYA_TOKEN_GIRIN";
    std::string server_id = "BURAYA_SERVER_ID_GIRIN";
    std::string password = "BURAYA_SIFRE_GIRIN";
    std::string host = "canary.discord.com";
    std::string port = "443";
    int refresh_interval_seconds = 120;
} CONFIG;

const std::string SUPER_PROPERTIES = "eyJvcyI6IldpbmRvd3MiLCJicm93c2VyIjoiRmlyZWZveCIsImRldmljZSI6IiIsInN5c3RlbV9sb2NhbGUiOiJ0ci1UUiIsImJyb3dzZXJfdXNlcl9hZ2VudCI6Ik1vemlsbGEvNS4wIChXaW5kb3dzIE5UIDEwLjA7IFdpbjY0OyB4NjQ7IHJ2OjEzMy4wKSBHZWNrby8yMDEwMDEwMSBGaXJlZm94LzEzMy4wIiwiYnJvd3Nlcl92ZXJzaW9uIjoiMTMzLjAiLCJvc192ZXJzaW9uIjoiMTAiLCJyZWZlcnJlciI6Imh0dHBzOi8vd3d3Lmdvb2dsZS5jb20vIiwicmVmZXJyaW5nX2RvbWFpbiI6Ind3dy5nb29nbGUuY29tIiwic2VhcmNoX2VuZ2luZSI6Imdvb2dsZSIsInJlZmVycmVyX2N1cnJlbnQiOiIiLCJyZWxlYXNlX2NoYW5uZWwiOiJjYW5hcnkiLCJjbGllbnRfYnVpbGRfbnVtYmVyIjozNTYxNDAsImNsaWVudF9ldmVudF9zb3VyY2UiOm51bGwsImhhc19jbGllbnRfbW9kcyI6ZmFsc2V9";
const std::string USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0";

std::string syncHttpsRequest(asio::io_context& ioc, const std::string& method, const std::string& target, const std::string& body = "") {
    try {
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_verify_mode(ssl::verify_none);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        auto const results = resolver.resolve(CONFIG.host, CONFIG.port);
        beast::get_lowest_layer(stream).connect(results);
        SSL_set_tlsext_host_name(stream.native_handle(), CONFIG.host.c_str());
        stream.handshake(ssl::stream_base::client);

        beast::http::request<beast::http::string_body> req{
            method == "POST" ? beast::http::verb::post : beast::http::verb::patch,
            target, 11
        };
        req.set(beast::http::field::host, CONFIG.host);
        req.set(beast::http::field::authorization, CONFIG.token);
        req.set(beast::http::field::content_type, "application/json");
        req.set(beast::http::field::user_agent, USER_AGENT);
        req.set("X-Super-Properties", SUPER_PROPERTIES);

        if (!body.empty()) {
            req.body() = body;
            req.prepare_payload();
        }

        beast::http::write(stream, req);

        beast::flat_buffer buffer;
        beast::http::response<beast::http::string_body> res;
        beast::http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec);
        return res.body();
    } catch (const std::exception& e) {
        std::cerr << "[-] Istek Hatasi: " << e.what() << "\n";
        return "{}";
    }
}

std::string refreshMfaToken(asio::io_context& ioc) {
    if (CONFIG.password.empty() || CONFIG.server_id.empty() || CONFIG.token.empty()) {
        std::cout << "[!] Bilgiler eksik.\n";
        return "";
    }

    try {
        // 1. Asama: /api/v7 uzerinden ticket al
        std::string initRes = syncHttpsRequest(ioc, "PATCH", "/api/v7/guilds/" + CONFIG.server_id + "/vanity-url");
        auto initData = json::parse(initRes, nullptr, false);

        if (!initData.is_discarded() && initData.value("code", 0) == 60003 && initData.contains("mfa")) {
            std::string ticket = initData["mfa"].value("ticket", "");

            // 2. Asama: /api/v9/mfa/finish ile ticket'i sifreyle onayla
            json finishPayload = {
                {"ticket", ticket},
                {"mfa_type", "password"},
                {"data", CONFIG.password}
            };

            std::string finishRes = syncHttpsRequest(ioc, "POST", "/api/v9/mfa/finish", finishPayload.dump());
            auto finishData = json::parse(finishRes, nullptr, false);

            if (!finishData.is_discarded() && finishData.contains("token")) {
                std::string mfaToken = finishData["token"].get<std::string>();
                std::cout << "\033[32m[+] MFA Token Basariyla Alindi:\033[0m " << mfaToken << "\n";
                return mfaToken;
            } else {
                std::cout << "\033[31m[-] Onaylama Hatasi:\033[0m " << finishRes << "\n";
            }
        } else if (!initData.is_discarded() && initData.contains("token")) {
            std::string mfaToken = initData["token"].get<std::string>();
            std::cout << "\033[32m[+] Dogrudan Token Alindi:\033[0m " << mfaToken << "\n";
            return mfaToken;
        } else {
            std::cout << "\033[33m[!] Beklenmeyen Yanit:\033[0m " << initRes << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[-] Hata: " << e.what() << "\n";
    }

    return "";
}

int main() {
    asio::io_context ioc;

    while (true) {
        refreshMfaToken(ioc);
        std::this_thread::sleep_for(std::chrono::seconds(CONFIG.refresh_interval_seconds));
    }

    return 0;
}
