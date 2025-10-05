/* src/ls-v1.0.0.c  -- ls v1.2.0
   Adds column display (down-then-across) for default listing (no -l).
   Keep -l long listing behavior from v1.1.0.
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

#define SIX_MONTHS_SECONDS (15552000) /* ~ 6*30*24*3600 */
#define COLUMN_SPACING 2              /* spaces between columns */
#define FALLBACK_TERM_WIDTH 80

typedef struct {
    char *name;
    char *path;
    struct stat st;
    char *link_target;
} FileEntry;

/* ---------- utilities from v1.1.0 ---------- */

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

/* ---------- directory reading ---------- */

int read_directory(const char *dirpath, FileEntry **out_entries) {
    DIR *d = opendir(dirpath);
    if (!d) return -1;

    struct dirent *ent;
    FileEntry *arr = NULL;
    size_t cap = 0;
    size_t n = 0;

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
        } else {
            if (S_ISLNK(fe->st.st_mode)) {
                char tmp[PATH_MAX+1];
                ssize_t r = readlink(fe->path, tmp, PATH_MAX);
                if (r >= 0) {
                    tmp[r] = '\0';
                    fe->link_target = strdup(tmp);
                }
            }
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

/* ---------- long listing (unchanged) ---------- */

int print_long_listing(const char *dirpath) {
    FileEntry *entries = NULL;
    int n = read_directory(dirpath, &entries);
    if (n < 0) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", dirpath, strerror(errno));
        return -1;
    }
    if (n == 0) { free_entries(entries, n); return 0; }

    qsort(entries, n, sizeof(FileEntry), cmp_fileentry);

    int max_nlink_w = 0, max_owner_w = 0, max_group_w = 0, max_size_w = 0;
    unsigned long long total_blocks = 0ULL;

    for (int i = 0; i < n; ++i) {
        struct stat *st = &entries[i].st;
        int d = digits_unsigned((unsigned long long)st->st_nlink);
        if (d > max_nlink_w) max_nlink_w = d;
        struct passwd *pw = getpwuid(st->st_uid);
        struct group *gr = getgrgid(st->st_gid);
        int ow = pw ? (int)strlen(pw->pw_name) : digits_unsigned((unsigned long long)st->st_uid);
        int gw = gr ? (int)strlen(gr->gr_name) : digits_unsigned((unsigned long long)st->st_gid);
        if (ow > max_owner_w) max_owner_w = ow;
        if (gw > max_group_w) max_group_w = gw;
        int szw = digits_unsigned((unsigned long long)st->st_size);
        if (szw > max_size_w) max_size_w = szw;
        total_blocks += (unsigned long long)st->st_blocks;
    }

    unsigned long long total_1k = total_blocks / 2ULL;
    printf("total %llu\n", total_1k);

    for (int i = 0; i < n; ++i) {
        char perm[11];
        mode_to_perm(entries[i].st.st_mode, perm);

        unsigned long long nlink = (unsigned long long)entries[i].st.st_nlink;
        struct passwd *pw = getpwuid(entries[i].st.st_uid);
        struct group *gr = getgrgid(entries[i].st.st_gid);
        char ownerbuf[64], groupbuf[64];
        if (pw) snprintf(ownerbuf, sizeof(ownerbuf), "%s", pw->pw_name);
        else snprintf(ownerbuf, sizeof(ownerbuf), "%u", (unsigned)entries[i].st.st_uid);
        if (gr) snprintf(groupbuf, sizeof(groupbuf), "%s", gr->gr_name);
        else snprintf(groupbuf, sizeof(groupbuf), "%u", (unsigned)entries[i].st.st_gid);

        char timebuf[64];
        format_mtime(entries[i].st.st_mtime, timebuf, sizeof(timebuf));

        if (entries[i].link_target) {
            printf("%s %*llu %-*s %-*s %*lld %s %s -> %s\n",
                   perm,
                   max_nlink_w, nlink,
                   max_owner_w, ownerbuf,
                   max_group_w, groupbuf,
                   max_size_w, (long long)entries[i].st.st_size,
                   timebuf,
                   entries[i].name,
                   entries[i].link_target);
        } else {
            printf("%s %*llu %-*s %-*s %*lld %s %s\n",
                   perm,
                   max_nlink_w, nlink,
                   max_owner_w, ownerbuf,
                   max_group_w, groupbuf,
                   max_size_w, (long long)entries[i].st.st_size,
                   timebuf,
                   entries[i].name);
        }
    }

    free_entries(entries, n);
    return 0;
}

/* ---------- new column printing (down then across) ---------- */

/* get terminal width in columns; fallback to 80 if not a tty or ioctl fails */
int get_terminal_width(void) {
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    return FALLBACK_TERM_WIDTH;
}

/* print names in columns (down then across) */
int print_columns(const char *dirpath) {
    FileEntry *entries = NULL;
    int n = read_directory(dirpath, &entries);
    if (n < 0) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", dirpath, strerror(errno));
        return -1;
    }
    if (n == 0) { free_entries(entries, n); return 0; }

    qsort(entries, n, sizeof(FileEntry), cmp_fileentry);

    /* gather names and longest length */
    char **names = malloc(n * sizeof(char*));
    if (!names) { free_entries(entries, n); return -1; }
    int maxlen = 0;
    for (int i = 0; i < n; ++i) {
        names[i] = entries[i].name; /* reuse pointer from entries */
        int L = (int)strlen(names[i]);
        if (L > maxlen) maxlen = L;
    }

    /* if output is not a terminal, fallback to single-column (common behavior) */
    if (!isatty(STDOUT_FILENO)) {
        for (int i = 0; i < n; ++i) printf("%s\n", names[i]);
        free(names);
        free_entries(entries, n);
        return 0;
    }

    int term_width = get_terminal_width();
    int col_width = maxlen + COLUMN_SPACING;
    if (col_width <= 0) col_width = 1;
    int cols = term_width / col_width;
    if (cols < 1) cols = 1;
    if (cols > n) cols = n;

    /* compute rows: ceil(n / cols) */
    int rows = (n + cols - 1) / cols;

    /* print row by row: down-then-across */
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = c * rows + r; /* mapping for down-then-across */
            if (idx >= n) continue;
            /* for last column, we don't need extra spaces after name */
            if (c == cols - 1) {
                printf("%s", names[idx]);
            } else {
                /* pad name to maxlen + COLUMN_SPACING */
                int pad = maxlen - (int)strlen(names[idx]) + COLUMN_SPACING;
                printf("%s", names[idx]);
                for (int p = 0; p < pad; ++p) putchar(' ');
            }
        }
        putchar('\n');
    }

    free(names);
    free_entries(entries, n);
    return 0;
}

void display_horizontal(char **names, int count, int maxlen, int term_width) {
    int col_width = maxlen + 2;  // filename length + spacing
    int pos = 0;  // track current width position

    for (int i = 0; i < count; i++) {
        // If next filename won't fit in current row, start a new line
        if (pos + col_width > term_width) {
            printf("\n");
            pos = 0;
        }

        // Print filename padded to column width
        printf("%-*s", col_width, names[i]);
        pos += col_width;
    }
    printf("\n");  // final newline
}

int print_horizontal(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Cannot open directory '%s': %s\n", path, strerror(errno));
        return 1;
    }

    // Step 1: collect filenames
    char **names = NULL;
    int count = 0, capacity = 0;
    struct dirent *entry;
    int maxlen = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // skip hidden files

        if (count >= capacity) {
            capacity = capacity ? capacity * 2 : 16;
            names = realloc(names, capacity * sizeof(char *));
        }
        names[count] = strdup(entry->d_name);
        int len = strlen(entry->d_name);
        if (len > maxlen) maxlen = len;
        count++;
    }
    closedir(dir);

    // Step 2: get terminal width
    struct winsize ws;
    int term_width = 80; // default fallback
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        term_width = ws.ws_col;
    }

    // Step 3: display horizontally
    if (count > 0)
        display_horizontal(names, count, maxlen, term_width);

    // Cleanup
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);

    return 0;
}


/* ---------- main and dispatch ---------- */

int main(int argc, char **argv) {
    int longflag = 0;
    int xflag = 0;   // NEW: horizontal flag

    int opt;
    while ((opt = getopt(argc, argv, "lRx")) != -1) {  // added x
        switch (opt) {
            case 'l': longflag = 1; break;
            case 'x': xflag = 1; break;
            /* case 'R': recflag = 1; break; */
            default:
                fprintf(stderr, "Usage: %s [-l] [-R] [-x] [file...|dir...]\n", argv[0]);
                return 1;
        }
    }

    if (optind == argc) {
        if (longflag) return print_long_listing(".");
        else if (xflag) return print_horizontal(".");   // NEW dispatch
        else return print_columns(".");
    }

    int many = (argc - optind) > 1;
    for (int i = optind; i < argc; ++i) {
        const char *p = argv[i];
        struct stat st;

        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (many) printf("%s:\n", p);

            if (longflag) print_long_listing(p);
            else if (xflag) print_horizontal(p);   // NEW dispatch
            else print_columns(p);

            if (i + 1 < argc) printf("\n");
        } else if (lstat(p, &st) == 0) {
            if (longflag) {
                // existing single-file long listing logic…
                FileEntry fe;
                memset(&fe, 0, sizeof(fe));
                fe.name = strdup(p);
                fe.path = strdup(p);
                if (lstat(fe.path, &fe.st) == 0) {
                    if (S_ISLNK(fe.st.st_mode)) {
                        char tmp[PATH_MAX+1];
                        ssize_t r = readlink(fe.path, tmp, PATH_MAX);
                        if (r >= 0) { tmp[r] = '\0'; fe.link_target = strdup(tmp); }
                    }
                    int nlink_w = digits_unsigned((unsigned long long)fe.st.st_nlink);
                    struct passwd *pw = getpwuid(fe.st.st_uid);
                    struct group *gr = getgrgid(fe.st.st_gid);
                    int ow = pw ? (int)strlen(pw->pw_name) : digits_unsigned((unsigned long long)fe.st.st_uid);
                    int gw = gr ? (int)strlen(gr->gr_name) : digits_unsigned((unsigned long long)fe.st.st_gid);
                    int szw = digits_unsigned((unsigned long long)fe.st.st_size);
                    char perm[11]; mode_to_perm(fe.st.st_mode, perm);
                    char timebuf[64]; format_mtime(fe.st.st_mtime, timebuf, sizeof(timebuf));
                    char ownerbuf[64], groupbuf[64];
                    if (pw) snprintf(ownerbuf, sizeof(ownerbuf), "%s", pw->pw_name);
                    else snprintf(ownerbuf, sizeof(ownerbuf), "%u", (unsigned)fe.st.st_uid);
                    if (gr) snprintf(groupbuf, sizeof(groupbuf), "%s", gr->gr_name);
                    else snprintf(groupbuf, sizeof(groupbuf), "%u", (unsigned)fe.st.st_gid);

                    if (fe.link_target) {
                        printf("%s %*llu %-*s %-*s %*lld %s %s -> %s\n",
                               perm, nlink_w, (unsigned long long)fe.st.st_nlink,
                               ow, ownerbuf, gw, groupbuf, szw, (long long)fe.st.st_size,
                               timebuf, fe.name, fe.link_target);
                    } else {
                        printf("%s %*llu %-*s %-*s %*lld %s %s\n",
                               perm, nlink_w, (unsigned long long)fe.st.st_nlink,
                               ow, ownerbuf, gw, groupbuf, szw, (long long)fe.st.st_size,
                               timebuf, fe.name);
                    }
                }
                free(fe.name); free(fe.path); if (fe.link_target) free(fe.link_target);
            } else {
                printf("%s\n", p);  // single file default/horizontal both just print name
            }
        } else {
            fprintf(stderr, "Cannot access '%s': %s\n", p, strerror(errno));
        }
    }
    return 0;
}
