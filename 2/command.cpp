#include "command.h"

Command::Command(command command, output_type outputType, std::string outputFile, bool isBackground)
    : _command{command}, _outputType{outputType}, _outputFile{outputFile}, _isBackground{isBackground}
{
}

int Command::execute(std::optional<int> readDescriptor, std::optional<int> writeDescriptor)
{
    (void)readDescriptor;
    (void)writeDescriptor;
    return 0;
}
