#include <stdexcept>

// A separate translation unit keeps the optimizer from replacing the unwind.
void throw_runtime_error()
{
    throw std::runtime_error("Kodi runtime check");
}
