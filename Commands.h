// Ver: 04-11-2025
#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_
// #pragma once TODO think about it
#include <list>
#include <utility>
#include <vector>
#include <sys/wait.h>
#include <algorithm>
#include <cstdint>
#include <dirent.h>

using namespace std;

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

class Command {
    char** args = nullptr;
    const char* cmd_name;
    int size_of_args;
    pid_t pid;
    std::string cmd_line; //cmd line that contains the full command (before alias)
    std::string original_cmd_line;
    std::string clean_cmd_line; // without & and spaces at the start and end
    bool isBackGround;
public:
    Command(string cmd_line, string org_cmd_line);

    virtual ~Command();

    virtual void execute() = 0;

    //virtual void prepare();
    //virtual void cleanup();
    char** getArgs() {return args;}
    int getArgsLength() {return size_of_args;}
    pid_t getPid() {return pid;}
    void setPid(pid_t O_pid){pid = O_pid;}
    const char* getCmdLine() {return cmd_line.c_str();}
    const char* getOriginalCmdLine() {return original_cmd_line.c_str();}
    const char* getCleanCmdLine() {return clean_cmd_line.c_str();}
    std::string getCmdLineStr() {return cmd_line;}
    std::string getCleanCmdLineStr() {return clean_cmd_line;}
    void setBackGround(bool background) {isBackGround = background;}
    bool getIsBackGround() const {return isBackGround;}
};

class BuiltInCommand : public Command {
public:
    BuiltInCommand(string cmd_line, string org_cmd_line);

    virtual ~BuiltInCommand() {
    }

    virtual void execute() = 0;
};

class ExternalCommand : public Command {
    bool complex = false;
public:
    ExternalCommand(string cmd_line, string org_cmd_line);

    virtual ~ExternalCommand() {}

    void execute() override;

    bool getIsComplex() const {return complex;}

    void setComplex(bool O_complex) {complex = O_complex;}
};

                            // special commands

class RedirectionCommand : public Command {
    std::string cmd_command;
    std::string file_name;
    bool is_append;

public:
    explicit RedirectionCommand(string cmd_line, string org_cmd_line);

    virtual ~RedirectionCommand() {}

    void execute() override;
};

class PipeCommand : public Command {
    std::string first_command;
    std::string second_command;
    bool isError;
public:
    PipeCommand(string cmd_line, string org_cmd_line);

    virtual ~PipeCommand() {
    }

    void execute() override;
};

class DiskUsageCommand : public Command {
public:
    DiskUsageCommand(string cmd_line, string org_cmd_line);

    virtual ~DiskUsageCommand() {}

    void execute() override;

    static off_t getFileSize(const string& file_path);

    static off_t getDiskUsage(const string& dir_path);

};

class WhoAmICommand : public Command {
public:
    WhoAmICommand(string cmd_line, string org_cmd_line);

    virtual ~WhoAmICommand() {
    }

    void execute() override;
};

class USBInfoCommand : public Command {
    // TODO: Add your data members **BONUS: 10 Points**
public:
    USBInfoCommand(string cmd_line, string org_cmd_line);

    virtual ~USBInfoCommand() {
    }

    void execute() override;
};


class JobsList {
public:
    class JobEntry {
    public:
        int job_id;
        Command *command;
        bool isStopped = false;
        JobEntry(int jobId, Command *command, bool is_stopped) : job_id(jobId), command(command), isStopped(is_stopped) {}
        int getJobId() {return job_id;}
    };
    int max_job_id = 1;
private:
    std::list<JobEntry *> jobs;
public:
    JobsList() : jobs(){}

    ~JobsList() {
        for (auto j : jobs) {
            delete j->command;
            delete j;
        }
        jobs.clear();
    }

    void addJob(Command *cmd) {  // check if the size greater than 100
        removeFinishedJobs();
        if (jobs.size() >= 100) { // there is 100 jobs
            delete cmd;
            return;
        }
        auto job = new JobEntry(max_job_id, cmd, false);
        jobs.push_back(job);
        max_job_id++;
    }

    void printJobsList() {
        removeFinishedJobs();
        for (auto job: jobs) {
            std::cout << '[' << job->getJobId() << "] " << job->command->getOriginalCmdLine() << std::endl;
        }
    }

    void killAllJobs() {
        removeFinishedJobs();
        std::cout << "smash: sending SIGKILL signal to " << jobs.size() << " jobs:" << std::endl;
        for (auto j : jobs) {
            pid_t pid = j->command->getPid();
            std::cout << pid << ": " << j->command->getCmdLine() << std::endl;
            if (kill(pid, SIGKILL) == -1) {
                perror("smash error: kill failed");
            }
            delete j->command;
            delete j;
        }
        jobs.clear();
    }

    void removeFinishedJobs() {
        for (auto it =jobs.begin(); it != jobs.end();) {
            auto j = *it;
            pid_t pid = j->command->getPid();
            int status = 0;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == 0) {
                ++it;
            }
            else if (result == -1) {
                perror("smash error: waitpid failed");
                delete j->command;
                delete j;
                it = jobs.erase(it);
            } else {
                if (WIFEXITED(status)|| WIFSIGNALED(status)) {
                    delete j->command;
                    delete j;
                    it = jobs.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    JobEntry *getJobById(int jobId) {
        removeFinishedJobs();
        if (jobId <= 0)
            return nullptr;
        for (auto j: jobs) {
            if (j->job_id == jobId)
                return j;
        }
        return nullptr;
    }

    void removeJobById(int jobId) {
        for (auto it = jobs.begin(); it != jobs.end(); ++it) {
            auto job = *it;
            if (job->job_id == jobId) {
                delete job->command;
                delete job;
                it = jobs.erase(it);
                break;
            }
        }
    }


    //lastJobId will hold the maximum Job ID found in the list, -1 if the list is empty.
    JobEntry *getLastJob(int *lastJobId) {
        if (getJobsCount() == 0) {
            if (lastJobId) {
                *lastJobId = -1;
            }
            return nullptr;
        }
        int maxId = -1;
        JobEntry* last = nullptr;

        for (JobEntry* job : jobs) {
            if (job->job_id > maxId) {
                maxId = job->job_id;
                last = job;
            }
        }
        if (lastJobId) {
            *lastJobId = maxId;
        }
        return last;
    }

    JobEntry *getLastStoppedJob(int *jobId) { // I think we don't need this function because there is no bg function
        if (getJobsCount() == 0) {
            if (jobId) {
                *jobId = -1;
            }
            return nullptr;
        }
        JobEntry* last = nullptr;
        int maxId = -1;
        for (JobEntry* job : jobs) {
            if (job->job_id > maxId) {
                last = job;
                maxId = job->job_id;
            }

        }
        if (jobId != nullptr)
            *jobId = max_job_id;
        return last;
    }

    int getJobsCount() {return jobs.size();}
};


class chpromptCommand : public BuiltInCommand {
public:
    chpromptCommand(string cmd_line, string org_cmd_line);

    virtual ~chpromptCommand() {}

    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
public:
    ShowPidCommand(string cmd_line, string org_cmd_line);

    virtual ~ShowPidCommand() {
    }

    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
public:
    GetCurrDirCommand(string cmd_line, string org_cmd_line);

    virtual ~GetCurrDirCommand() {
    }

    void execute() override;
};


class ChangeDirCommand : public BuiltInCommand {
    char** plastPwd;
public:
    ChangeDirCommand(string cmd_line, string org_cmd_line, char **plastPwd);

    virtual ~ChangeDirCommand() {}

    void execute() override;
};

class JobsCommand : public BuiltInCommand {
public:
    JobsCommand(string cmd_line, string org_cmd_line);

    virtual ~JobsCommand() {
    }

    void execute() override;
};


class ForegroundCommand : public BuiltInCommand {
    JobsList *F_jobs;

public:
    ForegroundCommand(string cmd_line, string org_cmd_line, JobsList *jobs);

    virtual ~ForegroundCommand() {}

    void execute() override;
};


class QuitCommand : public BuiltInCommand {
public:
    QuitCommand(string cmd_line, string org_cmd_line);

    virtual ~QuitCommand() {
    }

    void execute() override;
};


class KillCommand : public BuiltInCommand {
    JobsList *K_jobs;
public:
    KillCommand(string cmd_line, string org_cmd_line, JobsList *jobs);

    virtual ~KillCommand() {
    }

    void execute() override;
};


class AliasCommand : public BuiltInCommand {
public:
    AliasCommand(string cmd_line, string org_cmd_line);

    virtual ~AliasCommand() {
    }

    void execute() override;
};

class UnAliasCommand : public BuiltInCommand {
public:
    UnAliasCommand(string cmd_line, string org_cmd_line);

    virtual ~UnAliasCommand() {
    }

    void execute() override;
};

class UnSetEnvCommand : public BuiltInCommand {
public:
    UnSetEnvCommand(string cmd_line, string org_cmd_line);

    virtual ~UnSetEnvCommand() {
    }

    void execute() override;
};

class SysInfoCommand : public BuiltInCommand {
public:
    SysInfoCommand(string cmd_line, string org_cmd_line);

    virtual ~SysInfoCommand() {
    }

    void execute() override;
};

class SmallShell {
private:
    string prompt = "smash";
    JobsList *jobs_list;
    char *lastPwd = nullptr;
    pid_t fg_pid = -1;
    const char* fg_cmd;
    list<pair<string, string>> aliases;

    SmallShell();

public:
    Command *CreateCommand(string cmd_line);

    SmallShell(SmallShell const &) = delete; // disable copy ctor
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance() // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    ~SmallShell();

    string getPrompt() const {
        return prompt;
    }

    char* getLastPwd() const {
        return lastPwd;
    }

    void setPrompt(char* newPrompt) {
        prompt = string(newPrompt);
    }

    void resetPrompt() {
        prompt = "smash";
    }

    list<pair<string, string>>  getAliases() const {
        return aliases;
    }

    bool isValidAliasName(const std::string& name) {

        //Making sure it isn't used
        for ( auto& alias : aliases )
            if (alias.first == name) return false;

        //Making sure it isn't reserved keyword
        vector<string> reservedKeywords = {"chprompt", "showpid", "pwd", "cd", "jobs", "fg"
        , "quit", "kill", "alias", "unalias", "unsetenv", "sysinfo", "du", "whoami"
        , "usbinfo"};

        for (auto& resWord : reservedKeywords)
            if (resWord == name) return false;

        return true;
    }

    void addAlias(string name, string original_cmd) {
        if (isValidAliasName(name))
            aliases.push_back(make_pair(name, original_cmd));
    }


    pair<string, string>* findAlias(string name) {
        for (auto& alias : aliases) {
            if (alias.first == name) return &alias;
        }
        return nullptr;
    }

    void removeAlias(string name) {
        auto it = find_if(aliases.begin(), aliases.end(),
                          [&name](const pair<string, string>& p) {return (p.first == name);});
        if (it != aliases.end()) {
            aliases.erase(it);
        }
    }

    string restoreCmd(string alias_command, string cmd_line) {
        size_t pos = cmd_line.find_first_of(" \t");
        //no additional params
        if (pos == string::npos) {
            return alias_command;
        }
        return alias_command + cmd_line.substr(pos);
    }

    void printAliases() {
        for ( auto& alias : aliases ) {
            cout << alias.second << endl;
        }
    }

    void executeCommand(const char *cmd_line);

    pid_t getFgPid() const {return fg_pid;}
    void setFgPid(pid_t o_fg_pid) {fg_pid = o_fg_pid;}
    void clear_fg_pid() {fg_pid = -1;}

    std::string getFgCmd() const {
        if (fg_cmd) {
            return std::string(fg_cmd);
        }
        return std::string();
    }
    void setFgCmd(const char* o_fg_cmd) {fg_cmd = o_fg_cmd;}

    JobsList *getJobsList() {return jobs_list;}
};

#endif //SMASH_COMMAND_H_
