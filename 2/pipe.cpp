#include "pipe.h"

Pipe::Pipe(std::shared_ptr<ICommand> firstCommand, std::shared_ptr<ICommand> secondCommand)
    : _firstCommand{firstCommand}, _secondCommand{secondCommand}
{
}

int Pipe::execute(std::optional<int> readDescriptor, std::optional<int> writeDescriptor)
{
    (void)readDescriptor;
    (void)writeDescriptor;
    return 0;
}
