#pragma once

#include<optional>

class ICommand
{
    public:
        virtual ~ICommand() = default;
        virtual int execute(std::optional<int> readDescriptor = std::nullopt, std::optional<int> writeDescriptor = std::nullopt) = 0;
        virtual bool exitWasCalled() = 0;
        virtual bool isExit() = 0;
};

