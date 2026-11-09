#define _POSIX_C_SOURCE 200809L

#include "../runtime/file_layer.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char dir[] = "tests/.file-layer-XXXXXX";
    assert(mkdtemp(dir));
    char data[PATH_MAX], trace[PATH_MAX];
    snprintf(data, sizeof data, "%s/GLOBAL", dir);
    snprintf(trace, sizeof trace, "%s/trace.log", dir);
    assert(mkdir(data, 0700) == 0);
    char file[PATH_MAX];
    strcpy(file, data);
    strcat(file, "/TeSt.BIN");
    FILE *seed = fopen(file, "w");
    assert(seed);
    fputs("ok", seed);
    fclose(seed);

    FileLayer *layer = file_layer_create(dir, trace);
    assert(layer);
    char resolved[PATH_MAX];
    assert(file_layer_resolve(layer, "global\\test.bin", resolved,
                              sizeof resolved) == 0);
    FILE *opened = file_layer_open(layer, "GLOBAL/TEST.BIN", "r");
    assert(opened);
    fclose(opened);
    assert(file_layer_resolve(layer, "..\\outside", resolved,
                              sizeof resolved) != 0);
    assert(file_layer_open(layer, "GLOBAL\\..\\new.bin", "w") == NULL);
    file_layer_destroy(layer);

    FILE *log = fopen(trace, "r");
    assert(log);
    char line[PATH_MAX + 64];
    int records = 0;
    while (fgets(line, sizeof line, log))
        ++records;
    fclose(log);
    assert(records == 2);
    unlink(file);
    unlink(trace);
    rmdir(data);
    rmdir(dir);
    puts("file layer smoke: ok");
    return 0;
}
