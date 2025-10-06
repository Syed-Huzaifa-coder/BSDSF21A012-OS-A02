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

## Q3: Explain the general logic for printing items in a "down then across" columnar format. Why is a simple single loop insufficient?

Logic (down then across):

Determine total items N and the maximum filename length M.
Compute terminal width W and decide col_width = M + spacing.
Compute maximum number of columns C = max(1, W / col_width) and rows R = ceil(N / C).
Print row by row: for each row index r (0..R-1) print items at indices r, r+R, r+2R, … (r + k*R) until >= N. Each printed item is padded to col_width (except last column).

A simple single loop printing filenames sequentially (across rows) is insufficient because it fills rows left-to-right and produces a different ordering. The "down then across" layout requires computing the two-dimensional mapping between a linear list and a (rows × columns) grid and printing items in the row-major order that corresponds to filling columns top-to-bottom first. This ensures visually balanced columns and matches the behavior of the standard ls.

## Q4: What is the purpose of the ioctl system call in this context? What are the limitations if you only used a fixed-width fallback?

Purpose of ioctl:

ioctl with TIOCGWINSZ requests the terminal's window size (number of columns) from the kernel. This allows your program to compute how many columns will fit on the current terminal width and adapt the layout dynamically to the user's environment.

Limitations of fixed-width fallback:

If you only use a fixed width (e.g., 80 columns), the output may under-utilize wide terminals (leaving unused horizontal space) or overflow narrow terminals, causing undesirable wrapping. Dynamic ioctl-based detection ensures the output uses the available space efficiently and behaves well when users resize their terminal windows.

## Q5: Compare vertical vs horizontal complexity

Vertical (down-then-across) requires pre-calculation of rows and careful index jumps like filenames[i + num_rows], so it’s more complex.

Horizontal (-x) is simpler: you just print left to right, checking when to wrap. Only need to track current position vs terminal width.

## Q6: Strategy for managing modes

Used a single state variable (display_mode) set during argument parsing (-l → long listing, -x → horizontal, default → vertical).

After gathering filenames, the program checks this variable and calls the correct function (display_long_listing, display_horizontal, or display_vertical).

This ensures clean separation of logic and makes adding future modes easier.

## Q7: Why is it necessary to read all directory entries into memory before you can sort them?

Sorting requires access to the complete list of filenames.

If you process entries one by one (streaming), you can’t reorder them without storing first.

Drawback: For very large directories (millions of files), memory usage can be huge → may cause slowdowns or even out-of-memory errors.

## Q8: Explain the purpose and signature of the comparison function required by qsort().

qsort() needs to know how to compare two elements of your array.
Its comparator must have this signature:

int cmp(const void *a, const void *b);

const void * is generic → allows qsort to work with any type (int, float, string, struct, etc.).
Inside the comparator:
You cast to your actual type (e.g., char** for strings).
Return <0 if a<b, 0 if equal, >0 if a>b.
For strings: we use strcmp().

## Q9: How ANSI escape codes work

They are sequences starting with \033[ (ESC character), followed by style codes.
Example for green:

printf("\033[0;32mThis text is green\033[0m\n");
0 = reset style, 32 = green text, 0m = reset color to default.

## Q10: Executable file permission bits

st_mode & S_IXUSR → executable by owner
st_mode & S_IXGRP → executable by group
st_mode & S_IXOTH → executable by others

## Q11: What is a "base case" in a recursive function?

A base case is a condition that stops a recursive function from calling itself indefinitely. It provides an exit point for the recursion. Without a base case, the function would keep calling itself forever, leading to a stack overflow.

In the context of our recursive ls (-R) implementation:
The base case occurs when the function encounters a directory that has no subdirectories.
At this point, the recursion stops, because there are no more subdirectories to list.

## Q12: Why is it essential to construct a full path before making a recursive call?

When recursively listing directories, each call must know the exact path of the subdirectory it needs to explore.
If you construct the full path like "parent_dir/subdir", the function correctly accesses the subdirectory inside the parent directory.
If you simply call do_ls("subdir") without the parent path, the function will look for subdir in the current working directory, not inside "parent_dir".
This would either fail (if subdir isn’t in the current directory) or list the wrong directory.

Summary: Full paths ensure the recursion navigates the actual directory tree correctly.








