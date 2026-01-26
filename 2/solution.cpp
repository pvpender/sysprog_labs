#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <queue>
#include <memory>

#include "pipe.h"
#include "command.h"
#include "icommand.h"
#include "branch.h"

static void
execute_command_line(const struct command_line *line)
{
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

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
			if (!pipeQueue.empty()) {
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

			if (!branchQueue.empty()) {
				branchQueue.front()->setSecond(newBranch);

				if (!startBranch.has_value()) {
					startBranch = branchQueue.front();
				}

				branchQueue.pop();
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




	assert(line != NULL);
	printf("================================\n");
	printf("Command line:\n");
	printf("Is background: %d\n", (int)line->is_background);
	printf("Output: ");
	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		printf("stdout\n");
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		printf("new file - \"%s\"\n", line->out_file.c_str());
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		printf("append file - \"%s\"\n", line->out_file.c_str());
	} else {
		assert(false);
	}
	printf("Expressions:\n");
	for (const expr &e : line->exprs) {
		if (e.type == EXPR_TYPE_COMMAND) {
			printf("\tCommand: %s", e.cmd->exe.c_str());
			for (const std::string& arg : e.cmd->args)
				printf(" %s", arg.c_str());
			printf("\n");
		} else if (e.type == EXPR_TYPE_PIPE) {
			printf("\tPIPE\n");
		} else if (e.type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} else if (e.type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} else {
			assert(false);
		}
	}
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
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
			execute_command_line(line);
			delete line;
		}
	}
	parser_delete(p);
	return 0;
}
