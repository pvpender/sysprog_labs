#include "pipe.h"
#include <unistd.h>
#include <sys/wait.h>

Pipe::Pipe(std::shared_ptr<ICommand> firstCommand, std::shared_ptr<ICommand> secondCommand)
    : _firstCommand{firstCommand}, _secondCommand{secondCommand}
{
}

int Pipe::execute(std::optional<int> readDescriptor, std::optional<int> writeDescriptor)
{
    int pipefd[2];
    int exitCode = 0;
    int status;

    if (pipe(pipefd)) {
        return 1;
    }

    pid_t pid_1 = fork();
    if (pid_1 == 0) {
        close(pipefd[0]);
        exitCode = this->_firstCommand->execute(readDescriptor, pipefd[1]);
        close(pipefd[1]);
        _exit(exitCode);
    }

    pid_t pid_2 = fork();
    if (pid_2 == 0) {
        close(pipefd[1]);
        exitCode = this->_secondCommand->execute(pipefd[0], writeDescriptor);
        close(pipefd[0]);
        _exit(exitCode);
    }


    close(pipefd[0]);
    close(pipefd[1]);

    waitpid(pid_1, NULL, 0);
    waitpid(pid_2, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return exitCode;
}

bool Pipe::exitWasCalled() {
    return this->_exitCall;
}

bool Pipe::isExit() {
    return false;
}