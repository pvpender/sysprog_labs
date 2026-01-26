#include "branch.h"

Branch::Branch(std::shared_ptr<ICommand> firstCommand, bool executeOnFail)
    : _firstCommand{firstCommand}, _executeOnFail{executeOnFail}
{
}

void Branch::setSecond(SecondVariant secondCommand) 
{
    this->_secondCommand = secondCommand;    
}

int Branch::execute()
{
    return 0;
}
