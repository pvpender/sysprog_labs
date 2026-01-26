#pragma once

#include <optional>
#include <functional>
#include <memory>

#include "command.h"
#include "icommand.h"


class Pipe : public ICommand {
    public:
        Pipe() = delete;
        Pipe(std::shared_ptr<ICommand> firstCommand, std::shared_ptr<ICommand> secondCommand);
        int execute(std::optional<int> readDescriptor = std::nullopt, std::optional<int> writeDescriptor = std::nullopt) override;

    private:
        std::shared_ptr<ICommand> _firstCommand;
        std::shared_ptr<ICommand> _secondCommand;
};