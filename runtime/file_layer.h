#ifndef NFSPS_FILE_LAYER_H
#define NFSPS_FILE_LAYER_H

#include <stddef.h>
#include <stdio.h>

typedef struct FileLayer FileLayer;

FileLayer *file_layer_create(const char *root, const char *trace_path);
void file_layer_destroy(FileLayer *layer);

/*
 * Resolve a Windows-style game path to an existing regular file below root.
 * The output is always a host path.  Returns 0 on success, -1 on rejection
 * or lookup failure.
 */
int file_layer_resolve(FileLayer *layer, const char *windows_path,
                       char *out, size_t out_size);

/*
 * Open a game path using the supplied fopen mode and append one record to the
 * trace.  Creation modes may create the final component, but never a parent
 * directory.  Returns NULL on failure.
 */
FILE *file_layer_open(FileLayer *layer, const char *windows_path,
                      const char *mode);

#endif
