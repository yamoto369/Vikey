// Local-only crash diagnostics for the Linux IBus engine. The signal handler writes a small text report
// (signal, fault address, backtrace as module+offset, version; never typed text) and re-raises the signal.
#pragma once
#include <string>
namespace vikey { namespace crash {
std::string dir();                        // $XDG_STATE_HOME/vikey/crashes (~/.local/state/vikey/crashes)
/// Install the signal handler (SIGSEGV/SIGBUS/SIGABRT/SIGFPE/SIGILL) on an alternate stack. Call early in main.
void install(const char* version);
/// Crash on purpose (self-test): "abort" -> SIGABRT, anything else -> SIGSEGV.
[[noreturn]] void selfTest(const std::string& kind);
}}
