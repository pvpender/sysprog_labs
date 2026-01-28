#include "branch.h"


Branch::Branch(std::shared_ptr<ICommand> firstCommand, bool executeOnFail)
    : _firstCommand{firstCommand}, _executeOnFail{executeOnFail}
{
}

void Branch::setSecond(SecondCommand secondCommand) 
{
    this->_secondCommand = secondCommand;    
}

int Branch::execute()
{
    int exitCode;

    exitCode = this->_firstCommand->execute();

    if (this->_firstCommand->exitWasCalled()) {
        return exitCode;
    }

    if (((exitCode == 0) && !this->_executeOnFail) || ((exitCode == 1) && this->_executeOnFail)) {
        if (std::holds_alternative<std::shared_ptr<Branch>>(this->_secondCommand.value())) {
            exitCode = std::get<std::shared_ptr<Branch>>(this->_secondCommand.value())->execute();
        } else {
            exitCode = std::get<std::shared_ptr<ICommand>>(this->_secondCommand.value())->execute();
        }
    }

    return exitCode;
}
