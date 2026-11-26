#pragma once

// The board's web side for the crdt firmware: a Wi-Fi access point named PRISM at 192.168.4.1,
// an HTTPS server that serves the crdt_pair page, and a WebSocket at /room relayed into a
// crdt::Room. Nothing here touches the Doc: the server's tasks queue what arrived, and the logic
// thread drains it (see drain()).
//
// A file, not a component (components/ holds what other boards could reuse, and this is one
// firmware's server): main/crdt_net.{hpp,cpp} is only built for the crdt app.

#include <cstddef>
#include <memory>
#include <string>

namespace prism::crdt {
class Doc;
}

namespace board_crdt {

class Server {
public:
    // The definition is crdt_net.cpp's: this is here so the file's request handlers can name it.
    struct Impl;

    explicit Server(prism::crdt::Doc &doc);
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Brings up the access point and the server. "" when both are running, else the error to
    // halt with.
    std::string start();

    // The logic thread, once per loop: hands the queued joins, frames and leaves to the room, then
    // says how many browsers are in it.
    std::size_t drain();

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace board_crdt
