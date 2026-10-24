#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <typeinfo>

#if !defined(_CPPUNWIND) || !defined(_CPPRTTI)
#error Kodi requires both C++ exceptions and RTTI
#endif

void throw_runtime_error();

namespace
{
int failures = 0;
FILE* log_file = nullptr;

void check(const char* name, bool passed)
{
    debugPrint("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (log_file)
    {
        fprintf(log_file, "%s: %s\n", passed ? "PASS" : "FAIL", name);
        fflush(log_file);
    }
    if (!passed)
        ++failures;
}

struct Base { virtual ~Base() = default; };
struct Other { virtual ~Other() = default; };
struct Derived : Base, Other {};
struct Guard
{
    int& destroyed;
    ~Guard() { ++destroyed; }
};

__declspec(noinline) Derived* downcast(Base* base)
{
    return dynamic_cast<Derived*>(base);
}

__declspec(noinline) void unwind(int& destroyed)
{
    Guard guard{destroyed};
    throw_runtime_error();
}

DWORD WINAPI thread_check(void*)
{
    try { throw_runtime_error(); }
    catch (const std::runtime_error&) { return 42; }
    catch (...) { return 1; }
    return 2;
}

#ifdef RUNTIME_AUTO_RETURN
DWORD WINAPI return_watchdog(void*)
{
    Sleep(30000);
    XLaunchXBE("E:\\Dashboard\\default.xbe");
    return 0;
}
#endif
}

int main()
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    log_file = fopen("D:\\runtime-test.log", "w");
    debugPrint("Kodi NXDK runtime checks\n");
#ifdef RUNTIME_AUTO_RETURN
    HANDLE watchdog = CreateThread(nullptr, 0, return_watchdog, nullptr, 0, nullptr);
    if (watchdog)
        CloseHandle(watchdog);
#endif
    int destroyed = 0;
    bool caught = false;
    try { unwind(destroyed); }
    catch (const std::exception& error)
    {
        caught = strcmp(error.what(), "Kodi runtime check") == 0;
    }
    catch (...) {}
    check("typed catch across translation units", caught);
    check("destructor during unwinding", destroyed == 1);

    bool rethrown = false;
    try
    {
        try { throw 17; }
        catch (...) { throw; }
    }
    catch (int value) { rethrown = value == 17; }
    catch (...) {}
    check("catch-all and rethrow", rethrown);

    Derived derived;
    Base* base = &derived;
    Base plain;
    Base* missing = nullptr;
    check("dynamic_cast downcast", downcast(base) == &derived);
    check("dynamic_cast crosscast", dynamic_cast<Other*>(base) == static_cast<Other*>(&derived));
    check("dynamic_cast failed cast", downcast(&plain) == nullptr);
    check("dynamic_cast null", downcast(missing) == nullptr);
    check("typeid", typeid(*base) == typeid(Derived));

    HANDLE thread = CreateThread(nullptr, 0, thread_check, nullptr, 0, nullptr);
    DWORD code = 0;
    bool thread_ok = thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 &&
                     GetExitCodeThread(thread, &code) && code == 42;
    check("exception on worker thread", thread_ok);
    if (thread)
        CloseHandle(thread);

    char* end = nullptr;
    const double number = strtod("12.5x", &end);
    check("strtod", number == 12.5 && end && *end == 'x');
    char formatted[16];
    snprintf(formatted, sizeof(formatted), "%.2f", 1.25);
    check("float formatting", strcmp(formatted, "1.25") == 0);
    const auto before = std::chrono::steady_clock::now();
    Sleep(100);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - before).count();
    check("steady clock", elapsed >= 50 && elapsed < 5000);
    debugPrint("RESULT: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    if (log_file)
    {
        fprintf(log_file, "RESULT: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
        fclose(log_file);
    }
#ifdef RUNTIME_AUTO_RETURN
    debugPrint("Returning to dashboard in 10 seconds\n");
    Sleep(10000);
    XLaunchXBE("E:\\Dashboard\\default.xbe");
#endif
    while (true)
        Sleep(1000);
}
