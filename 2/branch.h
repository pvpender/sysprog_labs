#pragma once

#include <functional>
#include <optional>
#include <variant>
#include <memory>

#include "command.h"
#include "icommand.h"

class Branch {
    public:
        using SecondCommand = std::variant<std::shared_ptr<ICommand>, std::shared_ptr<Branch>>;

        Branch() = delete;
        Branch(std::shared_ptr<ICommand> firstCommand, bool executeOnFail);
        void setSecond(SecondCommand secondCommand);
        int execute();

    private:
        std::shared_ptr<ICommand> _firstCommand;
        std::optional<SecondCommand> _secondCommand;
        bool _executeOnFail;
};
