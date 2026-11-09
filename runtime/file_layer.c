#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "file_layer.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct FileLayer {
    char root[PATH_MAX];
    FILE *trace;
    pthread_mutex_t lock;
    int lock_ready;
};

static int path_within(const char *path, const char *root)
{
    size_t n = strlen(root);
    return (n == 1 && root[0] == '/') ||
           (strncmp(path, root, n) == 0 &&
            (path[n] == '\0' || path[n] == '/'));
}

static int trace_record(FileLayer *layer, const char *request,
                        const char *resolved, int ok)
{
    if (!layer->trace)
        return 0;

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return -1;
    pthread_mutex_lock(&layer->lock);
    int result = fprintf(layer->trace, "%lld.%09ld\t%s\t%s\t%s\n",
                         (long long)ts.tv_sec, ts.tv_nsec,
                         ok ? "open" : "fail", request,
                         resolved ? resolved : "-") < 0;
    if (fflush(layer->trace) != 0)
        result = -1;
    pthread_mutex_unlock(&layer->lock);
    return result;
}

static int split_path(const char *input, char *relative, size_t size)
{
    const char *p = input;
    size_t used = 0;
    if (!input || !*input)
        return -1;

    /* A drive is only syntax here; the configured root is the drive root. */
    if (p[0] && p[1] == ':')
        p += 2;
    while (*p == '\\' || *p == '/')
        ++p;

    while (*p) {
        const char *begin = p;
        while (*p && *p != '\\' && *p != '/')
            ++p;
        size_t length = (size_t)(p - begin);
        if (length == 0 ||
            (length == 2 && begin[0] == '.' && begin[1] == '.') ||
            memchr(begin, ':', length) != NULL)
            return -1;
        if (!(length == 1 && begin[0] == '.')) {
            if (used != 0) {
                if (used + 1 >= size)
                    return -1;
                relative[used++] = '/';
            }
            if (used + length >= size)
                return -1;
            memcpy(relative + used, begin, length);
            used += length;
        }
        while (*p == '\\' || *p == '/')
            ++p;
    }
    if (used == 0)
        return -1;
    relative[used] = '\0';
    return 0;
}

static int find_component(const char *directory, const char *name,
                          char *out, size_t out_size)
{
    struct stat st;
    int exact = snprintf(out, out_size, "%s/%s", directory, name);
    if (exact < 0 || (size_t)exact >= out_size)
        return -1;
    if (lstat(out, &st) == 0)
        return S_ISLNK(st.st_mode) ? -1 : 0;

    DIR *dir = opendir(directory);
    if (!dir)
        return -1;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcasecmp(entry->d_name, name) == 0) {
            int n = snprintf(out, out_size, "%s/%s", directory,
                             entry->d_name);
            if (n < 0 || (size_t)n >= out_size || lstat(out, &st) != 0 ||
                S_ISLNK(st.st_mode)) {
                found = -1;
            } else {
                found = 1;
            }
            break;
        }
    }
    closedir(dir);
    return found == 1 ? 0 : -1;
}

static int resolve_relative(FileLayer *layer, const char *relative,
                            char *out, size_t out_size, int allow_missing)
{
    char current[PATH_MAX];
    char parts[PATH_MAX];
    snprintf(current, sizeof current, "%s", layer->root);
    snprintf(parts, sizeof parts, "%s", relative);

    char *save = NULL;
    char *part = strtok_r(parts, "/", &save);
    while (part) {
        char candidate[PATH_MAX];
        if (find_component(current, part, candidate, sizeof candidate) != 0) {
            char *next = strtok_r(NULL, "/", &save);
            if (!allow_missing || next != NULL)
                return -1;
            if (snprintf(candidate, sizeof candidate, "%s/%s", current,
                         part) < 0)
                return -1;
        }
        if (!path_within(candidate, layer->root))
            return -1;
        snprintf(current, sizeof current, "%s", candidate);
        part = strtok_r(NULL, "/", &save);
    }

    struct stat st;
    if ((!allow_missing && (stat(current, &st) != 0 || !S_ISREG(st.st_mode))) ||
        (allow_missing && lstat(current, &st) == 0 && S_ISLNK(st.st_mode)))
        return -1;
    if (snprintf(out, out_size, "%s", current) < 0 ||
        strlen(current) >= out_size)
        return -1;
    return 0;
}

static int file_layer_init(FileLayer *layer, const char *root,
                           const char *trace_path)
{
    if (!layer || !root)
        return -1;
    memset(layer, 0, sizeof *layer);
    if (!realpath(root, layer->root) || strlen(layer->root) >= sizeof layer->root)
        return -1;
    struct stat st;
    if (stat(layer->root, &st) != 0 || !S_ISDIR(st.st_mode))
        return -1;
    if (pthread_mutex_init(&layer->lock, NULL) != 0)
        return -1;
    layer->lock_ready = 1;
    if (trace_path) {
        layer->trace = fopen(trace_path, "a");
        if (!layer->trace) {
            file_layer_destroy(layer);
            return -1;
        }
    }
    return 0;
}

FileLayer *file_layer_create(const char *root, const char *trace_path)
{
    FileLayer *layer = calloc(1, sizeof *layer);
    if (!layer || file_layer_init(layer, root, trace_path) != 0) {
        free(layer);
        return NULL;
    }
    return layer;
}

void file_layer_destroy(FileLayer *layer)
{
    if (!layer)
        return;
    if (layer->trace)
        fclose(layer->trace);
    if (layer->lock_ready)
        pthread_mutex_destroy(&layer->lock);
    free(layer);
}

int file_layer_resolve(FileLayer *layer, const char *windows_path,
                       char *out, size_t out_size)
{
    if (!layer || !windows_path || !out || out_size == 0)
        return -1;
    char relative[PATH_MAX];
    int result = split_path(windows_path, relative, sizeof relative) == 0 &&
                 resolve_relative(layer, relative, out, out_size, 0) == 0;
    return result ? 0 : -1;
}

FILE *file_layer_open(FileLayer *layer, const char *windows_path,
                      const char *mode)
{
    if (!layer || !windows_path || !mode)
        return NULL;
    char relative[PATH_MAX];
    char host[PATH_MAX];
    int create = strchr(mode, 'w') || strchr(mode, 'a') ||
                 strchr(mode, 'x');
    int ok = split_path(windows_path, relative, sizeof relative) == 0 &&
             resolve_relative(layer, relative, host, sizeof host, create) == 0;
    FILE *file = ok ? fopen(host, mode) : NULL;
    trace_record(layer, windows_path, file ? host : NULL, file != NULL);
    return file;
}
