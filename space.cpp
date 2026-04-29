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

void setup_cgroups(int pid) {
    // In v2, we create a folder directly in the root cgroup mount
    std::string path = "/sys/fs/cgroup/space";
    
    // 1. Create the folder (the 'cage')
    system(("sudo mkdir -p " + path).c_str());

    // 2. Set the limit (e.g., 100MB) 
    // In v2, the file is named 'memory.max'
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

    // 2. DYNAMIC PATH DETECTION
    // We look for a folder named "rootfs" in the current directory.
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        perror("getcwd");
        return 1;
    }
    std::string jail_path = std::string(cwd) + "/rootfs";
    
    std::cout << "Jailing process into: " << jail_path << std::endl;

    // 3. ENTER THE JAIL (chroot)
    // This makes the 'rootfs' folder appear as the root (/) to the container.
    if (chroot(jail_path.c_str()) != 0) {
        std::cerr << "Error: Could not chroot. Ensure a 'rootfs' folder exists in: " 
                  << cwd << std::endl;
        perror("chroot failed");
        return 1;
    }

    // 4. MOVE TO NEW ROOT
    // After chroot, we are technically 'outside' the new root until we chdir.
    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    // Set a new hostname for the container
    std::string hostname = "space";
    size_t hostname_len = hostname.length();
    sethostname(hostname.c_str(), hostname_len);

    // 6. ISOLATE PROCESSES (Mount /proc)
    // We mount /proc INSIDE the jail so 'ps' works and only shows container apps.
    // Note: The 'rootfs' folder MUST have an empty /proc directory inside it.
    if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
        perror("mount-proc (ensure rootfs/proc exists)");
        return 1;
    }

    // 7. LAUNCH SHELL
    // execv will look for /bin/sh INSIDE your rootfs folder.
    char* args[] = {(char*)"/bin/sh", NULL};
    if (execv(args[0], args) == -1) {
        perror("execv failed (is /bin/sh inside your rootfs?)");
        return 1;
    }

    return 0;
}

int main() {
    std::cout << "--- Starting Parent Process ---" << std::endl;

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
                              CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);

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
