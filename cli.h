#ifndef CLI_H
#define CLI_H
#include <iostream>
#include <cstdlib>

struct container_config {
    std::string image_name;
    std::string container_id;
    char** exec_args;

    std::string generate_id() { // this function generates a random id for the container if the user doesn't provide a name
        std::time_t t = std::time(nullptr);
        std::stringstream ss;
        ss << "container-" << std::hex << t; // e.g., container-6638f2a2
        return ss.str();
    }
};


namespace cli {

    inline container_config run(int argc, char** argv, container_config config) { 
        // Expected: "sudo ./space run --name <container_name> <image> <command>" => sudo ./space run --name test alpine /bin/sh
        std::string container_name;
        std::string image_name;
        char** exec_args = nullptr;

        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--name" && i + 1 < argc) {
                container_name = argv[++i];
            } else if (image_name.empty()) {
                image_name = arg;
                exec_args = &argv[i+1]; // Everything after image is the command
            }
        }

        if (container_name.empty()) {
            container_name = config.generate_id(); // Fallback to dynamic ID if no name provided
        }

        // This makes sure we don't use the run command for an already existing container
        std::string base_path = "var/lib/space/containers/" + container_name;
        if (std::filesystem::exists(base_path)) {
            std::cerr << "Error: Container \"" << container_name << "\" already exists.\n";
            std::cerr << "Use 'space start' to resume it or 'space rm' to delete it.\n";
            std::exit(EXIT_FAILURE);
        }

        config = { image_name, container_name, exec_args };
        return config;
    }


    inline void images() {
        std::string path = "var/lib/space/images";

        if (!std::filesystem::exists(path)) {
            std::cout << "No images found. Path does not exist: " << path << std::endl;
            return;
        }

        std::cout << std::left << std::setw(20) << "IMAGE NAME" << std::endl;
        std::cout << "--------------------" << std::endl;

        for (const auto& entry: std::filesystem::directory_iterator(path)) {
            if (entry.is_directory())  { // then this is an in image directory
                std::cout << entry.path().filename().string() << std::endl;
            }
        }
    }


    inline void ps() {
        std::string path = "var/lib/space/containers";

        if (!std::filesystem::exists(path)) {
            std::cout << "No containers found." << std::endl;
            return;
        }

        std::cout << std::left << std::setw(25) << "CONTAINER ID" 
                  << std::setw(20) << "IMAGE" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;

        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                
                // Read the image name from our "breadcrumb" file
                std::ifstream img_file(entry.path().string() + "/image_ref.txt");
                std::string img_name = "unknown";
                if (img_file.is_open()) {
                    std::getline(img_file, img_name);
                }

                std::cout << std::left << std::setw(25) << name 
                          << std::setw(20) << img_name << std::endl;
            }
        }
    }

    inline container_config start(int argc, char** argv, container_config config) {
        if (argc < 3) {
            std::cerr << "Usage: sudo ./space start <container_name> [command]\n";
            std::exit(EXIT_FAILURE);
        }

        std::string container_name = argv[2];
        std::string base_path = "var/lib/space/containers/" + container_name;

        if (!std::filesystem::exists(base_path)) {
            std::cerr << "Error: Container \"" << container_name << "\" not found.\n";
            std::exit(EXIT_FAILURE);
        }

        // Recover the image name from metadata
        std::ifstream img_file(base_path + "/image_ref.txt");
        std::string image_name;
        if (!std::getline(img_file, image_name)) {
            std::cerr << "Error: Could not determine image for " << container_name << "\n";
            std::exit(EXIT_FAILURE);
        }

        // If the user provided a command (argv[3]), use it. 
        // Otherwise, default to /bin/sh
        char** exec_args;
        if (argc > 3) {
            exec_args = &argv[3];
        } else {
            // Falling back to /bin/sh
            static char* default_cmd[] = {(char*)"/bin/sh", NULL};
            exec_args = default_cmd;
        }

        config = { image_name, container_name, exec_args };
        return config;
    }


    inline void rm(int argc, char** argv) {
        if (argc < 3) {
            std::cerr << "Usage: sudo ./space rm <container_name>\n";
            std::exit(EXIT_FAILURE);
        }

        std::string container_name = argv[2];
        std::string base_path = "var/lib/space/containers/" + container_name;

        if (!std::filesystem::exists(base_path)) {
            std::cerr << "Error: Container \"" << container_name << "\" not found.\n";
            std::exit(EXIT_FAILURE);
        }

        // The equivalent of 'rm -rf'
        std::filesystem::remove_all(base_path);
        std::cout << "Container " << container_name << " deleted successfully." << std::endl;
    }
}

#endif