/*
 * appledouble.c - AppleDouble file format support
 */

#include "appledouble.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#define AS_MAGIC_AD 0x00051607  /* AppleDouble magic number */
#define RESOURCE_FORK_ID 2
#define FINDER_INFO_ID 9

/* AppleDouble file entry descriptor */
typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
} ADEntry;

/* Helper functions for reading big-endian values */
static uint16_t read_uint16_be(const unsigned char *buf) {
    return (buf[0] << 8) | buf[1];
}

static uint32_t read_uint32_be(const unsigned char *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

/*
 * Find an AppleDouble sidecar file for the given filename.
 * Returns file descriptor, or -1 if not found.
 * Fills in the path buffer with the actual path found.
 */
static int open_appledouble_file(const char *filename, char *path_buf, size_t path_size) {
    int fd;
    char *fname_copy = strdup(filename);
    char *dir = dirname(fname_copy);
    char *fname_copy2 = strdup(filename);
    char *base = basename(fname_copy2);

    /* Try ._basename format (AppleDouble) */
    if (strcmp(dir, ".") == 0) {
        snprintf(path_buf, path_size, "._%s", base);
    } else {
        snprintf(path_buf, path_size, "%s/._%s", dir, base);
    }

    fd = open(path_buf, O_RDONLY);
    if (fd >= 0) {
        free(fname_copy);
        free(fname_copy2);
        return fd;
    }

    /* Try filename.rsrc format (legacy) */
    snprintf(path_buf, path_size, "%s.rsrc", filename);
    fd = open(path_buf, O_RDONLY);

    free(fname_copy);
    free(fname_copy2);
    return fd;
}

int has_appledouble_sidecar(const char *filename) {
    char path[PATH_MAX];
    int fd = open_appledouble_file(filename, path, sizeof(path));
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

/*
 * Read and parse AppleDouble header, finding specific entry.
 * Returns 0 on success, -1 on error.
 */
static int find_appledouble_entry(int fd, uint32_t entry_id, ADEntry *entry) {
    unsigned char header[26];
    unsigned char entry_buf[12];
    uint32_t magic;
    uint16_t num_entries;
    int i;

    /* Read header */
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    if (read(fd, header, sizeof(header)) != sizeof(header)) return -1;

    /* Check magic */
    magic = read_uint32_be(header);
    if (magic != AS_MAGIC_AD) return -1;

    /* Get number of entries */
    num_entries = read_uint16_be(header + 24);

    /* Search for our entry */
    for (i = 0; i < num_entries; i++) {
        if (read(fd, entry_buf, 12) != 12) return -1;

        uint32_t id = read_uint32_be(entry_buf);
        if (id == entry_id) {
            entry->id = id;
            entry->offset = read_uint32_be(entry_buf + 4);
            entry->size = read_uint32_be(entry_buf + 8);
            return 0;
        }
    }

    return -1; /* Entry not found */
}

size_t read_appledouble_rsrc_with_crc(const char *filename, int out_fd,
                                      unsigned short *crc_out,
                                      unsigned short (*updcrc_fn)(unsigned short, unsigned char*, int)) {
    char path[PATH_MAX];
    int fd;
    ADEntry entry;
    unsigned char buf[4096];
    ssize_t n;
    size_t total = 0;
    uint32_t remaining;
    unsigned short crc = 0;

    fd = open_appledouble_file(filename, path, sizeof(path));
    if (fd < 0) return 0;

    /* Find resource fork entry */
    if (find_appledouble_entry(fd, RESOURCE_FORK_ID, &entry) < 0) {
        close(fd);
        return 0;
    }

    /* Seek to resource fork data */
    if (lseek(fd, entry.offset, SEEK_SET) < 0) {
        close(fd);
        return 0;
    }

    /* Copy resource fork to output and calculate CRC */
    remaining = entry.size;
    while (remaining > 0) {
        size_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
        n = read(fd, buf, to_read);
        if (n <= 0) break;

        /* Calculate CRC if requested */
        if (crc_out && updcrc_fn) {
            crc = updcrc_fn(crc, buf, n);
        }

        if (write(out_fd, buf, n) != n) {
            close(fd);
            return 0;
        }

        total += n;
        remaining -= n;
    }

    if (crc_out) {
        *crc_out = crc;
    }

    close(fd);
    return total;
}

size_t read_appledouble_rsrc(const char *filename, int out_fd) {
    return read_appledouble_rsrc_with_crc(filename, out_fd, NULL, NULL);
}

size_t get_appledouble_rsrc_size(const char *filename) {
    char path[PATH_MAX];
    int fd;
    ADEntry entry;

    fd = open_appledouble_file(filename, path, sizeof(path));
    if (fd < 0) return 0;

    /* Find resource fork entry */
    if (find_appledouble_entry(fd, RESOURCE_FORK_ID, &entry) < 0) {
        close(fd);
        return 0;
    }

    close(fd);
    return entry.size;
}

int read_appledouble_metadata(const char *filename, AppleDoubleMetadata *metadata) {
    char path[PATH_MAX];
    int fd;
    ADEntry entry;
    unsigned char finder_info[32];

    if (!metadata) return -1;

    fd = open_appledouble_file(filename, path, sizeof(path));
    if (fd < 0) return -1;

    /* Find Finder Info entry */
    if (find_appledouble_entry(fd, FINDER_INFO_ID, &entry) < 0) {
        close(fd);
        return -1;
    }

    /* Read Finder Info (we need at least 32 bytes) */
    if (entry.size < 32) {
        close(fd);
        return -1;
    }

    if (lseek(fd, entry.offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    if (read(fd, finder_info, 32) != 32) {
        close(fd);
        return -1;
    }

    /* Extract metadata from Finder Info structure:
     * Bytes 0-3: File type
     * Bytes 4-7: Creator
     * Bytes 8-9: Finder flags
     * Note: Times are not in Finder Info, they're in separate entries
     */
    memcpy(metadata->type, finder_info, 4);
    memcpy(metadata->creator, finder_info + 4, 4);
    memcpy(metadata->flags, finder_info + 8, 2);

    /* Zero out time fields - caller should use stat() for times */
    memset(metadata->ctime, 0, 4);
    memset(metadata->mtime, 0, 4);

    close(fd);
    return 0;
}
