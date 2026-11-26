// PRISM's crdt::Hub for this board: the Wi-Fi access point, an HTTPS server with the page and the
// WebSocket at /room, and a crdt::Room fed from the logic thread. The header is PRISM's
// (include/prism/crdt/hub.hpp) and an app sees only that; PRISM builds no hub on bare metal
// (prism-net is off there: no sockets), so the transport lives here.
//
// Three layers that never cross: the server's tasks own the sockets and queue what they read, the
// channel wakes the logic thread (set_waker), and drain() -- the logic thread -- is the only one
// that ever touches the room, and through it, the Doc.

#include <prism/core/channel.hpp>
#include <prism/crdt/hub.hpp>
#include <prism/crdt/room.hpp>

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

#if !CONFIG_HTTPD_WS_SUPPORT
#error "the crdt firmware needs CONFIG_HTTPD_WS_SUPPORT=y (sdkconfig.crdt)"
#endif
#if !CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
#error "the crdt firmware needs CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT=y (sdkconfig.crdt)"
#endif
#if !CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
#error "the crdt firmware needs CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y (sdkconfig.crdt)"
#endif

#include <unistd.h>  // close()

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char *tag = "crdt";

constexpr const char *ap_ssid = "PRISM";
// WPA2 takes 8 to 63 characters (the literal's array carries its terminating NUL).
static_assert(sizeof(CONFIG_PRISM_CRDT_AP_PASSWORD) >= 9,
              "CONFIG_PRISM_CRDT_AP_PASSWORD is too short for WPA2: at least 8 characters");

// A browser's frame is a handful of bytes (a Yrs update or a sync message). The cap only refuses
// something that would not fit the room's idea of a message anyway.
constexpr std::size_t max_frame_bytes = 64 * 1024;

// The bundle, gzipped by scripts/build.sh into ${CMAKE_BINARY_DIR}/crdt-web and embedded by this
// component's CMakeLists.txt (EMBED_FILES, so these are the bytes verbatim). The certificate and
// its key come from the build too (EMBED_TXTFILES, which appends the NUL mbedTLS's PEM parser
// needs).
extern "C" {
extern const std::uint8_t cert_pem_start[] asm("_binary_cert_pem_start");
extern const std::uint8_t cert_pem_end[] asm("_binary_cert_pem_end");
extern const std::uint8_t key_pem_start[] asm("_binary_key_pem_start");
extern const std::uint8_t key_pem_end[] asm("_binary_key_pem_end");
extern const std::uint8_t page_gz_start[] asm("_binary_crdt_pair_html_gz_start");
extern const std::uint8_t page_gz_end[] asm("_binary_crdt_pair_html_gz_end");
extern const std::uint8_t script_gz_start[] asm("_binary_crdt_pair_js_gz_start");
extern const std::uint8_t script_gz_end[] asm("_binary_crdt_pair_js_gz_end");
extern const std::uint8_t module_gz_start[] asm("_binary_crdt_pair_wasm_gz_start");
extern const std::uint8_t module_gz_end[] asm("_binary_crdt_pair_wasm_gz_end");
extern const std::uint8_t data_gz_start[] asm("_binary_crdt_pair_data_gz_start");
extern const std::uint8_t data_gz_end[] asm("_binary_crdt_pair_data_gz_end");
}

struct Asset {
    const char *uri;
    const char *content_type;
    const std::uint8_t *begin;
    const std::uint8_t *end;
};

// The names the wasm build gives its files (PRISM's examples/meson.build emits crdt_pair.html,
// which loads crdt_pair.js, which loads crdt_pair.wasm and crdt_pair.data, the packaged files: the
// fonts). "/" is the page. Its coi-serviceworker.js stays a 404: it fakes the isolation headers on
// hosts that cannot send them, and this server sends them.
const Asset assets[] = {
    {"/", "text/html", page_gz_start, page_gz_end},
    {"/crdt_pair.js", "text/javascript", script_gz_start, script_gz_end},
    {"/crdt_pair.wasm", "application/wasm", module_gz_start, module_gz_end},
    {"/crdt_pair.data", "application/octet-stream", data_gz_start, data_gz_end},
};

using PeerId = prism::crdt::Room::PeerId;

enum class EventKind { join, message, leave };

// What a server task leaves for the logic thread. The bytes are copied: the receiving buffer is
// the handler's own.
struct Event {
    EventKind kind;
    PeerId peer;
    std::vector<std::byte> bytes;
};

std::string error_message(const char *what, esp_err_t error)
{
    return std::string(what) + " failed: " + esp_err_to_name(error);
}

std::string wifi_softap_start()
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if ((error = nvs_flash_erase()) == ESP_OK)
            error = nvs_flash_init();
    }
    if (error != ESP_OK)
        return error_message("nvs_flash_init", error);

    if ((error = esp_netif_init()) != ESP_OK)
        return error_message("esp_netif_init", error);
    if ((error = esp_event_loop_create_default()) != ESP_OK)
        return error_message("esp_event_loop_create_default", error);
    if (esp_netif_create_default_wifi_ap() == nullptr)
        return "esp_netif_create_default_wifi_ap failed: no access point netif";

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((error = esp_wifi_init(&init)) != ESP_OK)
        return error_message("esp_wifi_init", error);

    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char *>(config.ap.ssid), ap_ssid, sizeof config.ap.ssid);
    config.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(ap_ssid));
    std::strncpy(reinterpret_cast<char *>(config.ap.password), CONFIG_PRISM_CRDT_AP_PASSWORD,
                 sizeof config.ap.password);
    config.ap.channel = 1;
    config.ap.max_connection = 5;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;

    if ((error = esp_wifi_set_mode(WIFI_MODE_AP)) != ESP_OK)
        return error_message("esp_wifi_set_mode", error);
    if ((error = esp_wifi_set_config(WIFI_IF_AP, &config)) != ESP_OK)
        return error_message("esp_wifi_set_config", error);
    if ((error = esp_wifi_start()) != ESP_OK)
        return error_message("esp_wifi_start", error);

    ESP_LOGI(tag, "access point \"%s\" up at 192.168.4.1", ap_ssid);
    return {};
}

// The access point belongs to the chip, not to a Hub: the first Hub starts it, and a failure is
// remembered so a later attempt reports the same reason instead of re-running a half-done init.
const std::string &access_point()
{
    static const std::string error = wifi_softap_start();
    return error;
}

std::string header(httpd_req_t *req, const char *name)
{
    const std::size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length == 0)
        return {};
    std::string value(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK)
        return {};
    value.pop_back();
    return value;
}

// "https://host:port/path" and "host:port" both reduce to "host".
std::string_view url_host(std::string_view url)
{
    const std::size_t scheme = url.find("://");
    if (scheme != std::string_view::npos)
        url.remove_prefix(scheme + 3);
    return url.substr(0, url.find('/'));
}

std::string_view without_default_port(std::string_view host)
{
    constexpr std::string_view secure_port = ":443";
    if (host.ends_with(secure_port))
        host.remove_suffix(secure_port.size());
    return host;
}

// httpd_uri_t's fields depend on the Kconfig options, so each route starts zeroed.
httpd_uri_t get_route(const char *uri, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route{};
    route.uri = uri;
    route.method = HTTP_GET;
    route.handler = handler;
    return route;
}

}  // namespace

namespace prism::crdt {

// What the handshake asks (check_origin below): no Origin is not a browser; a listed one is
// allowed on purpose; otherwise the page must come from this server's own host.
//
// simplify: the desktop's hub also refuses a Host that is not an IP literal or localhost, which
// stops DNS rebinding (src/crdt/hub.cpp). Here there is no name to resolve -- the access point is
// 192.168.4.1 and the board is never a client of the internet -- so an Origin equal to the Host is
// the whole rule.
bool origin_allowed(std::string_view origin, std::string_view host,
                    std::span<const std::string> allowed)
{
    if (origin.empty())
        return true;
    if (std::find(allowed.begin(), allowed.end(), origin) != allowed.end())
        return true;
    return without_default_port(url_host(origin)) == without_default_port(host);
}

struct Hub::Impl {
    Impl(Doc &document, Options hub_options)
        : options(std::move(hub_options)), room(document)
    {
        events.observe([this](const Event &event) { apply(event); });
    }

    ~Impl()
    {
        if (httpd != nullptr)
            httpd_ssl_stop(httpd);
    }

    // Defined below: it registers the routes, which are built after this struct.
    void start();

    // The logic thread only.
    void apply(const Event &event)
    {
        switch (event.kind) {
        case EventKind::join: {
            auto relay = [this, peer = event.peer](std::span<const std::byte> bytes) {
                send(peer, bytes);
            };
            room.join(event.peer, std::move(relay));
            break;
        }
        case EventKind::message:
            room.receive(event.peer, event.bytes);
            break;
        case EventKind::leave:
            room.leave(event.peer);
            break;
        }
    }

    // The logic thread only, from inside a room callback: the relay of a message that was just
    // applied, or the catch-up the room sends a joining peer.
    //
    // simplify: no per-peer outbox cap -- the desktop hub holds 4 MiB per peer and drops the peer
    // when it overflows (src/crdt/hub.cpp). Here a phone that stops reading leaves its data in the
    // lwIP send queue until the socket gives up, which costs memory but cannot block the room (the
    // queue work fails and frees the frame). Add the hub's cap if a stalled browser is ever seen to
    // matter; the room then needs a "drop this peer" callback.
    void send(PeerId peer, std::span<const std::byte> bytes)
    {
        if (httpd == nullptr)
            return;
        // One block: the frame the async send reads, then the payload it writes out.
        const std::size_t frame_size = sizeof(httpd_ws_frame_t);
        auto *block = static_cast<std::byte *>(std::malloc(frame_size + bytes.size()));
        if (block == nullptr)
            return;
        auto *frame = reinterpret_cast<httpd_ws_frame_t *>(block);
        std::memset(frame, 0, frame_size);
        frame->type = HTTPD_WS_TYPE_BINARY;
        frame->payload = reinterpret_cast<std::uint8_t *>(block + frame_size);
        frame->len = bytes.size();
        std::memcpy(frame->payload, bytes.data(), bytes.size());
        // The queue copies the frame struct, not the payload, so the block stays ours: the
        // callback frees it, or we do when there was no queue to hand it to. A send to a socket
        // that has gone away fails inside the server's task; there is nothing to do about it.
        const esp_err_t error = httpd_ws_send_data_async(httpd, static_cast<int>(peer), frame,
                                                         free_sent_frame, block);
        if (error != ESP_OK)
            std::free(block);
    }

    static void free_sent_frame(esp_err_t error, int socket, void *arg)
    {
        (void)error;
        (void)socket;
        std::free(arg);
    }

    // The server's callbacks. Members, not free functions: Hub keeps its Impl to itself, and the
    // server only hands out C function pointers.
    static Impl *of(httpd_req_t *req);
    static esp_err_t serve_asset(httpd_req_t *req);
    static esp_err_t check_origin(httpd_req_t *req);
    static esp_err_t on_open(httpd_req_t *req);
    static esp_err_t on_frame(httpd_req_t *req);
    static void on_session_close(httpd_handle_t handle, int socket);
    static httpd_uri_t room_route();

    static const std::array<httpd_uri_t, 5> routes;

    Options options;
    Room room;
    core::Channel<Event> events;
    httpd_handle_t httpd = nullptr;
};

Hub::Impl *Hub::Impl::of(httpd_req_t *req)
{
    return static_cast<Impl *>(httpd_get_global_user_ctx(req->handle));
}

esp_err_t Hub::Impl::serve_asset(httpd_req_t *req)
{
    for (const Asset &asset : assets) {
        if (std::strcmp(req->uri, asset.uri) != 0)
            continue;
        // A browser needs the page to be a secure context with cross-origin isolation for the
        // threads the wasm build uses; the certificate is self-signed, so it is only accepted once.
        httpd_resp_set_hdr(req, "Cross-Origin-Opener-Policy", "same-origin");
        httpd_resp_set_hdr(req, "Cross-Origin-Embedder-Policy", "require-corp");
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_type(req, asset.content_type);
        return httpd_resp_send(req, reinterpret_cast<const char *>(asset.begin),
                              static_cast<ssize_t>(asset.end - asset.begin));
    }
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
}

// Called by the server before it answers the handshake, not as the URI handler: since IDF 5.0 a
// WebSocket handshake never reaches the handler (components/esp_http_server/src/httpd_uri.c), so
// this callback is where a join is accepted or refused. origin_allowed decides; this only reports.
//
// CHECK: returning ESP_FAIL closes the socket; whether the 403 written just before it reaches the
// browser or the connection just drops, the page's WebSocket fails either way.
esp_err_t Hub::Impl::check_origin(httpd_req_t *req)
{
    const std::string origin = header(req, "Origin");
    const std::string host = header(req, "Host");
    if (origin_allowed(origin, host, of(req)->options.allowed_origins))
        return ESP_OK;
    ESP_LOGW(tag, "refused a websocket from origin \"%s\" (host \"%s\")", origin.c_str(),
             host.c_str());
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "cross-origin websocket");
    return ESP_FAIL;
}

// The handshake was answered: the browser is in. Its peer id is its socket.
esp_err_t Hub::Impl::on_open(httpd_req_t *req)
{
    const auto peer = static_cast<PeerId>(httpd_req_to_sockfd(req));
    ESP_LOGI(tag, "socket %d joined", static_cast<int>(peer));
    // Queued for the logic thread, which the channel wakes through the waker the app set
    // (Hub::set_waker): once per drained batch, from this task.
    of(req)->events.send(Event{EventKind::join, peer, {}});
    return ESP_OK;
}

// Every binary frame is a Yrs message on its way to the logic thread. Control frames never arrive
// (handle_ws_control_frames is false); a text frame is not part of the protocol.
//
// simplify: a fragmented message arrives as one CONTINUE frame per fragment, which this drops --
// emscripten's WebSocket sends a page's message whole, and the room's messages are small.
esp_err_t Hub::Impl::on_frame(httpd_req_t *req)
{
    httpd_ws_frame_t frame = {};
    esp_err_t error = httpd_ws_recv_frame(req, &frame, 0);
    if (error != ESP_OK)
        return error;
    if (frame.type != HTTPD_WS_TYPE_BINARY || frame.len == 0)
        return ESP_OK;
    if (frame.len > max_frame_bytes) {
        ESP_LOGW(tag, "refused a %u-byte frame", static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }
    std::vector<std::byte> bytes(frame.len);
    frame.payload = reinterpret_cast<std::uint8_t *>(bytes.data());
    if ((error = httpd_ws_recv_frame(req, &frame, frame.len)) != ESP_OK)
        return error;
    const auto peer = static_cast<PeerId>(httpd_req_to_sockfd(req));
    of(req)->events.send(Event{EventKind::message, peer, std::move(bytes)});
    return ESP_OK;
}

// Replaces the server's own close(): it must close the socket itself. The room forgets the peer
// on the logic thread, so a message queued before this close still lands before the leave.
void Hub::Impl::on_session_close(httpd_handle_t handle, int socket)
{
    close(socket);
    auto *impl = static_cast<Impl *>(httpd_get_global_user_ctx(handle));
    if (impl == nullptr)
        return;
    ESP_LOGI(tag, "socket %d left", socket);
    impl->events.send(Event{EventKind::leave, static_cast<PeerId>(socket), {}});
}

httpd_uri_t Hub::Impl::room_route()
{
    httpd_uri_t room = get_route("/room", on_frame);
    room.is_websocket = true;
    room.handle_ws_control_frames = false;
    room.ws_pre_handshake_cb = check_origin;
    room.ws_post_handshake_cb = on_open;
    return room;
}

const std::array<httpd_uri_t, 5> Hub::Impl::routes = {
    get_route("/", serve_asset),
    get_route("/crdt_pair.js", serve_asset),
    get_route("/crdt_pair.wasm", serve_asset),
    get_route("/crdt_pair.data", serve_asset),
    room_route(),
};

// A Hub that cannot come up throws (hub.hpp): there is no hub to hand the app, and the board has
// nowhere to go without one.
void Hub::Impl::start()
{
    if (const std::string &wifi = access_point(); !wifi.empty()) {
        ESP_LOGE(tag, "%s", wifi.c_str());
        throw std::runtime_error{wifi};
    }

    // Options::host, Options::port and Options::static_dir are the desktop hub's (src/crdt/hub.cpp:
    // a loopback server handing out files from a directory). The board listens where the access
    // point is, on 443, and serves the page it embeds; port() says 443 for the same reason.
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = cert_pem_start;
    // mbedTLS parses PEM only when the length counts the NUL that EMBED_TXTFILES appends.
    config.servercert_len = static_cast<std::size_t>(cert_pem_end - cert_pem_start);
    config.prvtkey_pem = key_pem_start;
    config.prvtkey_len = static_cast<std::size_t>(key_pem_end - key_pem_start);
    // The five browsers the access point takes, plus the server's own two or three for closing
    // sessions (the default of 4 is one short of five browsers).
    config.httpd.max_open_sockets = 7;
    config.httpd.max_uri_handlers = static_cast<std::uint16_t>(routes.size());
    config.httpd.lru_purge_enable = true;
    config.httpd.close_fn = on_session_close;
    config.httpd.global_user_ctx = this;
    // Without it, stopping the server would free() the context: it belongs to this Impl.
    config.httpd.global_user_ctx_free_fn = [](void *) {};

    if (esp_err_t error = httpd_ssl_start(&httpd, &config); error != ESP_OK)
        throw std::runtime_error{error_message("httpd_ssl_start", error)};

    for (const httpd_uri_t &route : routes) {
        if (esp_err_t error = httpd_register_uri_handler(httpd, &route); error != ESP_OK) {
            httpd_ssl_stop(httpd);
            httpd = nullptr;
            throw std::runtime_error{error_message("httpd_register_uri_handler", error)};
        }
    }

    ESP_LOGI(tag, "serving https://192.168.4.1");
}

Hub::Hub(Doc &doc, Options hub_options)
    : impl_(std::make_unique<Impl>(doc, std::move(hub_options)))
{
    impl_->start();
}

Hub::~Hub() = default;

std::uint16_t Hub::port() const
{
    return 443;
}

void Hub::drain()
{
    impl_->events.drain_notifications();
}

void Hub::set_waker(std::function<void()> fn)
{
    impl_->events.set_waker(std::move(fn));
}

}  // namespace prism::crdt
