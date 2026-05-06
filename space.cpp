#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sched.h>
#include <unistd.h>
#include <limits.h> // For PATH_MAX
#include <fstream>
#include <string>
#include <sys/stat.h>

void setup_cgroups(int pid) {
    // Create a folder directly in the root cgroup mount
    std::string path = "/sys/fs/cgroup/space";
    
    // Create the folder (the 'cage')
    // system(("sudo mkdir -p " + path).c_str());
    if (mkdir(path.c_str(), 0755) == -1) {
    if (errno != EEXIST) { // If error is NOT "already exists", report it
        perror("mkdir failed");
    }
}

    // Set the limit (e.g., 100MB) 
    // In the latest versions of Ubuntu, the file is named 'memory.max'
    std::ofstream limit_file(path + "/memory.max");
    limit_file << "100M"; 
    limit_file.close();

    // 3. Attach the process
    std::ofstream procs_file(path + "/cgroup.procs");
    procs_file << pid;
    procs_file.close();
}

// This function sets up the container
int container_main(void* arg) {
    std::cout << "--- Container Started ---" << std::endl;

    char** user_args = (char**)arg; // // Cast the raw pointer back to a string array

    // for (int i = 0; user_args[i] != nullptr; i++) {
    //     std::cout << "arg[" << i << "] = " << user_args[i] << std::endl;
    // }

    /* 
    The mount function is used to mount a file-system within the container and it marks it private

    Explanation of the args being passed to the mount function:
    ----------------------------------------------------------
    1. NULL (source): We aren't mounting a device (like /dev/sda). We are just changing the settings of an existing path.
    2. "/" (target): We are applying this rule to the very top of the filesystem tree.
    3. NULL: NULL	Since there's no new device, we don't need a driver type (like ext4 or ntfs).
    4. MS_REC | MS_PRIVATE (flags): 
        MS_REC is recursively marking all directories under / as private
        MS_PRIVATE marks the given path as private so that the hostmachine doesn't see the container's file system
    5. NULL (data): No extra specialized strings are needed for this operation.  

    */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        perror("mount-private");
        return 1;
    }

    // DYNAMIC PATH DETECTION
    // We look for a folder named "rootfs" in the current directory.
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        perror("getcwd");
        return 1;
    }
    // std::string jail_path = std::string(cwd) + "/rootfs";
    
    // std::cout << "Jailing process into: " << jail_path << std::endl;

    // This combines the "Image" (lower) and a "User layer" (upper)
    // into a single "Merged" view.
    std::string options = "lowerdir=var/lib/space/images/alpine,"
                      "upperdir=var/lib/space/containers/c1/upper,"
                      "workdir=var/lib/space/containers/c1/work";

    if (mount("overlay", "var/lib/space/containers/c1/merged", "overlay", 0, options.c_str()) == -1) {
        perror("overlay mount failed");
        return 1;
    }

    // ENTER THE JAIL (chroot)
    if (chroot("var/lib/space/containers/c1/merged") != 0) {
        std::cerr << "Error: Could not chroot. Ensure a folder exists in: " 
                  << cwd << std::endl;
        perror("chroot failed");
        return 1;
    }

    // Move to a new root
    // After chroot, we are technically 'outside' the new root until we chdir.
    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    // Set a new hostname for the container
    std::string hostname = "space";
    size_t hostname_len = hostname.length();
    sethostname(hostname.c_str(), hostname_len);

    /* Step to isolate processes inside the new container (Mount /proc)

    We mount /proc INSIDE the container so 'ps' works and only shows container apps.
    Note: The 'rootfs' folder MUST have an empty /proc directory inside it.

    mount function arguments:
    1. proc: This is just a label. We can even change it to any name. But it should be proc for us to understand
    2. /proc: The /proc inside the rootfs filesystem that should be used as the proc directory
    3. proc: This tells the kernel to use the proc filesystem driver from the list of registered filesystem drivers
    4. 0: Use the default read-only behaviour
    5. NULL: We aren't passing any extra configuration information 
    */
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount-proc (ensure rootfs/proc exists)");
        return 1;
    }

    clearenv(); // to make sure the hostmachine's env variables doesn't leak into the child process
    setenv("PATH", "/bin:/sbin/:/usr/bin:/usr/sbin", 1);
    setenv("TERM", "xterm-256color", 1);
    setenv("HOME", "/root", 1);

    // Launch the shell
    // execv will look for /bin/sh INSIDE your rootfs folder. And it will replace the C++ code of the child process with the shell
    std::cout << "Attempting to exec: " << user_args[0] << "\n";
    if (execv(user_args[0], user_args) == -1) {
        perror("execv failed (is /bin/sh inside your rootfs?)");
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
    std::cout << "--- Starting Parent Process ---" << std::endl;

    if (argc < 2) {
        std::cerr << "Usage: sudo ./space <command> <args...>\n";
        return 1;
    }

    char** container_args = &argv[1];

    // Allocate stack memory for the child process
    const int STACK_SIZE = 65536;
    char* stack = new char[STACK_SIZE];

    // clone() flags: 
    // CLONE_NEWUTS: Tells the function to give the ability to set an isolated Hostname
    // CLONE_NEWPID: Isolated Process IDs (Child becomes PID 1)
    // CLONE_NEWNS: Isolates Mounts. It gives the container its own private list of mounted filesystems
    // SIGCHLD: Tells the parent when the child finishes
    // NULL: We are passing NULL as an argument into the container_main function. We can use this to pass configuration data into the new container if required
    int container_pid = clone(container_main, stack + STACK_SIZE, 
                              CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, container_args);

    if (container_pid == -1) {
        perror("clone");
        return 1;
    }

    // PARENT SIDE: Setup the 'cage' for the child
    setup_cgroups(container_pid);

    // Wait for the container to exit
    waitpid(container_pid, NULL, 0);
    std::cout << "--- Container Exited ---" << std::endl;

    delete[] stack;
    return 0;
}
