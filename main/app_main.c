// C, not C++: a C++ translation unit may not call main(), and calling it is all this wrapper does.

#include "port.hpp"

int main(void);

void app_main(void)
{
    korvo1_start();
    main();
    korvo1_stopped();
}
