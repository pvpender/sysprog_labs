#pragma once

#include <functional>
#include <optional>
#include <variant>
#include <memory>

#include "command.h"
#include "icommand.h"

class Branch {
    public:
        using FirstCommand = std::variant<std::shared_ptr<ICommand>, std::shared_ptr<Branch>>;

        Branch() = delete;
        Branch(FirstCommand firstCommand, bool executeOnFail);
        void setSecond(std::shared_ptr<ICommand> secondCommand);
        int execute();
        bool exitWasCalled();

    private:
        FirstCommand _firstCommand;
        std::optional<std::shared_ptr<ICommand>> _secondCommand;
        bool _executeOnFail;
        bool _exitWasCalled = false;
};
