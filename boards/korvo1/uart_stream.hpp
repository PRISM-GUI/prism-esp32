#pragma once

// UART0 -- the console's port, wired to the USB-UART bridge -- as a prism::io::ByteStream: the
// transport for prism::profile::dump() (doc/design/profiling.md, "Dump"). Bytes go through the
// UART driver, not through stdout, so the C library's "\n" to "\r\n" translation cannot touch a
// binary dump. Dumps only: read_some reports a closed stream and close does nothing.

#include <prism/io/byte_stream.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <system_error>

namespace korvo1 {

class UartStream {
public:
    // Installs the UART driver on UART0 once, and moves the console onto it (the console writes
    // to the FIFO directly until a driver exists, and the two must not both do that). The error
    // says what failed.
    static std::expected<UartStream, std::string> open();

    // All of `out`; returns when the driver has handed it to the FIFO.
    std::expected<void, std::error_code> write(std::span<const std::byte> out);
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte>);
    void close() {}

    // Returns when the last byte has left the pin.
    void drain();
    std::size_t bytes_written() const { return written_; }

private:
    std::size_t written_ = 0;
};

static_assert(prism::io::ByteStream<UartStream>);

} // namespace korvo1
