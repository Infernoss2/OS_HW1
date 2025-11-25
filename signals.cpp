#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;

void ctrlCHandler(int sig_num) {
    // TODO: Add your implementation
    std::cout << "got ctrl-C" << std::endl;
    // kill the foreground running process, if there isnt then dont do nothing
    // TODO how can this function now if there is foreground process?
    // print process <foreground-PID> was killed
    // TODO what is the sig_num that this function gets and from where does it gets it?

    _exit(0);
}
