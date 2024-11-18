#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"  // Replace with server's IP if needed
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

int client_socket;
struct sockaddr_in server_addr;

void generate_client_id(char* client_id, size_t size) {
    snprintf(client_id, size, "CLIENT-%d", rand() % 10000);
}

void send_message(const char* message) {
    sendto(client_socket, message, strlen(message), 0, 
           (struct sockaddr*)&server_addr, sizeof(server_addr));
}

int receive_message(char* buffer, size_t size) {
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int n = recvfrom(client_socket, buffer, size - 1, 0, 
                     (struct sockaddr*)&from_addr, &from_len);
    if (n >= 0) {
        buffer[n] = '\0';
    }
    return n;
}

void discover_ip(const char* client_id, int priority_flag) {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "DISCOVER %s %d", client_id, priority_flag);
    send_message(message);

    char response[BUFFER_SIZE];
    if (receive_message(response, BUFFER_SIZE) >= 0) {
        if (strstr(response, "OFFER")) {
            printf("Received IP Offer: %s\n", response + 6);  // Skip "OFFER "
        } else if (strstr(response, "OFFER_FAIL")) {
            printf("No IP available from the server.\n");
        } else {
            printf("Unknown response: %s\n", response);
        }
    }
}

void request_ip(const char* client_id) {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "REQUEST %s", client_id);
    send_message(message);

    char response[BUFFER_SIZE];
    if (receive_message(response, BUFFER_SIZE) >= 0) {
        if (strstr(response, "ACK")) {
            printf("IP Allocation Confirmed: %s\n", response + 4);  // Skip "ACK "
        } else if (strstr(response, "NACK")) {
            printf("IP Allocation Denied.\n");
        } else {
            printf("Unknown response: %s\n", response);
        }
    }
}

void release_ip(const char* client_id) {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "RELEASE %s", client_id);
    send_message(message);

    char response[BUFFER_SIZE];
    if (receive_message(response, BUFFER_SIZE) >= 0) {
        if (strstr(response, "RELEASE_ACK")) {
            printf("IP Release Confirmed for client %s.\n", client_id);
        } else {
            printf("Unknown response: %s\n", response);
        }
    }
}

void client_loop() {
    char client_id[32];
    generate_client_id(client_id, sizeof(client_id));

    printf("Client ID: %s\n", client_id);
    printf("Sending DHCP DISCOVER message...\n");
    discover_ip(client_id, 0);

    printf("Sending DHCP REQUEST message...\n");
    request_ip(client_id);

    printf("Press Enter to release the IP and exit...");
    getchar();

    printf("Sending DHCP RELEASE message...\n");
    release_ip(client_id);
}

int main() {
    srand(time(NULL));

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    printf("DHCP Client started. Communicating with server %s on port %d...\n", 
           SERVER_IP, SERVER_PORT);

    client_loop();

    close(client_socket);
    return 0;
}
