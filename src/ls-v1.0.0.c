#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>

/* ---------- ANSI Color Codes ---------- */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_PINK    "\033[1;35m"
#define COLOR_REVERSE "\033[7m"

/* ---------- Function Prototypes ---------- */
void do_ls(const char *dirname, int lflag, int xflag, int recursive_flag);
void print_colored(const char *name, const struct stat *st);
int print_long_listing(const char *dirpath);
void print_horizontal_listing(const char *dirpath);
void mode_to_perm(mode_t mode, char out[11]);
void format_mtime(time_t mtime, char *buf, size_t bufsize);

/* ---------- Helper: Print Colored File ---------- */
void print_colored(const char *name, const struct stat *st) {
    if (S_ISDIR(st->st_mode)) {
        printf(COLOR_BLUE "%s" COLOR_RESET, name);
    } else if (S_ISLNK(st->st_mode)) {
        printf(COLOR_PINK "%s" COLOR_RESET, name);
    } else if (st->st_mode & S_IXUSR) {
        printf(COLOR_GREEN "%s" COLOR_RESET, name);
    } else if (strstr(name, ".tar") || strstr(name, ".gz") || strstr(name, ".zip")) {
        printf(COLOR_RED "%s" COLOR_RESET, name);
    } else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode) || S_ISFIFO(st->st_mode) || S_ISSOCK(st->st_mode)) {
        printf(COLOR_REVERSE "%s" COLOR_RESET, name);
    } else {
        printf("%s", name);
    }
}

/* ---------- Helper: Mode to Permission String ---------- */
void mode_to_perm(mode_t mode, char out[11]) {
    out[0] = (S_ISDIR(mode)) ? 'd' : (S_ISLNK(mode)) ? 'l' : '-';
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

/* ---------- Helper: Format Modification Time ---------- */
void format_mtime(time_t mtime, char *buf, size_t bufsize) {
    struct tm *tm_info = localtime(&mtime);
    strftime(buf, bufsize, "%b %d %H:%M", tm_info);
}

/* ---------- Horizontal Listing ---------- */
void print_horizontal_listing(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) { perror("opendir"); return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        struct stat st;
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        if (lstat(fullpath, &st) != 0) continue;
        print_colored(entry->d_name, &st);
        printf("\t");
    }
    printf("\n");
    closedir(dir);
}

/* ---------- Long Listing ---------- */
int print_long_listing(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) { perror("opendir"); return -1; }
    struct dirent **namelist = NULL;
    int n = 0, capacity = 64;
    namelist = malloc(sizeof(struct dirent*) * capacity);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (n >= capacity) namelist = realloc(namelist, sizeof(struct dirent*) * (capacity *= 2));
        namelist[n++] = entry;
    }
    closedir(dir);

    // Sort alphabetically
    for (int i = 0; i < n-1; ++i) {
        for (int j = i+1; j < n; ++j) {
            if (strcmp(namelist[i]->d_name, namelist[j]->d_name) > 0) {
                struct dirent *tmp = namelist[i];
                namelist[i] = namelist[j];
                namelist[j] = tmp;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, namelist[i]->d_name);
        struct stat st;
        if (lstat(fullpath, &st) != 0) continue;
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
        print_colored(namelist[i]->d_name, &st);
        printf("\n");
    }

    free(namelist);
    return 0;
}

/* ---------- Core LS Function (Recursive) ---------- */
void do_ls(const char *dirname, int lflag, int xflag, int recursive_flag) {
    DIR *dir = opendir(dirname);
    if (!dir) { perror(dirname); return; }

    if (recursive_flag)
        printf("%s:\n", dirname);

    if (lflag) print_long_listing(dirname);
    else print_horizontal_listing(dirname);

    if (recursive_flag) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, entry->d_name);
            struct stat st;
            if (lstat(fullpath, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                printf("\n");
                do_ls(fullpath, lflag, xflag, recursive_flag);
            }
        }
    }

    closedir(dir);
}

/* ---------- Main ---------- */
int main(int argc, char *argv[]) {
    int lflag = 0, xflag = 0;
    int recursive_flag = 0;
    int opt;

    while ((opt = getopt(argc, argv, "lxR")) != -1) {
        switch (opt) {
            case 'l': lflag = 1; break;
            case 'x': xflag = 1; break;
            case 'R': recursive_flag = 1; break;
            default:
                fprintf(stderr, "Usage: %s [-l] [-x] [-R] [file...]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind == argc) {
        do_ls(".", lflag, xflag, recursive_flag);
    } else {
        for (int i = optind; i < argc; ++i) {
            struct stat st;
            if (lstat(argv[i], &st) == 0 && !S_ISDIR(st.st_mode)) {
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
                do_ls(argv[i], lflag, xflag, recursive_flag);
                if (i != argc - 1) putchar('\n');
            }
        }
    }

    return 0;
}

