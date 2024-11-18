// dhcp_client2.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char client_id[] = "CLIENT2";
    
    // Create socket
    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    // Send DISCOVER message
    char discover_msg[BUFFER_SIZE];
    sprintf(discover_msg, "DISCOVER %s", client_id);
    sendto(client_socket, discover_msg, strlen(discover_msg), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    printf("Sent DISCOVER message\n");
    
    // Receive OFFER message
    socklen_t server_len = sizeof(server_addr);
    int n = recvfrom(client_socket, buffer, BUFFER_SIZE, 0,
                     (struct sockaddr*)&server_addr, &server_len);
    buffer[n] = '\0';
    
    if (strncmp(buffer, "OFFER", 5) == 0) {
        printf("Received IP address: %s\n", buffer + 6);
    }
    
    // Keep connection alive for a while
    sleep(15);
    
    // Release IP
    char release_msg[BUFFER_SIZE];
    sprintf(release_msg, "RELEASE %s", client_id);
    sendto(client_socket, release_msg, strlen(release_msg), 0,
           (struct sockaddr*)&server_addr, sizeof(server_addr));
    printf("Released IP address\n");
    
    close(client_socket);
    return 0;
}