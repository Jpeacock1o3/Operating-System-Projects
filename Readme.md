# Operating System Projects

This repository contains a collection of projects focused on fundamental operating system concepts. Each project is implemented in C and explores a different area of OS design, including process management, inter-process communication, and memory management.

## Projects

1.  [Mini-Shell](#1-mini-shell)
2.  [Concurrency and Channels](#2-concurrency-and-channels)
3.  [Dynamic Memory Allocator](#3-dynamic-memory-allocator)

---

## 1. Mini-Shell

This project is a lightweight, Bash-like command-line interpreter that supports essential shell functionalities. It can execute commands, manage environment variables, and handle complex command sequences with various operators.

### Core Features:

* **Command Execution**: The shell can run external applications and handle an arbitrary number of command-line arguments. Each command is executed in a separate child process created using `fork()`.
* **Built-in Commands**:
    * `cd`: Changes the current working directory, supporting both relative and absolute paths.
    * `exit` / `quit`: Terminates the shell session.
* **Environment Variables**: Supports setting and using environment variables. Variables are inherited from the parent process and can be created or modified within the shell.
* **Operators**: The shell supports a variety of operators to control command execution flow, with the following priority:
    1.  **Pipe (`|`)**: Redirects the standard output of one command to the standard input of another.
    2.  **Conditional (`&&`, `||`)**:
        * `&&`: Executes the next command only if the previous one succeeds (exits with code 0).
        * `||`: Executes the next command only if the previous one fails (exits with a non-zero code).
    3.  **Parallel (`&`)**: Executes commands simultaneously in separate processes.
    4.  **Sequential (`;`)**: Executes commands one after another, regardless of their exit status.
* **I/O Redirection**: Provides comprehensive input/output redirection capabilities, allowing command file handles to be modified. Supported redirections include:
    * `< filename`: Redirects standard input.
    * `> filename`: Redirects standard output, overwriting the file.
    * `>> filename`: Appends standard output to a file.
    * `2> filename`: Redirects standard error.
    * `&> filename`: Redirects both standard output and standard error to the same file.

---

## 2. Concurrency and Channels

This project implements a message-passing channel in C, a powerful primitive for thread synchronization. The channel acts as a fixed-size, thread-safe queue where multiple sender threads can add messages and multiple receiver threads can retrieve them.

### Core Features:

* **Thread-Safe Buffer**: The channel is built around a buffer that is protected by a mutex to ensure safe concurrent access from multiple threads.
* **Blocking Operations**:
    * `channel_send()`: A blocking call that waits if the channel is full until space becomes available.
    * `channel_receive()`: A blocking call that waits if the channel is empty until a message is available.
* **Non-Blocking Operations**:
    * `channel_non_blocking_send()`: Immediately returns if the channel is full without sending the message.
    * `channel_non_blocking_receive()`: Immediately returns if the channel is empty without receiving a message.
* **Channel Lifecycle Management**:
    * `channel_close()`: Closes the channel, causing all subsequent and currently blocked operations to return with a `CLOSED_ERROR`.
    * `channel_destroy()`: Frees all memory associated with a channel, but only after it has been closed.
* **Select Operation**:
    * `channel_select()`: Allows a thread to monitor multiple channels and perform the first available send or receive operation, blocking only if no operations are ready.

---

## 3. Dynamic Memory Allocator

This project is a custom implementation of a dynamic memory allocator, providing replacements for the standard C library functions `malloc`, `free`, and `realloc`. The implementation focuses on both space efficiency and performance.

### Core Features:

* **Segregated Free Lists**: The allocator uses an array of 11 segregated free lists to manage free memory blocks. Each list corresponds to a different size class, which significantly speeds up the search for a suitable free block.
* **Best-Fit Allocation**: When searching for a free block, the allocator uses a best-fit strategy within the appropriate size class to minimize fragmentation and improve space utilization.
* **Boundary Tag Coalescing**: Free blocks are immediately coalesced with adjacent free blocks to create larger contiguous memory regions, which helps reduce external fragmentation.
* **Block Splitting**: When an allocated block is larger than the requested size, it is split into an allocated block and a new free block, provided the remainder is large enough. This helps to reduce internal fragmentation.
* **Realloc Implementation**: The `realloc` function is optimized to attempt in-place resizing by checking if the next block in the heap is free and large enough to accommodate the new size. If not, it allocates a new block and copies the data.
* **Heap Consistency Checker**: Includes a `mm_checkheap` function for debugging, which validates the heap's integrity by checking for issues like header/footer mismatches and alignment problems.
