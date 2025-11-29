// Ver: 04-11-2025
#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_
// #pragma once TODO think about it
#include <list>
#include <utility>
#include <vector>
#include <sys/wait.h>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)

class Command {
    char** args = nullptr;
    const char* cmd_name;
    int size_of_args;
    pid_t pid;
    std::string cmd_line;
    bool isBackGround;
public:
    Command(const char *cmd_line);

    virtual ~Command();

    virtual void execute() = 0;

    //virtual void prepare();
    //virtual void cleanup();
    char** getArgs() {return args;}
    int getArgsLength() {return size_of_args;}
    pid_t getPid() {return pid;}
    void setPid(pid_t O_pid){pid = O_pid;}
    const char* getCmdLine() {return cmd_line.c_str();}
    std::string getCmdLineStr() {return cmd_line;}
    void setBackGround(bool background) {isBackGround = background;}
    bool getIsBackGround() const {return isBackGround;}
    // TODO: Add your extra methods if needed
};

class BuiltInCommand : public Command {
public:
    BuiltInCommand(const char *cmd_line);

    virtual ~BuiltInCommand() {
    }

    virtual void execute() = 0;
};

class ExternalCommand : public Command {
public:
    ExternalCommand(const char *cmd_line);

    virtual ~ExternalCommand() {
    }

    void execute() override;
};

                            // special commands

class RedirectionCommand : public Command {
    // TODO: Add your data members
public:
    explicit RedirectionCommand(const char *cmd_line);

    virtual ~RedirectionCommand() {
    }

    void execute() override;
};

class PipeCommand : public Command {
    // TODO: Add your data members
public:
    PipeCommand(const char *cmd_line);

    virtual ~PipeCommand() {
    }

    void execute() override;
};

class DiskUsageCommand : public Command {
public:
    DiskUsageCommand(const char *cmd_line);

    virtual ~DiskUsageCommand() {
    }

    void execute() override;
};

class WhoAmICommand : public Command {
public:
    WhoAmICommand(const char *cmd_line);

    virtual ~WhoAmICommand() {
    }

    void execute() override;
};

class USBInfoCommand : public Command {
    // TODO: Add your data members **BONUS: 10 Points**
public:
    USBInfoCommand(const char *cmd_line);

    virtual ~USBInfoCommand() {
    }

    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand {
    char** plastPwd;
public:
    ChangeDirCommand(const char *cmd_line, char **plastPwd);

    virtual ~ChangeDirCommand() {}

    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
public:
    GetCurrDirCommand(const char *cmd_line);

    virtual ~GetCurrDirCommand() {
    }

    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
public:
    ShowPidCommand(const char *cmd_line);

    virtual ~ShowPidCommand() {
    }

    void execute() override;
};

class chpromptCommand : public BuiltInCommand {
public:
    chpromptCommand(const char *cmd_line);

    virtual ~chpromptCommand() {}

    void execute() override;
};

class JobsList;

class QuitCommand : public BuiltInCommand {
    // TODO: Add your data members
public:
    QuitCommand(const char *cmd_line, JobsList *jobs);

    virtual ~QuitCommand() {
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

    ~JobsList() = default;

    void addJob(Command *cmd, bool isStopped = false) {  // check if the size greater than 100
        removeFinishedJobs();
        if (jobs.size() > 100) { // there is 100 jobs
            return;
        }
        auto job = new JobEntry(max_job_id, cmd, isStopped);
        jobs.push_back(job);
        max_job_id++;
    }

    void printJobsList() {
        removeFinishedJobs();
        for (auto job: jobs) {
            pid_t pid = job->command->getPid();
            std::cout << '[' << job->getJobId() << ']' << job->command->getCmdLine() << std::endl;
        }
    }

    void killAllJobs() {
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
            if (result == -1) {
                perror("smash error: waitpid failed"); // TODO maybe we should delete the command
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

    int getJobsCount() {return jobs.size();};


    // TODO: Add extra methods or modify existing ones as needed
};

class JobsCommand : public BuiltInCommand {
    // TODO: Add your data members
public:
    JobsCommand(const char *cmd_line, JobsList *jobs);

    virtual ~JobsCommand() {
    }

    void execute() override;
};

class KillCommand : public BuiltInCommand {
    JobsList *K_jobs;
public:
    KillCommand(const char *cmd_line, JobsList *jobs);

    virtual ~KillCommand() {
    }

    void execute() override;
};

class ForegroundCommand : public BuiltInCommand {
    JobsList *F_jobs;

public:
    ForegroundCommand(const char *cmd_line, JobsList *jobs);

    virtual ~ForegroundCommand() {}

    void execute() override;
};

class AliasCommand : public BuiltInCommand {
public:
    AliasCommand(const char *cmd_line);

    virtual ~AliasCommand() {
    }

    void execute() override;
};

class UnAliasCommand : public BuiltInCommand {
public:
    UnAliasCommand(const char *cmd_line);

    virtual ~UnAliasCommand() {
    }

    void execute() override;
};

class UnSetEnvCommand : public BuiltInCommand {
public:
    UnSetEnvCommand(const char *cmd_line);

    virtual ~UnSetEnvCommand() {
    }

    void execute() override;
};

class SysInfoCommand : public BuiltInCommand {
public:
    SysInfoCommand(const char *cmd_line);

    virtual ~SysInfoCommand() {
    }

    void execute() override;
};

class SmallShell {
private:
    JobsList *jobs_list;
    char *lastPwd = nullptr;
    pid_t fg_pid = -1;
    const char* fg_cmd;
    SmallShell();

public:
    Command *CreateCommand(const char *cmd_line);

    SmallShell(SmallShell const &) = delete; // disable copy ctor
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance() // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    ~SmallShell();

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
    // TODO: add extra methods as needed
};

#endif //SMASH_COMMAND_H_
