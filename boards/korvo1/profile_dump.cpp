#include "profile_dump.hpp"

#include <prism/profile/dump.hpp>

#include "buttons.hpp"
#include "uart_stream.hpp"

#include <cstdio>
#include <memory>

namespace korvo1 {

#if defined(PRISM_PROFILING)

namespace {

struct DumpOnPlay {
    Button play;
    UartStream uart;
};

// Other threads' printf and ESP_LOG (which goes through vprintf to stdout) wait on stdout's lock
// for the whole dump, so no line lands inside a chunk.
void dump_with_console_held(UartStream &uart)
{
    flockfile(stdout);
    std::fflush(stdout);
    const auto dumped = prism::profile::dump(prism::profile::global(), uart);
    uart.drain();
    funlockfile(stdout);
    if (dumped)
        std::printf("\nprofile: dump done (%zu bytes)\n", uart.bytes_written());
    else
        std::printf("\nprofile: dump failed: %s; is prism-trace listening on this port at 921600? "
                    "press MODE again\n",
                    dumped.error().message().c_str());
}

void dump_if_pressed(DumpOnPlay &state)
{
    if (!state.play.pressed())
        return;
    std::printf("profile: dumping on the console UART...\n");
    dump_with_console_held(state.uart);
    state.play.pressed(); // a press during the dump (it takes seconds) is not a second request
}

} // namespace

std::function<void()> dump_profile_on_play()
{
    auto play = Button::open(mode_button);
    auto uart = UartStream::open();
    if (!play || !uart) {
        std::printf("profile dump unavailable (%s): the MODE button or the console UART could not be "
                    "set up; check the other tasks do not own the ADC or UART0. The demo runs "
                    "without the dump\n",
                    (play ? uart.error() : play.error()).c_str());
        return [] {};
    }
    auto state = std::make_shared<DumpOnPlay>(DumpOnPlay{std::move(*play), std::move(*uart)});
    return [state] { dump_if_pressed(*state); };
}

#else

std::function<void()> dump_profile_on_play() { return [] {}; }

#endif

} // namespace korvo1
