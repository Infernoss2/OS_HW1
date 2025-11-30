#include <iostream>
#include <signal.h>
#include "signals.h"
#include <cstring>
#include "Commands.h"

// #include "../../../../../AppData/Local/Programs/winlibs-x86_64-posix-seh-gcc-15.1.0-mingw-w64msvcrt-12.0.0-r1/mingw64/include/c++/15.1.0/bits/this_thread_sleep.h"
#include <unistd.h>   // for sleep

using namespace std;

void ctrlCHandler(int sig_num) {
    const char* msg = "smash: got ctrl-C\n"; // maybe without the \n
    write(STDOUT_FILENO, msg, strlen(msg));

    SmallShell &sm = SmallShell::getInstance();
    pid_t fg_pid = sm.getFgPid();

    if (fg_pid > 0) { // there is a child process
        if (kill(fg_pid, SIGKILL) == -1) {
            return;
        }
        char buf[1024];
        int size = snprintf(buf, sizeof(buf), "smash: process %d was killed\n", fg_pid);
        if (size > 0) {
            write(STDOUT_FILENO, buf, size);
        }
        sm.clear_fg_pid();
    }
}
