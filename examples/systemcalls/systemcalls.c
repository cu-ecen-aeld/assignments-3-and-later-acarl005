#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    return system(cmd) == 0;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    bool success = false;
    int pid = fork();
    switch (pid) {
        case -1:
            perror("fork");
            break;
        case 0:
            execv(command[0], &command[0]);
            perror("execv");
	    _exit(errno);
        default:
	    int status;
            int result = waitpid(pid, &status, 0);
            if (result == -1) {
                perror("waitpid");
            } else {
		if (WIFEXITED(status)) {
                    int code = WEXITSTATUS(status);
                    success = code == 0;
		}
            }
    }
    va_end(args);

    return success;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    bool success = false;
    int fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
    } else {
        int pid = fork();
        switch (pid) {
            case -1:
                perror("fork");
                break;
            case 0:
                if (dup2(fd, 1) < 0) {
                    perror("dup2");
                    return false;
                }
                close(fd);
                execv(command[0], &command[0]);
                perror("execv");
	        _exit(errno);
            default:
		int status;
                int result = waitpid(pid, &status, 0);
                if (result == -1) {
                    perror("waitpid");
                } else {
		    if (WIFEXITED(status)) {
                        int code = WEXITSTATUS(status);
                        success = code == 0;
		    }
                }
        }
    }

    close(fd);
    va_end(args);

    return success;
}

