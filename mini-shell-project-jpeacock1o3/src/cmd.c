// SPDX-License-Identifier: BSD-3-Clause
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

//added libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h> // Include for errno


#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	char *path = NULL;
	int rc;

	if (dir == NULL) {
		path = getenv("HOME");
		if (path == NULL) {
			fprintf(stderr, "cd: HOME not set\n");
			return false; }
	} else if (dir->next_word != NULL) {
		fprintf(stderr, "cd:too mandy arguments\n");
		return false; } else  {
		path = get_word(dir);
		DIE(path == NULL, "Error getting word for cd path");
	}
	rc = chdir(path);

	if (dir != NULL && path != NULL)
		free(path);
	if (rc != 0) {
		perror("cd");
		return false;
	}
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	// Use exit() with the defined SHELL_EXIT code
	exit(SHELL_EXIT);
	// This part is technically unreachable but needed to avoid compiler warnings
	return 0;
}

static int handle_redirection(simple_command_t *s)
{
	int fd;
	int rc = 0; // Return code, 0 for success

	// Redirect Input [< filename]
	if (s->in != NULL) {
		char *in_file = get_word(s->in);

		DIE(in_file == NULL, "Error getting word for input file");
		fd = open(in_file, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Error opening input file: %s\n", in_file);
			free(in_file);
			return -1; // Indicate failure
		}
		rc = dup2(fd, STDIN_FILENO);
		if (rc < 0) {
			perror("dup2 stdin");
			close(fd);
			free(in_file);
			return -1; // Indicate failure
		}
		close(fd);
		free(in_file);
	}

	// Redirect Output [> filename], [>> filename], [&> filename], [&>> filename]
	if (s->out != NULL) {
		char *out_file = get_word(s->out);

		DIE(out_file == NULL, "Error getting word for output file");
		int flags = O_WRONLY | O_CREAT;

		if (s->io_flags & IO_OUT_APPEND) { // Check for append mode (>>)
			flags |= O_APPEND;
		} else {
			flags |= O_TRUNC;
		}
		fd = open(out_file, flags, 0644); // 0644 permissions
		if (fd < 0) {
			fprintf(stderr, "Error opening output file: %s\n", out_file);
			free(out_file);
			return -1;
		}
		rc = dup2(fd, STDOUT_FILENO);
		if (rc < 0) {
			perror("dup2 stdout");
			close(fd);
			free(out_file);
			return -1;
		}
		// Handle &> and &>> (redirect stderr as well)
		if (s->err != NULL && strcmp(get_word(s->err), out_file) == 0) { // Simple check if filenames match
			rc = dup2(fd, STDERR_FILENO);
			if (rc < 0) {
				perror("dup2 stderr for &>");
				close(fd);
				free(out_file);
				// Need to free err_file if allocated and different, but here they are the same
				return -1;
			}
		}
		close(fd);
		free(out_file);
	}

	// Redirect Error [2> filename], [2>> filename] (only if not already handled by &>)
	if (s->err != NULL && (s->out == NULL || strcmp(get_word(s->err), get_word(s->out)) != 0)) {
		char *err_file = get_word(s->err);

		DIE(err_file == NULL, "Error getting word for error file");
		int flags = O_WRONLY | O_CREAT;

		if (s->io_flags & IO_ERR_APPEND) { // Check for append mode (2>>)
			flags |= O_APPEND;
		} else {
			flags |= O_TRUNC;
		}
		fd = open(err_file, flags, 0644);
		if (fd < 0) {
			fprintf(stderr, "Error opening error file: %s\n", err_file);
			free(err_file);
			return -1;
		}
		rc = dup2(fd, STDERR_FILENO);
		if (rc < 0) {
			perror("dup2 stderr");
			close(fd);
			free(err_file);
			return -1;
		}
		close(fd);
		free(err_file);
	}

	return 0; // Success
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (s == NULL || s->verb == NULL)
    	return EXIT_FAILURE; // Or some other error code

    char *verb = get_word(s->verb);

    DIE(verb == NULL, "Error getting command verb");

    /* Handle built-in commands with redirection. */
    if (strcmp(verb, "exit") == 0 || strcmp(verb, "quit") == 0) {
        /* Save original file descriptors */
        int saved_stdin = dup(STDIN_FILENO);
        int saved_stdout = dup(STDOUT_FILENO);
        int saved_stderr = dup(STDERR_FILENO);

        /* Apply redirection if specified */
        if (handle_redirection(s) != 0) {
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stdin); close(saved_stdout); close(saved_stderr);
            free(verb);
            return EXIT_FAILURE;
        }
        free(verb);
        /* Execute exit built-in; redirections affect the output before exit */
        shell_exit(); // This will call exit(SHELL_EXIT) and never return.
    }
    if (strcmp(verb, "cd") == 0) {
        /* Save original file descriptors */
        int saved_stdin = dup(STDIN_FILENO);
        int saved_stdout = dup(STDOUT_FILENO);
        int saved_stderr = dup(STDERR_FILENO);

        /* Apply redirection if specified */
        if (handle_redirection(s) != 0) {
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stdin); close(saved_stdout); close(saved_stderr);
            free(verb);
            return EXIT_FAILURE;
        }
        /* Execute the cd built-in command */
        bool success = shell_cd(s->params);

        /* Restore original file descriptors */
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stdin);
        close(saved_stdout);
        close(saved_stderr);

        free(verb);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (s->verb->next_part != NULL &&
        strcmp(s->verb->next_part->string, "=") == 0) {
        char *var_name = s->verb->string; // Variable name is the first part
        char *value = NULL;

        if (s->verb->next_part->next_part != NULL) {
            /* Value might be composed of multiple parts after '=' */
            value = get_word(s->verb->next_part->next_part);
        } else {
            /* Assign empty string if nothing after '=' */
            value = strdup("");
        }
        DIE(value == NULL, "Error getting variable value");

        /* Use setenv to assign the environment variable */
        int rc = setenv(var_name, value, 1); // 1 = overwrite

        free(value);
        free(verb); // Free the concatenated verb string ("VAR=value")

        if (rc != 0) {
            perror("setenv");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    /* External command: fork new process and handle redirections in child */
    pid_t pid;
    int status;

    pid = fork(); // Create child process
    switch (pid) {
    case -1:
        /* Fork failed */
        perror("fork");
        free(verb);
        return EXIT_FAILURE;

    case 0:
        /* Child process */
        if (handle_redirection(s) != 0)
            exit(EXIT_FAILURE); // Exit child if redirection failed

        int argc;
        char **argv = get_argv(s, &argc);
        execvp(argv[0], argv);
        /* execvp only returns on error */
        fprintf(stderr, "Execution failed for '%s'\n", argv[0]);
        /* Free argv memory before exiting child on error */
        for (int i = 0; i < argc; i++)
            free(argv[i]);
        free(argv);
        exit(127); // Exit child with failure status

    default:
        /* Parent process */
        free(verb);
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
    	return EXIT_FAILURE; // Indicate abnormal termination
    }

    /* Should not be reached normally */
    return EXIT_FAILURE;
}


/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	pid_t pid1, pid2;
	int status1, status2;
	int ret_status = EXIT_SUCCESS; // Default success

	pid1 = fork();
	if (pid1 < 0) {
		perror("fork cmd1");
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		// Child 1 executes cmd1
		exit(parse_command(cmd1, level + 1, father));
	}

	// Parent continues to fork the second child
	pid2 = fork();
	if (pid2 < 0) {
		perror("fork cmd2");
		waitpid(pid1, &status1, 0); // Clean up first child
		return EXIT_FAILURE;
	} else if (pid2 == 0) {
		// Child 2 executes cmd2
		exit(parse_command(cmd2, level + 1, father));
	}

	// Parent waits for both children
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	/* TODO: Replace with actual exit status. */
	// Decide on the return status.
	if (WIFEXITED(status2))
		ret_status = WEXITSTATUS(status2);
	else
		ret_status = EXIT_FAILURE; // Indicate abnormal termination

	return ret_status;; /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */

	int fds[2];
	pid_t pid1, pid2;
	int status1, status2;
	int ret_status;

	// Create pipe
	if (pipe(fds) < 0) {
		perror("pipe");
		return EXIT_FAILURE;
	}

	// Fork first child (cmd1 writer)
	pid1 = fork();
	if (pid1 < 0) {
		perror("fork cmd1 (pipe)");
		close(fds[READ]);
		close(fds[WRITE]);
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		// Child 1: Executes cmd1, writes to pipe
		close(fds[READ]); // Close unused read end
		// Redirect stdout to pipe write end
		if (dup2(fds[WRITE], STDOUT_FILENO) < 0) {
			perror("dup2 stdout to pipe");
			close(fds[WRITE]);
			exit(EXIT_FAILURE);
		}
		close(fds[WRITE]); // Close original write end
		// Execute cmd1
		exit(parse_command(cmd1, level + 1, father));
	}

	// Fork second child (cmd2 reader)
	pid2 = fork();
	if (pid2 < 0) {
		perror("fork cmd2 (pipe)");
		close(fds[READ]);
		close(fds[WRITE]);
		waitpid(pid1, &status1, 0); // Clean up first child
		return EXIT_FAILURE;
	} else if (pid2 == 0) {
		// Child 2: Executes cmd2, reads from pipe
		close(fds[WRITE]); // Close unused write end
		// Redirect stdin to pipe read end
		if (dup2(fds[READ], STDIN_FILENO) < 0) {
			perror("dup2 stdin from pipe");
			close(fds[READ]);
			exit(EXIT_FAILURE);
		}
		close(fds[READ]); // Close original read end
		// Execute cmd2
		exit(parse_command(cmd2, level + 1, father));
	}

	// Parent process
	// Close both pipe ends in parent
	close(fds[READ]);
	close(fds[WRITE]);
	// Wait for both children
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);
	// Return the exit status of the second command (the end of the pipe)
	if (WIFEXITED(status2))
		ret_status = WEXITSTATUS(status2);
	else
		ret_status = EXIT_FAILURE; // Indicate abnormal termination
	return ret_status;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	if (c == NULL)
		return EXIT_FAILURE; // Indicate error

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */

		return parse_simple(c->scmd, level, father); // Pass father (which is c)
	}
	int status1; // Status of the first command
	int status2; // Status of the second command (used in some cases)

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		/* Execute the commands one after the other (;). */
		// Execute cmd1
		status1 = parse_command(c->cmd1, level + 1, c);
		// Execute cmd2 regardless of cmd1's status
		status2 = parse_command(c->cmd2, level + 1, c);
		// Return status of the second command
		return status2;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		/* Execute the commands simultaneously (&). */
		return run_in_parallel(c->cmd1, c->cmd2, level, c);

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */
		/* Execute the second command only if the first one returns non zero (||). */
		status1 = parse_command(c->cmd1, level + 1, c);
		// If cmd1 failed (non-zero status), execute cmd2
		if (status1 != 0)
			return parse_command(c->cmd2, level + 1, c);
			// If cmd1 succeeded, return its status
		return status1;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */
		/* Execute the second command only if the first one returns zero (&&). */
		status1 = parse_command(c->cmd1, level + 1, c);
		// If cmd1 succeeded (zero status), execute cmd2
		if (status1 == 0)
			return parse_command(c->cmd2, level + 1, c);
			// If cmd1 failed, return its status
		return status1;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		/* Redirect the output of the first command to the input of the second (|). */
		return run_on_pipe(c->cmd1, c->cmd2, level, c);

	default:
		fprintf(stderr, "Unknown operator type\n");
		return SHELL_EXIT;
	}

	// Should not be reached if all cases are handled
	return EXIT_FAILURE; /* TODO: Replace with actual exit code of command. */
}
