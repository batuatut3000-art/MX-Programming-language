#include "mxc.h"
#include <stdio.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

int mx_compile(const char *source, const char *output) {
#ifdef _WIN32
    const char *args[] = {"python", "compiler/mxc.py", source, output, NULL};
    int result = _spawnvp(_P_WAIT, args[0], args);
    if (result < 0) perror("mxc: Python compiler");
    return result;
#else
    char *const args[] = {"python3", "compiler/mxc.py", (char *)source,
                          (char *)output, NULL};
    execvp(args[0], args);
    perror("mxc: Python compiler");
    return 127;
#endif
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: mxc source.mx output.c\n");
        return 2;
    }
    return mx_compile(argv[1], argv[2]);
}
