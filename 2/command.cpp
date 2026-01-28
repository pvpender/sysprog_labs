#include "command.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

Command::Command(command command, output_type outputType, std::string outputFile, bool isBackground)
    : _command{command}, _outputType{outputType}, _outputFile{outputFile}, _isBackground{isBackground}
{
    this->_exeArgs.push_back(const_cast<char*>(this->_command.exe.c_str()));
    for (std::size_t i = 0; i < this->_command.args.size(); ++i)
        this->_exeArgs.push_back(const_cast<char*>(this->_command.args[i].c_str()));
    
    this->_exeArgs.push_back(NULL);

    if (command.exe == "exit") {
        this->_isExit = true;
    }
}

int Command::execute(std::optional<int> readDescriptor, std::optional<int> writeDescriptor)
{
    if (this->_command.exe == "cd") {
        if (chdir(this->_exeArgs[1]) == 0) {
            return 0;
        }

        return 1;
    }

    if ((this->_command.exe == "exit") && (!writeDescriptor.has_value())) {
        int exitCode = 0;
        if (!this->_command.args.empty()) {
            exitCode = std::stoi(this->_command.args.front());
        }

        this->_exitWasCalled = true;
        
        return exitCode;
    }

    pid_t childPid = fork();

    if (childPid == 0) {
        if (readDescriptor.has_value()) {
            dup2(readDescriptor.value(), STDIN_FILENO);
            close(readDescriptor.value());
        }
    
        if (writeDescriptor.has_value()) {
            dup2(writeDescriptor.value(), STDOUT_FILENO);
            close(writeDescriptor.value());
        }
    
        int fd;
        int flags;
        mode_t mode = S_IRWXU;
    
        if (this->_outputType == output_type::OUTPUT_TYPE_FILE_NEW) {
            flags = O_CREAT | O_WRONLY | O_TRUNC;
        } else {
            flags = O_CREAT | O_WRONLY | O_APPEND;
        }
    
        if (this->_outputType != output_type::OUTPUT_TYPE_STDOUT) {
            fd = open(this->_outputFile.c_str(), flags, mode);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (readDescriptor.has_value()) close(readDescriptor.value());
        if (writeDescriptor.has_value()) close(writeDescriptor.value());

        execvp(this->_exeArgs[0], this->_exeArgs.data());
        exit(1);
    }

    if (readDescriptor.has_value()) close(readDescriptor.value());
    if (writeDescriptor.has_value()) close(writeDescriptor.value());

    int status;

    waitpid(childPid, &status, this->_isBackground ? WNOHANG: 0);
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
        return 0;
    }

    return WEXITSTATUS(status);
}

bool Command::exitWasCalled() {
    return this->_exitWasCalled;
}

bool Command::isExit() {
    return this->_isExit;
}