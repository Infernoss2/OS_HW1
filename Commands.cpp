#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"

#include "../../../../../AppData/Local/Programs/winlibs-x86_64-posix-seh-gcc-15.1.0-mingw-w64msvcrt-12.0.0-r1/mingw64/x86_64-w64-mingw32/include/limits.h"

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

bool _isBackgroundComamnd(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

static bool find_env_var(const char* var_name) {
    int fd = open("/proc/self/environ",O_RDONLY);
    if (fd == -1) {
        perror("smash error: open failed");
        return false;
    }
    ssize_t n;
    char buf[1024];
    while ((n=read(fd,buf,sizeof(buf))) > 0) {
        ssize_t i = 0;
        while (i < n) {
            char* key = &buf[i];
            ssize_t j = i;
            while (j < n && buf[j] != '\0') {
                j++;
            }
            if (j == n) {
                break;
            }
            buf[j] = '\0';
            char* tmp = strchr(key,'=');
            if (tmp != nullptr) {
                *tmp = '\0';
                if (strcmp(key,var_name) == 0) {
                    close(fd);
                    return true;
                }
            }
            i = j + 1;
        }
    }
    if (n == -1) {
        perror("smash error: read failed");
        close(fd);
        return false;
    }
    close(fd);
    return false;
}

// TODO: Add your implementation for classes in Commands.h
Command::Command(const char *cmd_line) :  pid(-1), cmd_line(cmd_line), isBackGround(false){
    args = new char*[COMMAND_MAX_ARGS+1]; // +1 for the null terminator
    for (int i = 0; i < COMMAND_MAX_ARGS+1; i++) {
        args[i] = nullptr;
    }
    size_of_args = _parseCommandLine(cmd_line, args);
    cmd_name = args[0];
}

Command::~Command() {
    if (args) {
        for (int i = 0; i < COMMAND_MAX_ARGS+1; i++) {
            free(args[i]);
        }
        delete [] args;
        args = nullptr;
    }
}

BuiltInCommand::BuiltInCommand(const char *cmd_line) : Command(cmd_line) {}

ShowPidCommand::ShowPidCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}

void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << std::endl;
}

ChangeDirCommand::ChangeDirCommand(const char *cmd_line, char **plastPWD) :
    BuiltInCommand(cmd_line),
    plastPwd(plastPWD) {}

void ChangeDirCommand::execute() {
    char** cur_args = getArgs();
    int argc = getArgsLength();
    if (argc == 1) //cd without anything
        return;
    if (argc > 2) {
        std::cerr << "smash error: cd: too many arguments" << std::endl;
        return;
    }
    const char* target = cur_args[1];       // the new directory
    char cur_dir[PATH_MAX];
    if (getcwd(cur_dir, PATH_MAX) == nullptr) {
        perror("smash error: getcwd failed");
        return;
    }

    if (strcmp(cur_args[1], "-") == 0) {
        if (plastPwd == nullptr || *plastPwd == nullptr) {
            std::cerr << "smash error: cd: OLDPWD not set" << std::endl;
            return;
        }
        target = *plastPwd;
    }
    if (chdir(target) == -1) {
        perror("smash error: chdir failed");
        return;
    }
    // assigning new last PWD
    if (plastPwd != nullptr) {
        delete[] *plastPwd;
        char* new_plastPwd = new char[strlen(cur_dir)+1];
        strcpy(new_plastPwd, cur_dir);
        *plastPwd = new_plastPwd;
    }
}

ForegroundCommand::ForegroundCommand(const char *cmd_line, JobsList *jobs) :
    BuiltInCommand(cmd_line),
    F_jobs(jobs) {}

void ForegroundCommand::execute() {
    char** curr_args = getArgs();
    int argc = getArgsLength();
    if (argc > 2) {
        cerr << "smash error: fg: invalid arguments" << endl;
        return;
    }
    JobsList::JobEntry* job;
    int jobId = -1;

    if (argc == 1) {
        job = F_jobs->getLastJob(&jobId);
        if (job == nullptr) {
            string msg = "smash error: fg: jobs list is empty";
            cerr << msg << std::endl;
            return;
        }
    }else { // args == 2
        const char* target = curr_args[1];
        for (int i =0; target[i] != '\0'; i++) {
            if (!isdigit(target[i])) {
                cerr << "smash error: fg: invalid arguments" << endl;
                return;
            }

            jobId = atoi(target);
            job = F_jobs->getLastJob(&jobId);
            if (job == nullptr) {
                cerr << "smash error: fg: job-id " << target << " does not exist"<< endl;
                return;
            }
        }
    }

    pid_t pid = job->command->getPid();
    std::string cmd_line = job->command->getCmdLineStr();
    bool stooped = job->isStopped;

    std::cout << cmd_line << " " << pid << endl;

    SmallShell &sm = SmallShell::getInstance();
    sm.setFgPid(pid);
    sm.setFgCmd(job->command->getCmdLine());

    if (stooped) {
        if (kill(pid , SIGCONT) == -1) {
            perror("smash error: kill failed");
        }
    }
    F_jobs->removeJobById(jobId);

    int status = 0;
    if (waitpid(pid, &status, WUNTRACED) == -1) {
        perror("smash error: waitpid failed");
    }

    sm.setFgPid(-1);
    sm.setFgCmd(nullptr);
}

KillCommand::KillCommand(const char* cmd_line , JobsList *jobs) : BuiltInCommand(cmd_line), K_jobs(jobs) {}

void KillCommand::execute() {
    char** curr_args = getArgs();
    int argc = getArgsLength();
    if (argc != 3) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }
    const char* signumWithFlag = curr_args[1];
    const char* target_job_id = curr_args[2];

    if (signumWithFlag[0] != '-' || signumWithFlag[1] == '\0') {
        cerr << "smash error: kill: invalid arguments" << endl;
    }
    for (int i =1; signumWithFlag[i] != '\0'; i++) {
        if (!isdigit(signumWithFlag[i])) {
            cerr << "smash error: kill: invalid arguments" << endl;
            return;
        }
    }
    for (int i =0; target_job_id[i] != '\0'; i++) {
        if (!isdigit(target_job_id[i])) {
            cerr << "smash error: kill: invalid arguments" << endl;
            return;
        }
    }
    int jobId = atoi(target_job_id);
    JobsList::JobEntry* job = K_jobs->getJobById(jobId);
    if (job == nullptr) {
        cerr << "smash error: job-id " << target_job_id << " does not exist" << endl;
        return;
    }
    int signum = atoi(target_job_id+1);
    pid_t job_pid = job->command->getPid();

    if (kill(job_pid, signum == -1)) {
        perror("smash error: kill failed");
    }

    cout << "signal number " << signum << " was sent to pid " << job_pid <<endl;
}

UnSetEnvCommand::UnSetEnvCommand(const char* cmd_line) : BuiltInCommand(cmd_line) {}

void UnSetEnvCommand::execute() {
    char** curr_args = getArgs();
    int argc = getArgsLength();
    if (argc == 1) {
        cerr << "smash error: unsetenv: not enough arguments" << endl;
        return;
    }
    for (int i = 1; i < argc; i++) {
        const char* var_name = curr_args[i];
        if (!find_env_var(var_name)) {
            std::cerr << "smash error: unsetenv: " << var_name << " does not exist" << endl;
            return;
        }
        if (unsetenv(var_name) == -1) {
            perror("smash error: unsetenv failed");
            return;
        }
    }
}

SmallShell::SmallShell() : jobs_list(new JobsList()){}

SmallShell::~SmallShell() {delete jobs_list;}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line) { // TODO add the & implement with the function they gave us
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    if (cmd_s == ""){return nullptr;} // empty command

    if (firstWord.compare("pwd") == 0) {return new GetCurrDirCommand(cmd_line);}

    if (firstWord.compare("showpid") == 0) {return new ShowPidCommand(cmd_line);}

    if (firstWord.compare("chprompt") == 0) {return new chpromptCommand(cmd_line);}

    if (firstWord.compare("cd") == 0) { return new ChangeDirCommand(cmd_line, &lastPwd);}

    if (firstWord.compare("jobs") == 0) { return new JobsCommand(cmd_line, jobs_list);}

    if (firstWord.compare("fg") == 0) { return new ForegroundCommand(cmd_line, jobs_list);}

    if (firstWord.compare("quit") == 0) {return new QuitCommand(cmd_line, jobs_list);}

    if (firstWord.compare("kill") == 0) {return new KillCommand(cmd_line, jobs_list);}

    if (firstWord.compare("alias") == 0) {return new AliasCommand(cmd_line);}

    if (firstWord.compare("unalias") == 0) {return new UnAliasCommand(cmd_line);}

    if (firstWord.compare("unsetenv") == 0) { return new UnSetEnvCommand(cmd_line);}

    if (firstWord.compare("sysinfo") == 0) { return new SysInfoCommand(cmd_line);}

    if (firstWord.compare("du")){return new DiskUsageCommand(cmd_line);}

    if (firstWord.compare("whoami")){return new WhoAmICommand(cmd_line);}

    // if (firstWord.compare("usbinfo")){return new USBInfoCommand(cmd_line);} // bonus

    return new ExternalCommand(cmd_line);

    return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
    Command* cmd = CreateCommand(cmd_line);
    if (cmd == nullptr){return;} // empty command
    // TODO check for external command
    jobs_list->removeFinishedJobs();
    cmd->execute();
    if (!cmd->getIsBackGround()) {
        delete cmd;
    }
    // Please note that you must fork smash process for some commands (e.g., external commands....)
}