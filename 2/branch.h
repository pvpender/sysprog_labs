#pragma once

#include <functional>
#include <optional>
#include <variant>
#include <memory>

#include "command.h"
#include "icommand.h"

class Branch {
    public:
        using SecondVariant = std::variant<std::shared_ptr<ICommand>, std::shared_ptr<Branch>>;
        
        Branch() = delete;
        Branch(std::shared_ptr<ICommand> firstCommand, bool executeOnFail);
        void setSecond(SecondVariant secondCommand);
        int execute();
          /*  if (std::holds_alternative<Branch>(_secondCommand.value())) {
                std::get<Branch>(_secondCommand.value()).execute();
            }*/

    private:
        std::shared_ptr<ICommand> _firstCommand;
        std::optional<SecondVariant> _secondCommand;
        bool _executeOnFail;
};