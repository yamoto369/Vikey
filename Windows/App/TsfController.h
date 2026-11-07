#pragma once
namespace vikey { namespace tsf {
// If commandLine holds --register-tsf=<hex langid>, runs the elevated TSF
// registration helper, stores its exit code and returns true.
bool handleRegistrationCommand(const wchar_t* commandLine, int* exitCode);
bool initialize();
bool sync(bool vietnameseEnabled);
void shutdown();
}}
