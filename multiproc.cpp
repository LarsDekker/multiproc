#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <curses.h>
#include <stdexcept>
#include <fcntl.h>
#include <array>
#include <deque>
#include <fstream>
#include <sstream>
#include <regex>

struct CommandConfig {
    int processes;
    std::string command;
};

class ProcessManager {
public:
    struct ProcessInfo {
        pid_t pid;
        int pipe_fd[2];
        std::string command;
        std::deque<std::string> output_lines;
        bool has_exited = false;
        int exit_status = 0;
        static const size_t MAX_LINES = 10;
    };

    ProcessManager(int maxProcesses) 
        : maxProcesses(maxProcesses), currentProcessIndex(0) {}

    void addProcess(const std::string& command) {
        if (commands.size() >= static_cast<size_t>(maxProcesses)) {
            throw std::runtime_error("Maximum number of processes exceeded");
        }
        commands.push_back(command);
    }

    void startProcesses() {
        for (const auto& command : commands) {
            ProcessInfo process_info;
            if (pipe(process_info.pipe_fd) == -1) {
                throw std::runtime_error("Failed to create pipe");
            }

            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                close(process_info.pipe_fd[0]);
                dup2(process_info.pipe_fd[1], STDOUT_FILENO);
                dup2(process_info.pipe_fd[1], STDERR_FILENO);
                close(process_info.pipe_fd[1]);

                execlp("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                // Parent process
                close(process_info.pipe_fd[1]);
                fcntl(process_info.pipe_fd[0], F_SETFL, O_NONBLOCK);
                process_info.pid = pid;
                process_info.command = command;
                processes.push_back(process_info);
            } else {
                throw std::runtime_error("Failed to fork process");
            }
        }
    }

    void readProcessOutput() {
        for (auto& process : processes) {
            char buffer[1024];
            static std::string partial_line;

            ssize_t bytes_read = read(process.pipe_fd[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                std::string new_data(buffer);
                partial_line += new_data;

                size_t pos;
                while ((pos = partial_line.find('\n')) != std::string::npos) {
                    std::string line = partial_line.substr(0, pos);
                    
                    // Get terminal height
                    int maxY, maxX;
                    getmaxyx(stdscr, maxY, maxX);
                    int available_lines = maxY - 8; // Account for header (3), process title (2), margins (2), bottom border (1)
                    
                    process.output_lines.push_back(line);
                    while (process.output_lines.size() > static_cast<size_t>(available_lines)) {
                        process.output_lines.pop_front();
                    }
                    
                    partial_line = partial_line.substr(pos + 1);
                }
            }
        }
    }

    void checkProcessStatus() {
        for (auto& process : processes) {
            if (!process.has_exited) {
                int status;
                pid_t result = waitpid(process.pid, &status, WNOHANG);
                if (result > 0) {
                    process.has_exited = true;
                    if (WIFEXITED(status)) {
                        process.exit_status = WEXITSTATUS(status);
                        std::string exit_msg = "Process exited with status: " + std::to_string(process.exit_status);
                        process.output_lines.push_back("");  // Empty line as separator
                        process.output_lines.push_back(exit_msg);
                    } else if (WIFSIGNALED(status)) {
                        process.exit_status = WTERMSIG(status);
                        std::string exit_msg = "Process terminated by signal: " + std::to_string(process.exit_status);
                        process.output_lines.push_back("");  // Empty line as separator
                        process.output_lines.push_back(exit_msg);
                    }
                }
            }
        }
    }

    void displayOutput() {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);
        start_color();
        timeout(100);


        init_pair(1, COLOR_WHITE, COLOR_BLUE);    // Header
        init_pair(2, COLOR_BLACK, COLOR_WHITE);    // Selected process
        init_pair(3, COLOR_RED, COLOR_BLACK);      // Exit status

        signal(SIGWINCH, nullptr);

        while (true) {
            int maxY, maxX;
            getmaxyx(stdscr, maxY, maxX);
            int available_lines = maxY - 8; // Same calculation as above

            // Read output from all processes
            readProcessOutput();
            checkProcessStatus();

            clear();

            // Draw header
            mvhline(0, 0, ACS_HLINE, maxX);
            attron(COLOR_PAIR(1) | A_BOLD);
            std::string header = " NAVIGATE: UP/DOWN | QUIT: q | KILL CURRENT: k ";
            int headerX = (maxX - header.length()) / 2;
            mvprintw(1, headerX, "%s", header.c_str());
            attroff(COLOR_PAIR(1) | A_BOLD);
            mvhline(2, 0, ACS_HLINE, maxX);

            // Display process information and output
            if (!processes.empty()) {
                const auto& current_process = processes[currentProcessIndex];
                
                // Display process header with status if exited
                attron(COLOR_PAIR(2) | A_BOLD);
                std::string title = "Process " + std::to_string(currentProcessIndex + 1) + "/" + 
                                  std::to_string(processes.size());
                if (current_process.has_exited) {
                    title += " (EXITED)";
                }
                mvprintw(4, 1, "%s", title.c_str());
                attroff(COLOR_PAIR(2) | A_BOLD);

                // Calculate display range
                size_t start_idx = 0;
                if (current_process.output_lines.size() > static_cast<size_t>(available_lines)) {
                    start_idx = current_process.output_lines.size() - available_lines;
                }

                // Display output
                int output_y = 6;
                for (size_t i = start_idx; i < current_process.output_lines.size(); ++i) {
                    if (output_y >= maxY - 1) break;
                    
                    const auto& line = current_process.output_lines[i];
                    
                    // Check if this is an exit status message
                    if (line.find("Process exited with status:") == 0 ||
                        line.find("Process terminated by signal:") == 0) {
                        attron(COLOR_PAIR(3) | A_BOLD);
                        mvprintw(output_y, 2, "%.*s", maxX - 4, line.c_str());
                        attroff(COLOR_PAIR(3) | A_BOLD);
                    } else {
                        mvprintw(output_y, 2, "%.*s", maxX - 4, line.c_str());
                    }
                    
                    output_y++;
                }
            } else {
                mvprintw(4, 1, "No active processes");
            }

            mvhline(maxY - 1, 0, ACS_HLINE, maxX);
            refresh();

            // Handle input
            int key = getch();
            if (key != ERR) {
                switch (key) {
                    case KEY_UP:
                        if (!processes.empty()) {
                            currentProcessIndex = (currentProcessIndex - 1 + processes.size()) % processes.size();
                        }
                        break;
                    case KEY_DOWN:
                        if (!processes.empty()) {
                            currentProcessIndex = (currentProcessIndex + 1) % processes.size();
                        }
                        break;
                    case 'q':
                        terminateAll();
                        break;
                    case 'k':
                        if (!processes.empty()) {
                            terminateCurrent();
                            if (processes.empty()) {
                                endwin();
                                exit(0);
                            }
                        }
                        break;
                    case KEY_RESIZE:
                        endwin();
                        refresh();
                        clear();
                        break;
                }
            }
        }

        endwin();
    }

    void terminateCurrent() {
        if (!processes.empty()) {
            if (!processes[currentProcessIndex].has_exited) {
                kill(processes[currentProcessIndex].pid, SIGTERM);
            }
            close(processes[currentProcessIndex].pipe_fd[0]);
            processes.erase(processes.begin() + currentProcessIndex);
            if (currentProcessIndex >= processes.size() && !processes.empty()) {
                currentProcessIndex = processes.size() - 1;
            }
        }
    }

    void terminateAll() {
        for (auto& process : processes) {
            if (!process.has_exited) {
                kill(process.pid, SIGTERM);
            }
            close(process.pipe_fd[0]);
        }
        endwin();
        exit(0);
    }

    static std::vector<CommandConfig> parseConfigFile(const std::string& filename) {
        std::vector<CommandConfig> configs;
        std::ifstream file(filename);
        std::string line;
        
        if (!file.is_open()) {
            throw std::runtime_error("Could not open config file: " + filename);
        }

        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Find the first non-whitespace character
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) {
                continue;
            }

            // Parse the line
            std::istringstream iss(line);
            int processes;
            std::string separator;
            std::string command;

            if (!(iss >> processes >> separator)) {
                continue;
            }

            // Check for the separator
            if (separator != "|") {
                continue;
            }

            // Get the rest of the line as the command
            std::getline(iss >> std::ws, command);
            if (!command.empty()) {
                configs.push_back({processes, command});
            }
        }

        return configs;
    }

private:
    int maxProcesses;
    std::vector<std::string> commands;
    std::vector<ProcessInfo> processes;
    size_t currentProcessIndex;
};

int main(int argc, char* argv[]) {
    try {
        // Look for .multiproc file in current directory
        std::vector<CommandConfig> configs = ProcessManager::parseConfigFile(".multiproc");
        
        if (configs.empty()) {
            std::cerr << "No valid commands found in .multiproc file\n";
            return 1;
        }

        // Calculate total number of processes
        int totalProcesses = 0;
        for (const auto& config : configs) {
            totalProcesses += config.processes;
        }

        // Create and start processes
        ProcessManager manager(totalProcesses);
        
        for (const auto& config : configs) {
            for (int i = 0; i < config.processes; ++i) {
                manager.addProcess(config.command);
            }
        }

        manager.startProcesses();
        manager.displayOutput();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
