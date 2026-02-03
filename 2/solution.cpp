#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <queue>
#include <memory>
#include <fstream>
#include <sys/wait.h>
#include <set>

#include "pipe.h"
#include "command.h"
#include "icommand.h"
#include "branch.h"

typedef std::variant<std::shared_ptr<ICommand>, std::shared_ptr<Branch>> StartCommand;

bool 
can_be_optimised(const struct command_line *line) {
	auto startIt = line->exprs.begin();
	auto endIt = line->exprs.end();
	while (startIt != endIt)
	{
		if ((startIt->type != expr_type::EXPR_TYPE_COMMAND) && (startIt->type != expr_type::EXPR_TYPE_PIPE)) {
			return false;
		}	
		++startIt;
	}
	
	return true;
}

bool is_simple_command(std::string command) {
	std::set<std::string> simpleCommands = {"cat", "grep", "head", "tail", "true", "false", "yes"};

	return (simpleCommands.find(command) != simpleCommands.end());
}

bool
is_equal_commands(const std::optional<command> command1, const std::optional<command> command2) {
	if (!command1.has_value() || !command2.has_value()) {
        return false;
    }

	auto cmd1 = command1.value();
	auto cmd2 = command2.value();

	if (cmd1.exe != cmd2.exe)
		return false;

	if (cmd1.args.size() != cmd2.args.size()) 
		return false;
		
	if (!is_simple_command(cmd2.exe))
		return false;

	for (size_t i = 0; i < cmd1.args.size(); ++i) {
		if (cmd1.args[i] != cmd2.args[i]) {
			return false;
		}
	}

	return true;
}

StartCommand
build_optimized(const struct command_line *line) {
	std::vector<std::shared_ptr<ICommand>> commands;
	std::queue<std::shared_ptr<Pipe>> pipeQueue;
	auto startIt = line->exprs.begin();
	auto endIt = line->exprs.end();

	while (startIt != endIt) {
		if (startIt->type == expr_type::EXPR_TYPE_COMMAND) {
			if (std::next(startIt) == endIt) {
				commands.push_back(std::make_shared<Command>(
					startIt->cmd.value(),
					line->out_type,
					line->out_file,
					line->is_background
				));
			} else {
				if (startIt == line->exprs.begin() || !is_equal_commands(startIt->cmd, std::prev(startIt, 2)->cmd)) {
					commands.push_back(std::make_shared<Command>(
						startIt->cmd.value()
					));
				}
			}
		} 

		++startIt;
	}

	startIt = line->exprs.begin();
	endIt = line->exprs.end();

	if (commands.size() > 1) {
		for(size_t i = 1; i < commands.size(); ++i) {
			std::shared_ptr<ICommand> firstCommand;
			if (!pipeQueue.empty()) {
				firstCommand = pipeQueue.front();
				pipeQueue.pop();
			} else {
				firstCommand = commands[i - 1];
			}

			auto newPipe = std::make_shared<Pipe>(firstCommand, commands[i]);
			pipeQueue.push(newPipe);	
		}
	}
	
	if (!pipeQueue.empty()) {
		return pipeQueue.front();
	}

	return commands[commands.size() - 1];
}

StartCommand 
build_graph(const struct command_line *line) {
	std::queue<std::shared_ptr<Command>> commandQueue;
	std::queue<std::shared_ptr<Pipe>> pipeQueue;
	std::queue<std::shared_ptr<Branch>> branchQueue;
	std::optional<std::shared_ptr<Branch>> startBranch;

	auto startIt = line->exprs.begin();
	auto endIt = line->exprs.end();

	while (startIt != endIt) {
		if (startIt->type == expr_type::EXPR_TYPE_COMMAND) {
			if (std::next(startIt) == endIt) {
				commandQueue.push(std::make_shared<Command>(
					startIt->cmd.value(),
					line->out_type,
					line->out_file,
					line->is_background
				));
			} else {
				commandQueue.push(std::make_shared<Command>(
					startIt->cmd.value()
				));
			}
		} 

		++startIt;
	}

	startIt = line->exprs.begin();
	endIt = line->exprs.end();

	while(startIt != endIt) {
		if (startIt->type == expr_type::EXPR_TYPE_PIPE) {
			std::shared_ptr<ICommand> firstCommand;
			if (!pipeQueue.empty()) {
				firstCommand = pipeQueue.front();
				pipeQueue.pop();
			} else {
				firstCommand = commandQueue.front();
				commandQueue.pop();
			}

			auto newPipe = std::make_shared<Pipe>(firstCommand, commandQueue.front());
			commandQueue.pop();
			pipeQueue.push(newPipe);
		}

		if ((startIt->type == expr_type::EXPR_TYPE_AND) || (startIt->type == expr_type::EXPR_TYPE_OR)) {
			std::shared_ptr<Branch> newBranch;
			if (!branchQueue.empty()) {
				if (!pipeQueue.empty()) {
					branchQueue.front()->setSecond(pipeQueue.front());
					pipeQueue.pop();
				} else {
					branchQueue.front()->setSecond(commandQueue.front());
					commandQueue.pop();
				}

				newBranch = std::make_shared<Branch>(
					branchQueue.front(),
					startIt->type == expr_type::EXPR_TYPE_AND ? false : true
				);

				branchQueue.pop();

			} else if (!pipeQueue.empty()) {
				newBranch = std::make_shared<Branch>(
					pipeQueue.front(),
					startIt->type == expr_type::EXPR_TYPE_AND ? false : true
				);
				pipeQueue.pop();
			} else {
				newBranch = std::make_shared<Branch>(
					commandQueue.front(),
					startIt->type == expr_type::EXPR_TYPE_AND ? false : true
				);
				commandQueue.pop();
			}

			branchQueue.push(newBranch);
		}

		++startIt;
	}

	if (!branchQueue.empty()) {
		if (!pipeQueue.empty()) {
			branchQueue.front()->setSecond(pipeQueue.front());
			pipeQueue.pop();
		} else {
			branchQueue.front()->setSecond(commandQueue.front());
			commandQueue.pop();
		}

		if (!startBranch.has_value()) {
			startBranch = branchQueue.front();
		}

		branchQueue.pop();
	}

	if (startBranch.has_value()) {
		return startBranch.value();
	}

	if (!pipeQueue.empty()) {
		return pipeQueue.front();
	}

	return commandQueue.front();
}

static int
execute_command_line(const struct command_line *line, bool *exitWasCalled)
{
	StartCommand command;
	if (can_be_optimised(line)) {
		command = build_optimized(line);
	} else {
		command = build_graph(line);
	}
 
	int exitCode = 0;
	
	if (std::holds_alternative<std::shared_ptr<Branch>>(command)) {
		exitCode = std::get<std::shared_ptr<Branch>>(command)->execute();
		*exitWasCalled = std::get<std::shared_ptr<Branch>>(command)->exitWasCalled();
	} else {
		exitCode = std::get<std::shared_ptr<ICommand>>(command)->execute();
		*exitWasCalled = std::get<std::shared_ptr<ICommand>>(command)->exitWasCalled();
	}

	while (waitpid(-1, NULL, WNOHANG) > 0) {}

	return exitCode;
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
	int exitCode = 0;
	bool exitWasCalled = false;

	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;

		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			exitCode = execute_command_line(line, &exitWasCalled);
			delete line;

			if (exitWasCalled)
				break;
		}

		if (exitWasCalled)
			break;
	}
	parser_delete(p);

	return exitCode;
}
