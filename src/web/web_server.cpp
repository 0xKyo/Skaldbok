#include "web/web_server.h"

#include <algorithm>
#include <cctype>

#include <httplib.h>

namespace gm {

WebServer::WebServer(WebApp& app) : app_(app), server_(std::make_unique<httplib::Server>()) { install(); }

WebServer::~WebServer() { stop(); }

void WebServer::install() {
    server_->set_payload_max_length(13 * 1024 * 1024);   // only a chat picture is big; WebApp refuses anything else above 64 KB
    server_->set_read_timeout(10, 0);
    server_->set_write_timeout(10, 0);
    server_->set_keep_alive_timeout(5);

    auto handler = [this](const httplib::Request& rq, httplib::Response& rs) {
        WebRequest req;
        req.method = rq.method;
        req.path = rq.path;
        for (const auto& [k, v] : rq.params) req.query.emplace(k, v);
        for (const auto& [k, v] : rq.headers) {
            std::string key = k;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            req.headers.emplace(key, v);
        }
        req.body = rq.body;
        req.ip = rq.remote_addr;
        if (app_.config().trustProxy)                     // behind a tunnel every request comes from the tunnel: use the real client
            if (auto it = req.headers.find("x-forwarded-for"); it != req.headers.end()) req.ip = it->second.substr(0, it->second.find(','));

        WebResponse res = app_.handle(req);
        rs.status = res.status;
        for (const auto& [k, v] : res.headers) rs.set_header(k, v);
        rs.set_content(res.body, res.contentType);
    };
    for (const char* pattern : {".*"}) {
        server_->Get(pattern, handler);
        server_->Post(pattern, handler);
        server_->Put(pattern, handler);
        server_->Delete(pattern, handler);
        server_->Patch(pattern, handler);
    }
    server_->set_exception_handler([](const httplib::Request&, httplib::Response& rs, std::exception_ptr) {
        rs.status = 500;
        rs.set_content("{\"error\":\"Something went wrong.\"}", "application/json");
    });
}

int WebServer::start(const std::string& host, int port) {
    const int bound = port == 0 ? server_->bind_to_any_port(host) : (server_->bind_to_port(host, port) ? port : 0);
    if (bound <= 0) return 0;
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();
    return bound;
}

bool WebServer::run(const std::string& host, int port) { return server_->listen(host, port); }

void WebServer::stop() {
    server_->stop();
    if (thread_.joinable()) thread_.join();
}

}  // namespace gm
