#pragma once

#include<optional>

class ICommand
{
    public:
        virtual int execute(std::optional<int> readDescriptor = std::nullopt, std::optional<int> writeDescriptor = std::nullopt) = 0;
};

