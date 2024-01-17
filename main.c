// required for POLLRDHUP option
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "requests.h"

#define BACKLOG 10
#define START_CAPACITY 5
#define SEND_INTERVAL 86400 // (60 * 60 * 24) seconds
#define HOURS 24
#define BUFFER_LEN 200

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int get_server_socket(struct addrinfo *availables)
{
    int serv_sock;
    int rc;

    struct addrinfo *ptr;
    for (ptr = availables; ptr != NULL; ptr = ptr->ai_next)
    {
        serv_sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (serv_sock == -1)
        {
            perror("server socket()");
            continue;
        }

        const int yes = 1;
        rc = setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (rc == -1)
        {
            perror("server setsockopt()");
            close(serv_sock);
            continue;
        }

        rc = bind(serv_sock, ptr->ai_addr, ptr->ai_addrlen);
        if (rc == -1)
        {
            perror("server bind()");
            close(serv_sock);
            continue;
        }

        // if all successful, no more iterations needed
        break;
    }

    if (ptr == NULL)
    {
        (void)fprintf(stderr, "server failed to bind\n");
        return -1;
    }
    return serv_sock;
}

void add_client_to_pfds(int fd, struct pollfd **pfds, int *pfds_size, int *pfds_capacity)
{
    if (*pfds_size == *pfds_capacity)
    {
        *pfds_capacity *= 2;
        *pfds = realloc(*pfds, *pfds_capacity * sizeof **pfds);
    }

    (*pfds)[*pfds_size].fd = fd;
    (*pfds)[*pfds_size].events = POLLRDHUP;
    (*pfds)[*pfds_size].revents = 0;

    ++(*pfds_size);
}

void remove_fd_from_pfds(int fd, struct pollfd *pfds, int *pfds_size)
{
    for (int i = 0; i < *pfds_size; ++i)
    {
        if (pfds[i].fd == fd)
        {
            pfds[i] = pfds[*pfds_size - 1];
            break;
        }
    }
    --(*pfds_size);
}

struct Conn
{
    int socket;
    char ip[INET6_ADDRSTRLEN];
    double latitude;
    double longitude;
};

void add_conn_to_conns(int fd, char *ip, struct Conn **conns, int *conns_size, int *conns_capacity, double latitude,
                       double longitude)
{
    if (*conns_size == *conns_capacity)
    {
        *conns_capacity *= 2;
        *conns = realloc(*conns, *conns_capacity * sizeof **conns);
    }

    (*conns)[*conns_size].socket = fd;
    strcpy((*conns)[*conns_size].ip, ip);
    (*conns)[*conns_size].latitude = latitude;
    (*conns)[*conns_size].longitude = longitude;
    ++(*conns_size);
}

void remove_conn_from_conns(int fd, struct Conn *conns, int *conns_size)
{
    for (int i = 0; i < *conns_size; ++i)
    {
        if (conns[i].socket == fd)
        {
            conns[i] = conns[*conns_size - 1];
            break;
        }
    }
    --(*conns_size);
}

int sendall(int s, const char *buf, int *len)
{
    int total = 0;
    int bytesleft = *len;
    int n = -1;

    while (total < *len)
    {
        n = (int)send(s, buf + total, bytesleft, 0);
        if (n == -1)
        {
            break;
        }
        total += n;
        bytesleft -= n;
    }
    *len = total;

    return n == -1 ? -1 : 0;
}

const char *precipitation_formated(int probability)
{
    if (probability <= 10)
    {
        return "No precipitation";
    }
    if (probability <= 35)
    {
        return "Might be snow or rain";
    }
    if (probability <= 80)
    {
        return "Likely will be snow or rain";
    }
    if (probability <= 100)
    {
        return "Very likely will be snow or rain";
    }
    return "Invalid precipitation probability value";
}

const char *cloudy_formated(int coverage)
{
    if (coverage <= 20)
    {
        return "Clear sky";
    }
    if (coverage <= 60)
    {
        return "Partly cloudy";
    }
    if (coverage <= 100)
    {
        return "Cloudy";
    }
    return "Invalid cloudy coverage value";
}

int send_forecast(int sock, double latitude, double longitude)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        (void)fprintf(stderr, "curl_easy_init() failed\n");
        goto end;
    }

    double temperature[HOURS];
    int humidity[HOURS];
    double wind_speed[HOURS];
    int precipitation[HOURS];
    int cloud_cover[HOURS];
    int rc =
        get_forecast(curl, latitude, longitude, temperature, humidity, wind_speed, precipitation, cloud_cover, HOURS);
    if (rc != 0)
    {
        goto end;
    }

    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);

    static const char *day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

    int day_of_week = time_info->tm_wday;
    const char *current_day = day_names[day_of_week];

    char send_str[BUFFER_LEN];
    (void)snprintf(send_str, BUFFER_LEN, "Forecast for %s:\n", current_day);
    int len = (int)strlen(send_str);
    if (sendall(sock, send_str, &len) < 0)
    {
        perror("sendall()");
        goto end;
    }

    for (int i = 0; i < HOURS; ++i)
    {
        (void)snprintf(send_str, BUFFER_LEN, "%02d:00: Temperature %dC, Humididty %d%%, Wind %.1lfkm/h, %s, %s\n", i,
                       (int)temperature[i], humidity[i], wind_speed[i], precipitation_formated(precipitation[i]),
                       cloudy_formated(cloud_cover[i]));
        len = (int)strlen(send_str);
        if (sendall(sock, send_str, &len) < 0)
        {
            perror("sendall()");
            goto end;
        }
    }
end:
    curl_easy_cleanup(curl);

    return 0;
}

struct SenderThreadData
{
    pthread_mutex_t *mutex;
    struct Conn *conns;
    int *conns_size;
};

void *sender_thread(void *vargp)
{
    struct SenderThreadData *data = vargp;
    pthread_mutex_t *mutex_ptr = data->mutex;
    while (1)
    {
        pthread_mutex_lock(mutex_ptr);
        int conns_size = *data->conns_size;
        pthread_mutex_unlock(mutex_ptr);
        for (int i = 0; i < conns_size; ++i)
        {
            pthread_mutex_lock(mutex_ptr);
            int sock = data->conns[i].socket;
            double latitude = data->conns[i].latitude;
            double longitude = data->conns[i].longitude;
            pthread_mutex_unlock(mutex_ptr);
            send_forecast(sock, latitude, longitude);
        }

        // probably can be more precise but will work for now
        time_t sleep_time;
        sleep_time = time(NULL);
        sleep_time %= SEND_INTERVAL;

        sleep(SEND_INTERVAL - sleep_time);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    const char *port;

    if (argc != 2)
    {
        (void)fprintf(stderr, "Usage: wthr port\n");
        return -1;
    }
    port = argv[1];

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        (void)fprintf(stderr, "curl_easy_init() failed\n");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // don't care ipv4 or ipv6
    hints.ai_socktype = SOCK_STREAM; // tcp
    hints.ai_flags = AI_PASSIVE;     // this machine's ip

    int rc;

    rc = getaddrinfo(NULL, port, &hints, &res);

    if (rc != 0)
    {
        (void)fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(rc));
        return -1;
    }

    int serv_sock = get_server_socket(res);
    if (serv_sock == -1)
    {
        (void)printf("failed to initialize server socket\n");
        return -1;
    }
    freeaddrinfo(res);

    rc = listen(serv_sock, BACKLOG);
    if (rc == -1)
    {
        perror("listen()");
        return -1;
    }

    printf("Server is waiting for connections\n");

    struct sockaddr_storage client_addr = {};
    socklen_t addr_size;
    int client_sock;
    char ip_str[INET6_ADDRSTRLEN];

    int pfds_capacity = START_CAPACITY;
    int pfds_size = 0;
    struct pollfd *pfds = malloc(pfds_capacity * sizeof *pfds);
    pfds[0].fd = serv_sock;
    pfds[0].events = POLLIN;
    ++pfds_size;

    int conns_capacity = START_CAPACITY;
    int conns_size = 0;
    struct Conn *conns = malloc(conns_capacity * sizeof *conns);

    pthread_mutex_t sender_mutex;
    struct SenderThreadData sender_thread_data = {
        .mutex = &sender_mutex,
        .conns = conns,
        .conns_size = &conns_size,
    };
    pthread_t sender_pthread;
    if (pthread_create(&sender_pthread, NULL, sender_thread, &sender_thread_data) != 0)
    {
        perror("pthread_create()");
        goto end;
    }

    for (;;)
    {
        int poll_count = poll(pfds, pfds_size, -1);
        if (poll_count == -1)
        {
            perror("poll()");
            goto end;
        }
        for (int i = 0; i < pfds_size; ++i)
        {
            // socket hangup
            if (pfds[i].revents & POLLRDHUP)
            {
                (void)printf("Closed connection with %s\n", conns[i - 1].ip);

                pthread_mutex_lock(&sender_mutex);
                close(pfds[i].fd);
                remove_conn_from_conns(pfds[i].fd, conns, &conns_size);
                pthread_mutex_unlock(&sender_mutex);

                remove_fd_from_pfds(pfds[i].fd, pfds, &pfds_size);
            }
            // new socket coming in
            else if (pfds[i].fd == serv_sock && pfds[i].revents & POLLIN)
            {
                addr_size = sizeof client_addr;
                client_sock = accept(serv_sock, (struct sockaddr *)&client_addr, &addr_size);
                if (client_sock == -1)
                {
                    perror("accept()");
                    continue;
                }
                inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), ip_str, sizeof ip_str);

                add_client_to_pfds(client_sock, &pfds, &pfds_size, &pfds_capacity);
                double latitude;
                double longitude;
                if (get_geolocation(curl, ip_str, &latitude, &longitude) == -1)
                {
                    const char *send_str = "Couldn't retreive geolocation data\n";
                    int len = (int)strlen(send_str);
                    sendall(client_sock, send_str, &len);
                    (void)fprintf(stderr, "Couldn't retreive geolocation of new client\n");
                    close(client_sock);
                    continue;
                }

                (void)printf("Started connection with %s\n", ip_str);

                pthread_mutex_lock(&sender_mutex);
                add_conn_to_conns(client_sock, ip_str, &conns, &conns_size, &conns_capacity, latitude, longitude);
                pthread_mutex_unlock(&sender_mutex);
            }
        }
    }
end:
    // pthread_cansel wakes up thread from sleep before joining it
    pthread_cancel(sender_pthread);
    pthread_join(sender_pthread, NULL);

    for (int i = 0; i < conns_size; ++i)
    {
        close(conns[i].socket);
    }

    free(conns);
    free(pfds);

    close(serv_sock);
    curl_easy_cleanup(curl);

    return 0;
}
