/*
 * appledouble.h - AppleDouble file format support
 *
 * Provides cross-platform access to Mac resource forks and metadata
 * stored in AppleDouble sidecar files.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

/* AppleDouble metadata structure - matches Finder info */
typedef struct {
    char type[4];       /* File type (e.g., "TEXT", "APPL") */
    char creator[4];    /* Creator code (e.g., "KAHL") */
    char flags[2];      /* Finder flags */
    char ctime[4];      /* Creation time (Mac epoch) */
    char mtime[4];      /* Modification time (Mac epoch) */
} AppleDoubleMetadata;

/*
 * Read resource fork data from an AppleDouble sidecar file.
 * Tries the following paths in order:
 *   1. ._basename (AppleDouble format in same directory)
 *   2. filename.rsrc (legacy xbin format)
 *
 * Returns: size of resource fork, or 0 if not found
 * The resource fork data is written directly to the output file descriptor.
 * If crc_out is not NULL, the CRC is calculated and returned.
 */
size_t read_appledouble_rsrc_with_crc(const char *filename, int out_fd,
                                      unsigned short *crc_out,
                                      unsigned short (*updcrc_fn)(unsigned short, unsigned char*, int));

/*
 * Simple version without CRC calculation.
 */
size_t read_appledouble_rsrc(const char *filename, int out_fd);

/*
 * Read metadata from an AppleDouble sidecar file.
 *
 * Returns: 0 on success, -1 if not found or error
 * Populates the metadata structure with type, creator, flags, and times.
 */
int read_appledouble_metadata(const char *filename, AppleDoubleMetadata *metadata);

/*
 * Check if a file has an associated AppleDouble sidecar file.
 *
 * Returns: 1 if sidecar exists, 0 otherwise
 */
int has_appledouble_sidecar(const char *filename);

/*
 * Get the size of the resource fork in an AppleDouble file.
 *
 * Returns: size of resource fork, or 0 if not found
 */
size_t get_appledouble_rsrc_size(const char *filename);
