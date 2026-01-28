#pragma once

#include <string>
#include <optional>

#include "parser.h"
#include "icommand.h"

class Command : public ICommand {
    public:
        Command() = delete;
        Command(command command, output_type outputType = output_type::OUTPUT_TYPE_STDOUT, std::string outputFile = "", bool isBackground = false);
        int execute(std::optional<int> readDescriptor = std::nullopt, std::optional<int> writeDescriptor = std::nullopt) override;
        bool exitWasCalled() override;
        bool isExit() override;

    private:
        command _command;
        std::vector<char*> _exeArgs;
        output_type _outputType;
        std::string _outputFile;
        bool _isBackground;
        bool _exitWasCalled;
        bool _isExit = false;
};