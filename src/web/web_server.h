// Puts a WebApp on HTTP (cpp-httplib). Plain HTTP: for players outside the local network use a tunnel or a reverse proxy
// that adds HTTPS (Cloudflare Tunnel, Tailscale Funnel, Caddy...).
#pragma once

#include <memory>
#include <string>
#include <thread>

#include "web/web_app.h"

namespace httplib {
class Server;
}

namespace gm {

class WebServer {
public:
    explicit WebServer(WebApp& app);
    ~WebServer();
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Starts serving on a background thread. port 0 = any free port. Returns the port, or 0 if it could not bind.
    int start(const std::string& host, int port);
    // Serves on the calling thread until stop() is called (from a signal handler, for example). False if it could not bind.
    bool run(const std::string& host, int port);
    void stop();

private:
    void install();

    WebApp& app_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
};

}  // namespace gm
