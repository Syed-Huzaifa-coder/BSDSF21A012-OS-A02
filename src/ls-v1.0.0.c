/* src/ls-v1.5.0.c -- ls v1.5.0
   Adds colorized output based on file type.
   All previous features are included: -l, -x, alphabetical sort, multiple directories.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <getopt.h>
#include <math.h>

#define SIX_MONTHS_SECONDS (15552000) /* ~6*30*24*3600 */
#define COLUMN_SPACING 2
#define FALLBACK_TERM_WIDTH 80

/* ANSI Color Codes */
#define COLOR_RESET "\033[0m"
#define COLOR_DIR "\033[0;34m"
#define COLOR_EXEC "\033[0;32m"
#define COLOR_SYMLINK "\033[0;35m"
#define COLOR_ARCHIVE "\033[0;31m"
#define COLOR_SPECIAL "\033[7m"

typedef struct {
    char *name;
    char *path;
    struct stat st;
    char *link_target;
} FileEntry;

/* ---------- Utility functions ---------- */
int cmpstring(const void *a, const void *b) {
    const char *str1 = *(const char **)a;
    const char *str2 = *(const char **)b;
    return strcmp(str1, str2);
}

int digits_unsigned(unsigned long long v) {
    int d = 1;
    while (v >= 10) { v /= 10; d++; }
    return d;
}

int cmp_fileentry(const void *a, const void *b) {
    const FileEntry *A = a;
    const FileEntry *B = b;
    return strcmp(A->name, B->name);
}

void mode_to_perm(mode_t mode, char out[11]) {
    out[0] = S_ISREG(mode) ? '-' :
             S_ISDIR(mode) ? 'd' :
             S_ISLNK(mode) ? 'l' :
             S_ISCHR(mode) ? 'c' :
             S_ISBLK(mode) ? 'b' :
             S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '?';

    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';

    if (mode & S_ISUID) out[3] = (out[3] == 'x') ? 's' : 'S';
    if (mode & S_ISGID) out[6] = (out[6] == 'x') ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = (out[9] == 'x') ? 't' : 'T';

    out[10] = '\0';
}

void format_mtime(time_t mtime, char *buf, size_t bufsz) {
    struct tm tm;
    localtime_r(&mtime, &tm);
    time_t now = time(NULL);
    if (llabs(now - mtime) > SIX_MONTHS_SECONDS || mtime > now + 60) {
        strftime(buf, bufsz, "%b %e  %Y", &tm);
    } else {
        strftime(buf, bufsz, "%b %e %H:%M", &tm);
    }
}

/* ---------- Read directory ---------- */
int read_directory(const char *dirpath, FileEntry **out_entries) {
    DIR *d = opendir(dirpath);
    if (!d) return -1;

    struct dirent *ent;
    FileEntry *arr = NULL;
    size_t cap = 0, n = 0;

    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        if (n + 1 > cap) {
            cap = (cap == 0) ? 64 : cap * 2;
            FileEntry *tmp = realloc(arr, cap * sizeof(FileEntry));
            if (!tmp) { free(arr); closedir(d); return -1; }
            arr = tmp;
        }

        FileEntry *fe = &arr[n];
        fe->name = strdup(ent->d_name);
        size_t needed = strlen(dirpath) + 1 + strlen(ent->d_name) + 1;
        fe->path = malloc(needed);
        if (!fe->path) { perror("malloc"); exit(1); }
        if (strcmp(dirpath, ".") == 0) snprintf(fe->path, needed, "%s", ent->d_name);
        else snprintf(fe->path, needed, "%s/%s", dirpath, ent->d_name);

        fe->link_target = NULL;
        if (lstat(fe->path, &fe->st) != 0) {
            memset(&fe->st, 0, sizeof(struct stat));
        } else if (S_ISLNK(fe->st.st_mode)) {
            char tmp[PATH_MAX+1];
            ssize_t r = readlink(fe->path, tmp, PATH_MAX);
            if (r >= 0) { tmp[r] = '\0'; fe->link_target = strdup(tmp); }
        }
        n++;
    }

    closedir(d);
    *out_entries = arr;
    return (int)n;
}

void free_entries(FileEntry *arr, int n) {
    for (int i = 0; i < n; ++i) {
        free(arr[i].name);
        free(arr[i].path);
        if (arr[i].link_target) free(arr[i].link_target);
    }
    free(arr);
}

/* ---------- Colorized Printing ---------- */
void print_colored(const char *name, struct stat *st) {
    const char *color = COLOR_RESET;

    if (S_ISDIR(st->st_mode)) color = COLOR_DIR;
    else if (S_ISLNK(st->st_mode)) color = COLOR_SYMLINK;
    else if ((st->st_mode & S_IXUSR) || (st->st_mode & S_IXGRP) || (st->st_mode & S_IXOTH))
        color = COLOR_EXEC;
    else if (strstr(name, ".tar") || strstr(name, ".gz") || strstr(name, ".zip"))
        color = COLOR_ARCHIVE;
    else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode) || S_ISFIFO(st->st_mode) || S_ISSOCK(st->st_mode))
        color = COLOR_SPECIAL;

    printf("%s%s%s", color, name, COLOR_RESET);
}

/* ---------- Terminal width ---------- */
int get_terminal_width(void) {
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return FALLBACK_TERM_WIDTH;
}

/* ---------- Long Listing ---------- */
int print_long_listing(const char *dirpath) {
    FileEntry *entries = NULL;
    int n = read_directory(dirpath, &entries);
    if (n < 0) { fprintf(stderr, "Cannot read '%s': %s\n", dirpath, strerror(errno)); return -1; }
    if (n == 0) { free_entries(entries, n); return 0; }

    qsort(entries, n, sizeof(FileEntry), cmp_fileentry);

    int max_nlink = 0, max_owner = 0, max_group = 0, max_size = 0;
    unsigned long long total_blocks = 0ULL;

    for (int i = 0; i < n; ++i) {
        struct stat *st = &entries[i].st;
        int d = digits_unsigned((unsigned long long)st->st_nlink);
        if (d > max_nlink) max_nlink = d;
        struct passwd *pw = getpwuid(st->st_uid);
        struct group *gr = getgrgid(st->st_gid);
        int ow = pw ? (int)strlen(pw->pw_name) : digits_unsigned(st->st_uid);
        int gw = gr ? (int)strlen(gr->gr_name) : digits_unsigned(st->st_gid);
        if (ow > max_owner) max_owner = ow;
        if (gw > max_group) max_group = gw;
        int szw = digits_unsigned((unsigned long long)st->st_size);
        if (szw > max_size) max_size = szw;
        total_blocks += (unsigned long long)st->st_blocks;
    }

    printf("total %llu\n", total_blocks / 2);

    for (int i = 0; i < n; ++i) {
        struct stat *st = &entries[i].st;
        char perms[11], timebuf[64];
        mode_to_perm(st->st_mode, perms);
        format_mtime(st->st_mtime, timebuf, sizeof(timebuf));

        struct passwd *pw = getpwuid(st->st_uid);
        struct group *gr = getgrgid(st->st_gid);

        printf("%s %*lu %-*s %-*s %*lld %s ",
               perms,
               max_nlink, (unsigned long)st->st_nlink,
               max_owner, pw ? pw->pw_name : "",
               max_group, gr ? gr->gr_name : "",
               max_size, (long long)st->st_size,
               timebuf);

        if (entries[i].link_target) {
            print_colored(entries[i].name, st);
            printf(" -> %s\n", entries[i].link_target);
        } else {
            print_colored(entries[i].name, st);
            putchar('\n');
        }
    }

    free_entries(entries, n);
    return 0;
}

/* ---------- Column / Horizontal Listing ---------- */
void print_horizontal_listing(const char *dirpath) {
    FileEntry *entries = NULL;
    int n = read_directory(dirpath, &entries);
    if (n <= 0) { free_entries(entries, n); return; }

    qsort(entries, n, sizeof(FileEntry), cmp_fileentry);

    int term_width = get_terminal_width();
    int max_len = 0;
    for (int i = 0; i < n; ++i) {
        int len = (int)strlen(entries[i].name);
        if (len > max_len) max_len = len;
    }
    int col_width = max_len + COLUMN_SPACING;
    int ncols = term_width / col_width;
    if (ncols == 0) ncols = 1;
    int nrows = (n + ncols - 1) / ncols;

    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            int idx = c * nrows + r;
            if (idx < n) {
                print_colored(entries[idx].name, &entries[idx].st);
                if (c != ncols - 1 && idx + nrows < n)
                    printf("%*s", col_width - (int)strlen(entries[idx].name), "");
            }
        }
        putchar('\n');
    }

    free_entries(entries, n);
}

/* ---------- Main ---------- */
int main(int argc, char *argv[]) {
    int lflag = 0, xflag = 0;
    int opt;
    while ((opt = getopt(argc, argv, "lx")) != -1) {
        switch (opt) {
            case 'l': lflag = 1; break;
            case 'x': xflag = 1; break;
            default:
                fprintf(stderr, "Usage: %s [-l] [-x] [file...]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind == argc) {
        /* No directories/files specified, use current directory */
        if (lflag) print_long_listing(".");
        else print_horizontal_listing(".");
    } else {
        for (int i = optind; i < argc; ++i) {
            struct stat st;
            if (lstat(argv[i], &st) == 0 && !S_ISDIR(st.st_mode)) {
                /* Single file */
                if (lflag) {
                    char perms[11], timebuf[64];
                    mode_to_perm(st.st_mode, perms);
                    format_mtime(st.st_mtime, timebuf, sizeof(timebuf));
                    struct passwd *pw = getpwuid(st.st_uid);
                    struct group *gr = getgrgid(st.st_gid);
                    printf("%s %lu %s %s %lld %s ", perms,
                           (unsigned long)st.st_nlink,
                           pw ? pw->pw_name : "",
                           gr ? gr->gr_name : "",
                           (long long)st.st_size,
                           timebuf);
                    print_colored(argv[i], &st);
                    putchar('\n');
                } else {
                    print_colored(argv[i], &st);
                    putchar('\n');
                }
            } else {
                /* Directory */
                if (argc - optind > 1) printf("%s:\n", argv[i]);
                if (lflag) print_long_listing(argv[i]);
                else print_horizontal_listing(argv[i]);
                if (i != argc - 1) putchar('\n');
            }
        }
    }

    return 0;
}
