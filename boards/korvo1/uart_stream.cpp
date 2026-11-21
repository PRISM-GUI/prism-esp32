#include "uart_stream.hpp"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_err.h"

namespace korvo1 {

namespace {

constexpr uart_port_t kConsolePort = UART_NUM_0;
constexpr int kRxBuffer = 256;   // the IDF console's own size; more than the 128-byte FIFO

esp_err_t install_driver_once()
{
    if (uart_is_driver_installed(kConsolePort))
        return ESP_OK;
    const esp_err_t err = uart_driver_install(kConsolePort, kRxBuffer, 0, 0, nullptr, 0);
    if (err == ESP_OK)
        uart_vfs_dev_use_driver(kConsolePort);
    return err;
}

} // namespace

std::expected<UartStream, std::string> UartStream::open()
{
    const esp_err_t err = install_driver_once();
    if (err != ESP_OK)
        return std::unexpected(std::string("UART0 driver: ") + esp_err_to_name(err) +
                               "; the profile dump needs the console UART");
    return UartStream{};
}

std::expected<void, std::error_code> UartStream::write(std::span<const std::byte> out)
{
    while (!out.empty()) {
        const int n = uart_write_bytes(kConsolePort, out.data(), out.size());
        if (n < 0)
            return std::unexpected(std::make_error_code(std::errc::io_error));
        written_ += static_cast<std::size_t>(n);
        out = out.subspan(static_cast<std::size_t>(n));
    }
    return {};
}

std::expected<std::size_t, std::error_code> UartStream::read_some(std::span<std::byte>)
{
    return std::size_t{0};
}

void UartStream::drain() { uart_wait_tx_done(kConsolePort, portMAX_DELAY); }

} // namespace korvo1
