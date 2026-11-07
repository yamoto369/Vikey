#include "TsfEngineAdapter.h"
#include <windows.h>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>

using vikey::tsf::EngineAdapter;
using vikey::tsf::EngineEdit;
static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
int main() {
    EngineAdapter adapter;
    check("adapter initializes", adapter.initialize());
    EngineEdit a = adapter.processKey('A', false, false, false);
    check("plain letter is consumed by Telex engine", a.consume);
    EngineEdit s = adapter.processKey('S', false, false, false);
    check("tone key produces replacement", s.consume && s.backspaces == 1 && s.text == L"á");

    EngineAdapter controlAdapter;
    check("second adapter initializes on same thread", controlAdapter.initialize());
    EngineEdit ctrl = controlAdapter.processKey('A', false, false, true);
    check("modified shortcut passes through", !ctrl.consume);

    std::mutex mutex;
    std::condition_variable cv;
    int stage = 0;
    EngineEdit threadA, threadB;
    std::thread first([&] {
        EngineAdapter local;
        bool init = local.initialize();
        EngineEdit firstLetter = local.processKey('A', false, false, false);
        {
            std::unique_lock<std::mutex> lock(mutex);
            stage = 1; cv.notify_all();
            cv.wait(lock, [&] { return stage >= 2; });
        }
        threadA = local.processKey('S', false, false, false);
        std::lock_guard<std::mutex> lock(mutex);
        stage = 3; cv.notify_all();
        if (!init || !firstLetter.consume) threadA.consume = false;
    });
    std::thread second([&] {
        EngineAdapter local;
        bool init = local.initialize();
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return stage >= 1; });
        }
        EngineEdit firstLetter = local.processKey('O', false, false, false);
        {
            std::unique_lock<std::mutex> lock(mutex);
            stage = 2; cv.notify_all();
            cv.wait(lock, [&] { return stage >= 3; });
        }
        threadB = local.processKey('S', false, false, false);
        if (!init || !firstLetter.consume) threadB.consume = false;
    });
    first.join();
    second.join();
    check("thread A keeps independent typing state", threadA.consume && threadA.backspaces == 1 && threadA.text == L"á");
    check("thread B keeps independent typing state", threadB.consume && threadB.backspaces == 1 && threadB.text == L"ó");
    std::printf("TSF engine adapter: %s (%d failures)\n",
        failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
