# REPORT - BSDSF21A012-OS-A02

## Student
Name: SYED HUZAIFA
Roll No: BSDSF21A012

## Steps performed
1. Created GitHub repo: https://github.com/Syed-Huzaifa-coder/BSDSF21A012-OS-A02
2. Cloned repo locally.
3. Copied starter code into `src/`.
4. Created directories: bin/, obj/, man/.
5. Created REPORT.md and updated README.md.
6. Updated Makefile to build bin/ls.
7. Ran `make` and tested `./bin/ls`.

## Build & run commands used
make
./bin/ls
./bin/ls /tmp/ls-test

## Observations / Issues
- <Any compile error you fixed>
- <Platform notes>

## Files included
- src/ls-v1.0.0.c
- Makefile
- bin/ls (built)
- obj/ls-v1.0.0.o
- man/  (empty)
- REPORT.md

## Q1: What is the crucial difference between the stat() and lstat() system calls? In the context of the ls command, when is it more appropriate to use lstat()?

stat(): Returns information about the target file. If the file is a symbolic link, stat() follows the link and reports information about the file the link points to.

lstat(): Returns information about the link itself. If the file is a symbolic link, lstat() reports details about the link (permissions, size of the link path, etc.), not the target file.

👉 In the context of the ls command, lstat() is more appropriate because when we use ls -l, we need to show details of the link itself (like link_to_file1 -> file1.txt), not just the target file.

## Q2: The st_mode field in struct stat is an integer that contains both the file type (e.g., regular file, directory) and the permission bits. Explain how you can use bitwise operators (like &) and predefined macros (like S_IFDIR or S_IRUSR) to extract this information.

The st_mode field stores bitwise flags representing both file type and permissions.
We can use bitwise AND (&) with macros to check specific bits.

// Check file type
if (st.st_mode & S_IFDIR) {
    printf("This is a directory\n");
}
if (S_ISREG(st.st_mode)) {
    printf("This is a regular file\n");
}

// Check permissions
if (st.st_mode & S_IRUSR) {
    printf("Owner has read permission\n");
}
if (st.st_mode & S_IWGRP) {
    printf("Group has write permission\n");
}
if (st.st_mode & S_IXOTH) {
    printf("Others have execute permission\n");
}
Using bitwise operators allows us to extract file type and permissions individually from st_mode. This is how the ls -l command prints the familiar drwxr-xr-x style output.


