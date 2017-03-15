#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <string.h>
#include <sys/wait.h>
#include <memory>
#include <functional>
#include <string>
#include <initializer_list>

#define EINTRWRAP(var, op)                      \
    do {                                        \
        var = op;                               \
    } while (var == -1 && errno == EINTR);

struct {
    pid_t pid, pgid;
    struct termios tmodes;
} state;

extern "C" char **environ;

void run(const std::string& cmd, std::initializer_list<const std::string> list)
{
    printf("running %s\n", cmd.c_str());

    pid_t p = fork();
    if (p == 0) {
        // child
        pid_t self = getpid();
        setpgid(self, self);
        tcsetpgrp(STDIN_FILENO, self);

        char** args = static_cast<char**>(malloc((list.size() + 2) * sizeof(char*)));
        args[0] = strdup(cmd.c_str());
        args[list.size() + 1] = 0;
        auto ch = list.begin();
        auto end = list.end();
        int off = 0;
        while (ch != end) {
            args[++off] = strdup(ch->c_str());
            ++ch;
        }

        execve(cmd.c_str(), args, environ);
        printf("boo %d\n", errno);
        exit(0);
    } else if (p > 0) {
        // parent
        // wait for pid
        int status;
        if (waitpid(p, &status, 0) > 0) {
            // done
            // restore term
            tcsetattr(STDIN_FILENO, TCSADRAIN, &state.tmodes);
            tcsetpgrp(STDIN_FILENO, state.pgid);
            printf("ok\n");
        } else {
            exit(3);
        }
    } else {
        // ugh
    }
}

int readFromStdin()
{
    // setup
    state.pid = getpid();

    while (tcgetpgrp(STDIN_FILENO) != (state.pgid = getpgrp()))
        kill(state.pid, SIGTTIN);

    setpgid(state.pid, state.pid);
    state.pgid = getpgrp();
    if (state.pgid != state.pid) {
        // more badness
        fprintf(stderr, "unable to set group leader\n");
        return 2;
    }

    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    if (tcsetpgrp(STDIN_FILENO, state.pgid) == -1) {
        fprintf(stderr, "unable to set process group for terminal\n");
        return 2;
    }
    if (tcgetattr(STDIN_FILENO, &state.tmodes) == -1) {
        fprintf(stderr, "unable to get terminal attributes for terminal\n");
        return 2;
    }

    char buf[100];
    int e;
    for (;;) {
        EINTRWRAP(e, ::read(STDIN_FILENO, buf, sizeof(buf)));
        if (e == -1)
            return 1;
        for (int i = 0; i < e; ++i) {
            switch (buf[i]) {
            case 'q':
                return 0;
            case 'g':
                // run git
                run("/usr/bin/git", { "log" });
                break;
            }
        }
    }
}

int main(int, char**)
{
    return readFromStdin();
}
