#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"
#include <cstring>
#include <ctime>
#include <limits.h>
#include <regex>
#include <sys/stat.h>
#include <sys/syscall.h>


// #include "../../../../../AppData/Local/Programs/winlibs-x86_64-posix-seh-gcc-15.1.0-mingw-w64msvcrt-12.0.0-r1/mingw64/x86_64-w64-mingw32/include/limits.h"

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
#define BUFFER_SIZE (8192) //8KB
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

bool findUnquotedToken(const string& cmd_line, const string& token) {
    bool there_is_quote = false;
    for (int i = 0; i < cmd_line.length(); i++) {
        if (cmd_line[i] == '\'') {
            there_is_quote = !there_is_quote;
            continue;
        }
        if (there_is_quote == false && cmd_line.compare(i, token.length(), token) == 0)
            return true;
    }
    return false;
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

static bool read_line(const char* path, std::string& out) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        cerr << "failed to read: " << path << std::endl;
        perror("smash error: open failed");
        return false;
    }
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    if (n == -1) {
        perror("smash error: read failed");
        close(fd);
        return false;
    }
    close(fd);
    buf[n] = '\0';
    char* tmp = strchr(buf, '\n');
    if (tmp != nullptr) {
        *tmp = '\0';
    }
    out.assign(buf);
    return true;
}

static bool read_boot_time(time_t &boot_time) {
    int fd = open("/proc/stat", O_RDONLY);
    if (fd == -1) {
        cerr << "failed to read: /proc/stat" << std::endl;
        perror("smash error: open failed");
        return false;
    }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    if (n < 0) {
        perror("smash error: read failed");
        close(fd);
        return false;
    }
    close(fd);
    buf[n] = '\0';
    const char* start_of_boot = strstr(buf, "btime");
    if (!start_of_boot) {
        std::cout << "smash error: failed to find btime in /proc/stat" << std::endl;
        return false;
    }
    start_of_boot+= 6; // pass the "btime "
    long long val = 0;
    while (*start_of_boot && std::isspace((unsigned char)*start_of_boot)) {
        ++start_of_boot;
    }
    if (!std::isdigit((unsigned char)*start_of_boot)) {
        std::cout << "smash error: invalid btime format" << std::endl;
        return false;
    }
    while (std::isdigit((unsigned char)*start_of_boot)) {
        val = val*10 + (*start_of_boot-'0');
        ++start_of_boot;
    }
    boot_time = static_cast<time_t>(val);
    return true;
}

Command::Command(string O_cmd_line, string org_cmd_line) :  pid(-1), cmd_line(O_cmd_line),
                original_cmd_line(org_cmd_line), isBackGround(false){
    if (_isBackgroundComamnd(cmd_line.c_str())) {
        isBackGround = true;
        std::string tmp = cmd_line;
        _removeBackgroundSign(&tmp[0]);
        clean_cmd_line = _trim(tmp);
    }else {
        clean_cmd_line = _trim(cmd_line);
    }
    args = new char*[COMMAND_MAX_ARGS + 1]; // + 1 for cmd_name
    for (int i = 0; i < COMMAND_MAX_ARGS + 1; i++) {
        args[i] = nullptr;
    }
    size_of_args = _parseCommandLine(clean_cmd_line.c_str(), args);
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

BuiltInCommand::BuiltInCommand(string cmd_line, string org_cmd_line) : Command(cmd_line, org_cmd_line) {}

chpromptCommand::chpromptCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void chpromptCommand::execute() {
    if (this->getArgsLength() == 1) {
        SmallShell::getInstance().resetPrompt();
    }
    else {
        char* newPrompt = this->getArgs()[1];
        SmallShell::getInstance().setPrompt(newPrompt);
    }
}

ShowPidCommand::ShowPidCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}


void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << std::endl;
}

GetCurrDirCommand::GetCurrDirCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void GetCurrDirCommand::execute() {
    char buff[PATH_MAX];
    char* current_dir = getcwd(buff, PATH_MAX);
    if (current_dir != nullptr) {
        std::cout << current_dir << std::endl;
    }
}

ChangeDirCommand::ChangeDirCommand(string cmd_line, string org_cmd_line,  char **plastPWD) :
    BuiltInCommand(cmd_line, org_cmd_line),
    plastPwd(plastPWD) {}

void ChangeDirCommand::execute() {
    char** cur_args = getArgs();
    int argc = getArgsLength();
    if (argc == 1) //cd without args
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

    // SmallShell &sm = SmallShell::getInstance();
    // char* last_pwd = sm.getLastPwd();

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

JobsCommand::JobsCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void JobsCommand::execute() {
    JobsList* jobs_list = SmallShell::getInstance().getJobsList();
    jobs_list->removeFinishedJobs();
    jobs_list->printJobsList();
}

ForegroundCommand::ForegroundCommand(string cmd_line, string org_cmd_line, JobsList *jobs) :
    BuiltInCommand(cmd_line, org_cmd_line),
    F_jobs(jobs) {}

void ForegroundCommand::execute() {
    F_jobs->removeFinishedJobs();
    char** curr_args = getArgs();
    int argc = getArgsLength();
    if (argc > 2) {
        cerr << "smash error: fg: invalid arguments" << endl;
        return;
    }
    JobsList::JobEntry* job;
    int jobId;

    if (argc == 1) {
        job = F_jobs->getLastJob(&jobId);
        if (job == nullptr) {
            string msg = "smash error: fg: jobs list is empty";
            cerr << msg << std::endl;
            return;
        }
    }
    else { // args == 2
        const char* target = curr_args[1];
        for (int i =0; target[i] != '\0'; i++) {
            if (!isdigit(target[i])) {
                cerr << "smash error: fg: invalid arguments" << endl;
                return;
            }
        }
        jobId = stoi(curr_args[1]);
        job = F_jobs->getJobById(jobId);
        if (job == nullptr) {
            cerr << "smash error: fg: job-id " << jobId << " does not exist"<< endl;
            return;
        }
    }

    pid_t pid = job->command->getPid();
    std::string cmd_line = _trim(job->command->getCmdLineStr());
    bool stooped = job->isStopped;

    std::cout << cmd_line << " " << pid << endl;

    SmallShell &sm = SmallShell::getInstance();
    sm.setFgPid(pid);
    sm.setFgCmd(job->command->getCmdLine());

    if (stooped) {
        return;
    }
    F_jobs->removeJobById(jobId);

    int status = 0;
    if (waitpid(pid, &status, WUNTRACED) == -1) {
        perror("smash error: waitpid failed");
    }

    sm.setFgPid(-1);
    sm.setFgCmd(nullptr);
}


QuitCommand::QuitCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void QuitCommand::execute() {
    JobsList* jobs_list = SmallShell::getInstance().getJobsList();
    jobs_list->removeFinishedJobs();
    int argc = getArgsLength();
    if (argc >= 2) {
        SmallShell &sm = SmallShell::getInstance();
        sm.getJobsList()->killAllJobs();
    }
    exit(0);
}

KillCommand::KillCommand(string cmd_line, string org_cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line, org_cmd_line), K_jobs(jobs) {}

void KillCommand::execute() {
    K_jobs->removeFinishedJobs();
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
        return;
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
    int signum = atoi(signumWithFlag+1);
    pid_t job_pid = job->command->getPid();

    if (kill(job_pid, signum) == -1) {
        perror("smash error: kill failed");
        return;
    }

    cout << "signal number " << signum << " was sent to pid " << job_pid <<endl;
}

AliasCommand::AliasCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void AliasCommand::execute() {
    string cmd_line = _trim(this->getCmdLine());
    // int argc = getArgsLength();

    if (cmd_line == "alias") {
        SmallShell::getInstance().printAliases();
        return;
    }

    std::smatch match_results;
    static const regex pattern(R"(^alias ([a-zA-Z0-9_]+)='([^']*)'$)");
    if (!regex_match(cmd_line, match_results, pattern)) {
        cerr << "smash error: alias: invalid alias format" << endl;
        return;
    }

    string name = match_results[1];
    string command = match_results[2];
    SmallShell &sm = SmallShell::getInstance();
    if (!sm.isValidAliasName(name)) {
        cerr << "smash error: alias: " << name << " already exists or is a reserved command" << endl;
        return;
    }
    sm.addAlias(name, command);
}

UnAliasCommand::UnAliasCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void UnAliasCommand::execute() {
    SmallShell &sm = SmallShell::getInstance();
    char** args = getArgs();
    int argc = getArgsLength();
    if (argc == 1) {
        cerr << "smash error: unalias: not enough arguments" << endl;
        return;
    }
    for (int i =1; i < argc; i++) {
        pair<string, string>* curr_alias = sm.findAlias(args[i]);
        if (curr_alias == nullptr) {
            cerr << "smash error: unalias: " << args[i] << " alias does not exist" << endl;
            return;
        }
        sm.removeAlias(args[i]);
    }
}

UnSetEnvCommand::UnSetEnvCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

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

SysInfoCommand::SysInfoCommand(string cmd_line, string org_cmd_line) : BuiltInCommand(cmd_line, org_cmd_line) {}

void SysInfoCommand::execute() {
    std::string system;
    std::string hostname;
    std::string kernel;

    if (!read_line("/proc/sys/kernel/ostype",system)) {
        return;
    }
    if (!read_line("/proc/sys/kernel/hostname",hostname)) {
        return;
    }
    if (!read_line("/proc/sys/kernel/osrelease",kernel)) {
        return;
    }

    time_t boot_time;
    if (!read_boot_time(boot_time)) {
        return;
    }

    struct tm bt;
    if (!localtime_r(&boot_time,&bt)) {
        perror("smash error: localtime_r failed");
        return;
    }

    char time_buf[64];
    if (strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &bt) == 0) {
        perror("smash error: strftime failed");
        return;
    }

    cout << "System: " << system << endl
         << "Hostname: " << hostname << endl
         << "Kernel: " << kernel << endl
         << "Architecture: x86_64" << endl
         << "Boot Time: " << time_buf << endl;
}

ExternalCommand::ExternalCommand(string cmd_line, string org_cmd_line) : Command(cmd_line, org_cmd_line) {
    char** curr_args = getArgs();
    int argc = getArgsLength();
    for (int i =0; i <  argc; i++) {
        for (int j = 0; j <strlen(curr_args[i]); j++) {
            if (curr_args[i][j] == '*' || curr_args[i][j] == '?') {
                setComplex(true);
                return;
            }
        }
    }
}

void ExternalCommand::execute() {
    char** curr_args = getArgs();
    char*  cmd = curr_args[0];
    pid_t pid = fork();
    auto &smash = SmallShell::getInstance();
    auto *jobs = smash.getJobsList();
    if (pid == -1) {
        perror("smash error: fork failed");
        return;
    }
    if (pid == 0) { // child
        setpgrp();      // change the group id

        if (!getIsComplex()){
            if (execvp(cmd, curr_args) == -1) {
                perror("smash error: execvp failed");
                _exit(1);
            }
        }else {
            if (execl("/bin/bash","bash", "-c", getCleanCmdLine() ,(char*)nullptr) == -1) {
                perror("smash error: execl failed");
                _exit(1);
            }
        }
    }
    else { // father
        setPid(pid);        // set command pid
        if (!getIsBackGround()) { // foreground command
            smash.setFgPid(pid);
            smash.setFgCmd(getCmdLine());
            if (waitpid(pid,NULL,0)== -1) {
                perror("smash error: waitpid failed");
                return;
            }
            smash.clear_fg_pid();
            smash.setFgCmd(nullptr);
        }
        else {
            if (jobs) {
                jobs->addJob(this);
            }
        }
    }
}

RedirectionCommand::RedirectionCommand(string cmd_line, string org_cmd_line) :
                                        Command(cmd_line, cmd_line), is_append(false) {
    setBackGround(false);
    std::string tmp = getCleanCmdLineStr();
    size_t s = tmp.find_first_of('>');
    cmd_command = _trim(tmp.substr(0, s));
    if (s+1 < tmp.size() && tmp[s+1] == '>') {
        is_append = true;
        file_name = _trim(tmp.substr(s+2));
    }else {
        file_name = _trim(tmp.substr(s+1));
    }
}

void RedirectionCommand::execute() {
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout == -1) {
        perror("smash error: dup failed");
        return;
    }

    int flags = O_WRONLY | O_CREAT | (is_append? O_APPEND : O_TRUNC);
    int fd = open(file_name.c_str(), flags, 0666);
    if (fd == -1) {
        perror("smash error: open failed");
        close(saved_stdout);
        return;
    }
    if (dup2(fd,STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
        close(fd);
        dup2(saved_stdout,STDOUT_FILENO);
        close(saved_stdout);
        return;
    }
    close(fd);      // cout of the command is fd aka the file we want

    SmallShell &smash = SmallShell::getInstance();
    Command* cmd = smash.CreateCommand(cmd_command.c_str());
    if (cmd != nullptr) {
        cmd->execute();
        delete cmd;
    }
    if (dup2(saved_stdout,STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
    }
    close (saved_stdout);
}

PipeCommand::PipeCommand(string cmd_line, string org_cmd_line) : Command(cmd_line, org_cmd_line), isError(false)  {
    setBackGround(false);
    std::string tmp = getCleanCmdLineStr();
    size_t s = tmp.find_first_of('|');
    first_command = _trim(tmp.substr(0, s));
    if (s+1 < tmp.size() && tmp[s+1] == '&') {
        isError = true;
        second_command = _trim(tmp.substr(s+2));
    }else {
        second_command = _trim(tmp.substr(s+1));
    }
}

void PipeCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("smash error: pipe failed");
        return;
    }

    pid_t cmd1_pid = fork();
    if (cmd1_pid == -1) {
        perror("smash error: fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (cmd1_pid == 0) { // child 1
        if (!isError) {
            if (dup2(pipefd[1],STDOUT_FILENO)== -1) {
                perror("smash error: dup2 failed");
                close(pipefd[1]);
                close(pipefd[0]);
                _exit(1);
            }
        }else {
            if (dup2(pipefd[1],STDERR_FILENO)==-1) {
                perror("smash error: dup2 failed");
                close(pipefd[1]);
                close(pipefd[0]);
                _exit(1);
            }
        }
        close(pipefd[0]);
        close(pipefd[1]);

        Command* cmd = smash.CreateCommand(first_command.c_str());
        if (cmd != nullptr) {
            cmd->execute();
            delete cmd;
        }
        _exit(0);
    }

    pid_t cmd2_pid = fork();
    if (cmd2_pid == -1) {
        perror("smash error: fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (cmd2_pid == 0) {    // child 2
        if (dup2(pipefd[0],STDIN_FILENO) == -1) {
            perror("smash error: dup2 failed");
            close(pipefd[0]);
            close(pipefd[1]);
            _exit(1);
        }
        close(pipefd[0]);
        close(pipefd[1]);

        Command* cmd2 = smash.CreateCommand(second_command.c_str());
        if (cmd2 != nullptr) {
            cmd2->execute();
            delete cmd2;
        }
        _exit(0);
    }
    // parent
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(cmd1_pid,NULL,0);
    waitpid(cmd2_pid,NULL,0);
}

DiskUsageCommand::DiskUsageCommand(string cmd_line, string org_cmd_line)
                                    : Command(cmd_line, org_cmd_line) {}

off_t DiskUsageCommand::getFileSize(const string &file_path) {
    struct stat st{};
    if (lstat(file_path.c_str(), &st) == -1) {
        perror("smash error: lstat failed");
        return -1;
    }
    return (st.st_blocks * 512);
}

off_t DiskUsageCommand::getDiskUsage(const string& dir_path) {
    struct stat st{};
    if (lstat(dir_path.c_str(), &st) == -1) {
        perror("smash error: lstat failed");
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        return getFileSize(dir_path);
    }

    int fd = open(dir_path.c_str(), O_RDONLY | O_DIRECTORY); //getdents64 receives fd of an open file.
    if (fd == -1) {
        perror("smash error: open failed");
        return -1;
    }

    off_t total_disk_usage = getFileSize(dir_path);
    if (total_disk_usage == -1) {
        close(fd);
        return -1;
    }
    char buffer[BUFFER_SIZE];
    int bytes_read;
    while (true) {
        bytes_read = syscall(SYS_getdents64, fd, buffer, BUFFER_SIZE);
        if (bytes_read == 0) { // No more entries
            break;
        }
        else if (bytes_read == -1) {
            perror("smash error: getdents64 failed");
            close(fd);
            return -1;
        }
        int pos = 0;
        while (pos < bytes_read) {
            struct linux_dirent64* current_file = (struct linux_dirent64*)(buffer + pos);
            string name = current_file->d_name;

            if (name == "." || name == "..") {
                pos += current_file->d_reclen;
                continue;
            }

            string fullPath = dir_path + "/" + name;
            if (current_file->d_type == DT_DIR) {
                off_t additional_disk_usage = getDiskUsage(fullPath);
                if (additional_disk_usage == -1) {
                    close(fd);
                    return -1;
                }
                total_disk_usage += additional_disk_usage;
            }
            else {
                off_t additional_disk_usage = getFileSize(fullPath);
                if (additional_disk_usage == -1) { //lstat failed
                    if (close(fd) == -1) {
                        perror("smash error: close failed");
                        return -1;
                    }
                    return -1;
                }
                total_disk_usage += additional_disk_usage;
            }
            pos += current_file->d_reclen;
        }
    }
    if (close(fd) == -1) {
        perror("smash error: close failed");
        return -1;
    }
    return total_disk_usage;
}

void DiskUsageCommand::execute() {
    char** args = getArgs();
    int argv = getArgsLength();
    if (argv > 2) {
        cerr << "smash error: du: too many arguments" << endl;
        return;
    }
    char cur_dir[PATH_MAX];
    if (argv == 1) {
        if (getcwd(cur_dir, PATH_MAX) == nullptr) {
            perror("smash error: getcwd failed");
            return;
        }
    }
    else {
        strcpy(cur_dir, args[1]);
    }
    off_t total_disk_usage = getDiskUsage(cur_dir); //by bytes
    if (total_disk_usage != -1) {
        off_t total_disk_usage_rounded = (total_disk_usage + 1023) / 1024;
        cout <<"Total disk usage: " << total_disk_usage_rounded << " KB" << endl;
    }
}

WhoAmICommand::WhoAmICommand(string cmd_line, string org_cmd_line) : Command(cmd_line, org_cmd_line) {}

void WhoAmICommand::execute() {
    uid_t uid = getuid();
    gid_t gid = getgid();
    string str_uid = to_string(uid);
    string str_gid = to_string(gid);
    string user_name;
    string home_dir;

    const char* PASSWD_PATH = "/etc/passwd";
    char buffer[1024]; //1KB
    ssize_t bytes_read = 0;
    bool foundUser = false;
    string curr_line;
    string trimmed_str;

    int fd = open(PASSWD_PATH, O_RDONLY);
    if (fd == -1) {
        perror("smash error: open failed");
    }
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        string updated_buffer = trimmed_str.append(buffer, bytes_read);
        size_t new_line_pos;
        while ((new_line_pos = updated_buffer.find('\n')) != string::npos) {
            curr_line = updated_buffer.substr(0, new_line_pos);
            updated_buffer.erase(0, new_line_pos + 1);

            string curr_user_info[7];
            for (int i = 0; i < 7; i++) {
                size_t colon_pos = curr_line.find(':');
                if (colon_pos == string::npos) {    //shell_path, last field of current user
                    curr_user_info[i] = curr_line;
                    break;
                }
                curr_user_info[i] = curr_line.substr(0, colon_pos);
                curr_line = curr_line.substr(colon_pos + 1);
            }
            if (curr_user_info[2] == str_uid) {
                user_name = curr_user_info[0];
                home_dir = curr_user_info[5];
                foundUser = true;
            }
        }
        if (foundUser == true) {
            break;
        }
        trimmed_str = updated_buffer;
    }
    if (bytes_read == -1) {
        close(fd);
        perror("smash error: read failed");
    }

    close(fd);

    cout << user_name << endl;
    cout << str_uid << endl;
    cout << str_gid << endl;
    cout << home_dir << endl;
}


SmallShell::SmallShell() : jobs_list(new JobsList()) ,fg_cmd(nullptr) {}

SmallShell::~SmallShell() {
    delete jobs_list;
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(string cmd_line) {
    std::string cmd_s = _trim(std::string(cmd_line));
    if (cmd_s.empty()) {
        return nullptr;
    }
    size_t pos = cmd_s.find_first_of(" \n&");
    std::string firstWord = cmd_s.substr(0, pos);
    SmallShell &sm = SmallShell::getInstance();
    string original_cmd_line = cmd_line;

    pair<string, string>* alias = sm.findAlias(firstWord);
    if (alias != nullptr) {
        cmd_line = sm.restoreCmd(alias->second, cmd_line); //cmd_line contains the full command (before alias)
    }

    if (findUnquotedToken(cmd_line ,">") == true) {return new RedirectionCommand(cmd_line, original_cmd_line);}

    if (findUnquotedToken(cmd_line ,"|") == true) {return new PipeCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("pwd") == 0) {return new GetCurrDirCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("showpid") == 0) {return new ShowPidCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("chprompt") == 0) {return new chpromptCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("cd") == 0) { return new ChangeDirCommand(cmd_line, original_cmd_line, &lastPwd);}

    if (firstWord.compare("jobs") == 0) { return new JobsCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("fg") == 0) { return new ForegroundCommand(cmd_line, original_cmd_line, jobs_list);}

    if (firstWord.compare("quit") == 0) {return new QuitCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("kill") == 0) {return new KillCommand(cmd_line, original_cmd_line, jobs_list);}

    if (firstWord.compare("alias") == 0) {return new AliasCommand(cmd_line, original_cmd_line);}

     if (firstWord.compare("unalias") == 0) {return new UnAliasCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("unsetenv") == 0) { return new UnSetEnvCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("sysinfo") == 0) { return new SysInfoCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("du") == 0){return new DiskUsageCommand(cmd_line, original_cmd_line);}

    if (firstWord.compare("whoami") == 0){return new WhoAmICommand(cmd_line, original_cmd_line);}

    // if (firstWord.compare("usbinfo")){return new USBInfoCommand(cmd_line, original_cmd_line);} // bonus

    return new ExternalCommand(cmd_line, original_cmd_line);

    return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
    Command* cmd = CreateCommand(cmd_line);
    if (cmd == nullptr){return;} // empty command
    jobs_list->removeFinishedJobs();
    cmd->execute();
    if (!cmd->getIsBackGround()) {
        delete cmd;
    }
    // Please note that you must fork smash process for some commands (e.g., external commands....)
}