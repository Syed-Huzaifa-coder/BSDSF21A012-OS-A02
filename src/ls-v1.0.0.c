/* src/ls-v1.0.0.c  -- ls v1.1.0 (long listing with -l using getopt) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <getopt.h>

#define SIX_MONTHS_SECONDS (15552000) /* ~ 6*30*24*3600 */

typedef struct {
    char *name;
    char *path;
    struct stat st;
    char *link_target;
} FileEntry;

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
    /* use ctime_r as the assignment asked to use ctime (we still use strftime for final layout) */
    char ctime_buf[26];
    if (ctime_r(&mtime, ctime_buf) == NULL) {
        /* fallback: format via strftime */
        strftime(buf, bufsz, "%b %e %H:%M", &tm);
        return;
    }

    time_t now = time(NULL);
    if (llabs(now - mtime) > SIX_MONTHS_SECONDS || mtime > now + 60) {
        strftime(buf, bufsz, "%b %e  %Y", &tm); /* two spaces before year to align like GNU ls */
    } else {
        strftime(buf, bufsz, "%b %e %H:%M", &tm);
    }
}

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

int print_simple(const char *dirpath) {
    FileEntry *entries = NULL;
    int n = read_directory(dirpath, &entries);
    if (n < 0) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", dirpath, strerror(errno));
        return -1;
    }
    qsort(entries, n, sizeof(FileEntry), cmp_fileentry);
    for (int i = 0; i < n; ++i) printf("%s\n", entries[i].name);
    free_entries(entries, n);
    return 0;
}

int main(int argc, char **argv) {
    int longflag = 0;
    int opt;
    /* parse -l using getopt */
    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
            case 'l': longflag = 1; break;
            default:
                fprintf(stderr, "Usage: %s [-l] [file...|dir...]\n", argv[0]);
                return 1;
        }
    }

    /* remaining args are paths starting at argv[optind] */
    if (optind == argc) {
        if (longflag) return print_long_listing(".");
        else return print_simple(".");
    }

    int many = (argc - optind) > 1;
    for (int i = optind; i < argc; ++i) {
        const char *p = argv[i];
        struct stat st;
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (many) printf("%s:\n", p);
            if (longflag) print_long_listing(p);
            else print_simple(p);
            if (i + 1 < argc) printf("\n");
        } else if (lstat(p, &st) == 0) {
            if (longflag) {
                /* print single-file long listing */
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
                printf("%s\n", p);
            }
        } else {
            fprintf(stderr, "Cannot access '%s': %s\n", p, strerror(errno));
        }
    }
    return 0;
}
