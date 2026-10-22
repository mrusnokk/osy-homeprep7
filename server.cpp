#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctime>
#include <csignal>
#include <sys/wait.h>
#include <cerrno>

int zoo_map[10][10];

void send_area(int cl, int cx, int cy)
{
    char area_str[64] = "";
    char tmp[16];
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            int nx = cx + dx;
            int ny = cy + dy;
            int val = 0;
            if (nx >= 0 && nx < 10 && ny >= 0 && ny < 10)
            {
                val = zoo_map[ny][nx];
            }
            if (dy == 1 && dx == 1)
                snprintf(tmp, sizeof(tmp), "%d", val);
            else
                snprintf(tmp, sizeof(tmp), "%d,", val);
            strcat(area_str, tmp);
        }
    }
    char response[128];
    snprintf(response, sizeof(response), "POS %d %d AREA %s\n", cx, cy, area_str);
    write(cl, response, strlen(response));
}

void serve_player(int cl)
{
    char buffer[1024];
    char name[64] = "";
    bool registered = false;

    srand(time(nullptr) ^ getpid());
    int x = rand() % 10;
    int y = rand() % 10;

    pollfd pfd{};
    pfd.fd = cl;
    pfd.events = POLLIN;

    while (1)
    {
        int pr = poll(&pfd, 1, -1);
        if (pr < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        ssize_t bytes = read(cl, buffer, sizeof(buffer) - 1);
        if (bytes <= 0)
            break;

        buffer[bytes] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        if (strncmp(buffer, "HELLO ", 6) == 0)
        {
            if (registered)
            {
                const char *msg = "ERROR Already registered\n";
                write(cl, msg, strlen(msg));
            }
            else
            {
                snprintf(name, sizeof(name), "%s", buffer + 6);
                registered = true;

                char welcome_msg[128];
                snprintf(welcome_msg, sizeof(welcome_msg), "WELCOME %s\n", name);
                write(cl, welcome_msg, strlen(welcome_msg));

                send_area(cl, x, y);
            }
        }
        else if (strcmp(buffer, "WHERE") == 0)
        {
            if (!registered)
            {
                const char *msg = "ERROR Please register first\n";
                write(cl, msg, strlen(msg));
            }
            else
            {
                send_area(cl, x, y);
            }
        }
        else if (strncmp(buffer, "MOVE ", 5) == 0)
        {
            if (!registered)
            {
                const char *msg = "ERROR Please register first\n";
                write(cl, msg, strlen(msg));
            }
            else
            {
                int nx = x, ny = y;
                bool valid = true;
                const char *dir = buffer + 5;
                if (strcmp(dir, "NORTH") == 0)
                    ny--;
                else if (strcmp(dir, "SOUTH") == 0)
                    ny++;
                else if (strcmp(dir, "WEST") == 0)
                    nx--;
                else if (strcmp(dir, "EAST") == 0)
                    nx++;

                else
                {
                    valid = false;
                }
                if (!valid)
                {
                    const char *msg = "ERROR Invalid direction!\n";
                    write(cl, msg, strlen(msg));
                }
                else if (nx < 0 || nx > 9 || ny < 0 || ny > 9)
                {
                    const char *msg = "ERROR Out of bounds\n";
                    write(cl, msg, strlen(msg));
                }
                else
                {
                    x = nx;
                    y = ny;
                    send_area(cl, x, y);
                }
            }
        }
        else if (strncmp(buffer, "INFO ", 5) == 0)
        {
            if (!registered)
            {
                const char *msg = "ERROR Please register first\n";
                write(cl, msg, strlen(msg));
            }
            else
            {
                pid_t pol = fork();
                if (pol == 0)
                {
                    dup2(cl, STDOUT_FILENO);
                    close(cl);
                    execlp("./zoo_info", "zoo_info", buffer + 5, nullptr);
                    _exit(127);
                }
                else
                {
                    waitpid(pol, nullptr, 0);
                }
            }
        }
        else if (strcmp(buffer, "QUIT") == 0)
        {
            const char *msg = "BYE\n";
            write(cl, msg, strlen(msg));
            break;
        }
        else
        {
            const char *msg = "ERROR Unknown command\n";
            write(cl, msg, strlen(msg));
        }
    }
    close(cl);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return -1;
    }

    int optind_arg = 1;
    if (strcmp(argv[1], "-d") == 0)
    {
        if (argc < 3)
            return -1;
        optind_arg = 2;
    }
    int PORT = atoi(argv[optind_arg]);

    srand(time(nullptr));

    signal(SIGCHLD, SIG_IGN);

    for (int y = 0; y < 10; y++)
    {
        for (int x = 0; x < 10; x++)
        {
            zoo_map[y][x] = (rand() % 9) + 1;
        }
    }

    int soc = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in srv_addr{};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(PORT);
    srv_addr.sin_addr.s_addr = INADDR_ANY;

    int opt_val = 1;
    setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

    if (bind(soc, (sockaddr *)&srv_addr, sizeof(srv_addr)) < 0)
        exit(EXIT_FAILURE);
    if (listen(soc, 5) < 0)
        exit(EXIT_FAILURE);

    while (1)
    {
        int cl = accept(soc, nullptr, nullptr);
        if (cl < 0)
            continue;

        pid_t p = fork();
        if (p < 0)
        {
            close(cl);
            continue;
        }

        if (p == 0)
        {
            close(soc);
            signal(SIGCHLD, SIG_DFL);
            serve_player(cl);
            _exit(EXIT_SUCCESS);
        }
        else
        {
            close(cl);
        }
    }
    return 0;
}