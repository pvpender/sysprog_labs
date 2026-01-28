#include "branch.h"


Branch::Branch(FirstCommand firstCommand, bool executeOnFail)
    : _firstCommand{firstCommand}, _executeOnFail{executeOnFail}
{
}

void Branch::setSecond(std::shared_ptr<ICommand> secondCommand) 
{
    this->_secondCommand = secondCommand;    
}


int Branch::execute()
{
    int exitCode;

    if (std::holds_alternative<std::shared_ptr<Branch>>(this->_firstCommand)) {
        exitCode = std::get<std::shared_ptr<Branch>>(this->_firstCommand)->execute();
        if (std::get<std::shared_ptr<Branch>>(this->_firstCommand)->exitWasCalled()) {
            this->_exitWasCalled = true;
            
            return exitCode;
        }
    } else {
        exitCode = std::get<std::shared_ptr<ICommand>>(this->_firstCommand)->execute();
        if (std::get<std::shared_ptr<ICommand>>(this->_firstCommand)->exitWasCalled()) {
            this->_exitWasCalled = true;

            return exitCode;
        }
    }

    if (!this->_secondCommand.has_value()) 
        return 1;


    if (((exitCode == 0) && !this->_executeOnFail) || ((exitCode == 1) && this->_executeOnFail)) {
        exitCode = this->_secondCommand.value()->execute();
    }

    return exitCode;
}

bool Branch::exitWasCalled() {
    return this->_exitWasCalled;
}