// dhcp_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#define MAX_CLIENTS 10
#define START_IP "192.168.1.100"
#define PORT 8888
#define BUFFER_SIZE 1024

typedef struct {
    char ip_address[16];
    char client_id[32];
    time_t lease_start;
    time_t lease_duration;
    int is_allocated;
    int has_permanent_lease;
    int priority_client;
    int retry_count;
} LeaseInfo;

typedef struct {
    LeaseInfo leases[MAX_CLIENTS];
    pthread_mutex_t mutex;
    time_t last_cleanup;
} LeasePool;

LeasePool lease_pool;
int server_socket;
pthread_t lease_monitor_thread;

void cleanup(int signo) {
    printf("\nCleaning up and shutting down...\n");
    close(server_socket);
    pthread_cancel(lease_monitor_thread);
    pthread_mutex_destroy(&lease_pool.mutex);
    exit(0);
}

void init_lease_pool() {
    pthread_mutex_init(&lease_pool.mutex, NULL);
    lease_pool.last_cleanup = time(NULL);
    
    struct in_addr ip;
    inet_pton(AF_INET, START_IP, &ip);
    uint32_t ip_int = ntohl(ip.s_addr);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&lease_pool.leases[i], 0, sizeof(LeaseInfo));
        ip.s_addr = htonl(ip_int + i);
        inet_ntop(AF_INET, &ip, lease_pool.leases[i].ip_address, 16);
        lease_pool.leases[i].lease_duration = 3600; // Default 1 hour
    }
}

void log_activity(const char* activity, const char* client_id, const char* ip) {
    FILE* log_file = fopen("dhcp_lease.log", "a");
    if (log_file) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] %s: Client=%s, IP=%s\n", 
                timestamp, activity, client_id, ip ? ip : "N/A");
        fclose(log_file);
    }
}

void* lease_monitor(void* arg) {
    while (1) {
        time_t current_time = time(NULL);
        pthread_mutex_lock(&lease_pool.mutex);
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (lease_pool.leases[i].is_allocated && !lease_pool.leases[i].has_permanent_lease) {
                time_t elapsed = current_time - lease_pool.leases[i].lease_start;
                
                // Warning threshold (80% of lease duration)
                if (elapsed >= (lease_pool.leases[i].lease_duration * 0.8)) {
                    printf("WARNING: Lease for IP %s (Client %s) is about to expire\n",
                           lease_pool.leases[i].ip_address,
                           lease_pool.leases[i].client_id);
                    log_activity("LEASE_WARNING", lease_pool.leases[i].client_id, 
                               lease_pool.leases[i].ip_address);
                }
                
                // Expire lease
                if (elapsed >= lease_pool.leases[i].lease_duration) {
                    printf("Lease expired for IP %s (Client %s)\n",
                           lease_pool.leases[i].ip_address,
                           lease_pool.leases[i].client_id);
                    log_activity("LEASE_EXPIRED", lease_pool.leases[i].client_id, 
                               lease_pool.leases[i].ip_address);
                    lease_pool.leases[i].is_allocated = 0;
                    memset(lease_pool.leases[i].client_id, 0, 32);
                }
            }
        }
        
        pthread_mutex_unlock(&lease_pool.mutex);
        sleep(60); // Check every minute
    }
    return NULL;
}

char* allocate_ip(const char* client_id, int is_priority) {
    pthread_mutex_lock(&lease_pool.mutex);
    time_t current_time = time(NULL);
    
    // First check if client already has an IP
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (lease_pool.leases[i].is_allocated && 
            strcmp(lease_pool.leases[i].client_id, client_id) == 0) {
            // Renew lease with dynamic duration based on retry count
            lease_pool.leases[i].lease_start = current_time;
            int lease_duration = lease_pool.leases[i].retry_count < 3 ? 3600 : 1800; // Shorter lease for retrying clients
            lease_pool.leases[i].lease_duration = lease_duration;
            char* ip = lease_pool.leases[i].ip_address;
            log_activity("LEASE_RENEWED", client_id, ip);
            pthread_mutex_unlock(&lease_pool.mutex);
            return ip;
        }
    }
    
    // For priority clients, look for the first available IP
    if (is_priority) {
        for (int i = 0; i < MAX_CLIENTS/2; i++) {  // Reserve first half for priority clients
            if (!lease_pool.leases[i].is_allocated) {
                lease_pool.leases[i].is_allocated = 1;
                lease_pool.leases[i].priority_client = 1;
                lease_pool.leases[i].lease_start = current_time;
                strncpy(lease_pool.leases[i].client_id, client_id, 31);
                lease_pool.leases[i].lease_duration = 7200; // Longer lease for priority clients
                char* ip = lease_pool.leases[i].ip_address;
                printf("Allocated priority IP %s to client %s\n", ip, client_id);
                log_activity("IP_ALLOCATED_PRIORITY", client_id, ip);
                pthread_mutex_unlock(&lease_pool.mutex);
                return ip;
            }
        }
    }
    
    // For non-priority clients or priority clients when priority pool is full
    int start_index = is_priority ? 0 : MAX_CLIENTS/2;  // Priority clients can use any IP if needed
    for (int i = start_index; i < MAX_CLIENTS; i++) {
        if (!lease_pool.leases[i].is_allocated) {
            lease_pool.leases[i].is_allocated = 1;
            lease_pool.leases[i].priority_client = is_priority;
            lease_pool.leases[i].lease_start = current_time;
            strncpy(lease_pool.leases[i].client_id, client_id, 31);
            // Dynamic lease duration based on retry count
            lease_pool.leases[i].lease_duration = lease_pool.leases[i].retry_count < 3 ? 3600 : 1800; // Shorter lease for retrying clients
            char* ip = lease_pool.leases[i].ip_address;
            printf("Allocated IP %s to client %s\n", ip, client_id);
            log_activity("IP_ALLOCATED_REGULAR", client_id, ip);
            pthread_mutex_unlock(&lease_pool.mutex);
            return ip;
        }
    }
    
    printf("No available IPs for client %s\n", client_id);
    log_activity("IP_ALLOCATION_FAILED", client_id, NULL);
    pthread_mutex_unlock(&lease_pool.mutex);
    return NULL;
}


void release_ip(const char* client_id) {
    pthread_mutex_lock(&lease_pool.mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (lease_pool.leases[i].is_allocated && 
            strcmp(lease_pool.leases[i].client_id, client_id) == 0) {
            log_activity("IP_RELEASED", client_id, lease_pool.leases[i].ip_address);
            lease_pool.leases[i].is_allocated = 0;
            memset(lease_pool.leases[i].client_id, 0, 32);
            break;
        }
    }
    
    pthread_mutex_unlock(&lease_pool.mutex);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    
    // Initialize lease pool
    init_lease_pool();
    
    // Set up signal handler
    signal(SIGINT, cleanup);
    
    // Create lease monitor thread
    if (pthread_create(&lease_monitor_thread, NULL, lease_monitor, NULL) != 0) {
        perror("Failed to create lease monitor thread");
        exit(1);
    }
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    printf("DHCP Server started. Listening on port %d...\n", PORT);
    printf("Press Ctrl+C to shutdown gracefully\n");
    
    while (1) {
        socklen_t client_len = sizeof(client_addr);
        int n = recvfrom(server_socket, buffer, BUFFER_SIZE, 0,
                        (struct sockaddr*)&client_addr, &client_len);
        buffer[n] = '\0';
        
        if (strncmp(buffer, "DISCOVER", 8) == 0) {
            // Extract client ID and priority flag
            char client_id[32];
            int is_priority = 0;
            sscanf(buffer + 9, "%s %d", client_id, &is_priority);
            
            char* assigned_ip = allocate_ip(client_id, is_priority);
            if (assigned_ip) {
                char response[BUFFER_SIZE];
                sprintf(response, "OFFER %s %ld", assigned_ip, 
                        lease_pool.leases[0].lease_duration);
                
                // Send OFFER message to client
                sendto(server_socket, response, strlen(response), 0,
                       (struct sockaddr*)&client_addr, client_len);
                printf("Assigned IP %s to client %s (Priority: %s)\n", 
                       assigned_ip, client_id, is_priority ? "Yes" : "No");

                // Wait for the client to send a REQUEST message
                n = recvfrom(server_socket, buffer, BUFFER_SIZE, 0,
                             (struct sockaddr*)&client_addr, &client_len);
                buffer[n] = '\0';
                
                if (strncmp(buffer, "REQUEST", 7) == 0) {
                    // Send ACK message to client with assigned IP
                    char ack_response[BUFFER_SIZE];
                    sprintf(ack_response, "ACK %s", assigned_ip);
                    sendto(server_socket, ack_response, strlen(ack_response), 0,
                           (struct sockaddr*)&client_addr, client_len);
                    printf("Sent ACK for IP %s to client %s\n", assigned_ip, client_id);
                }
            }
        } else if (strncmp(buffer, "RELEASE", 7) == 0) {
            char client_id[32];
            sscanf(buffer + 8, "%s", client_id);
            release_ip(client_id);
            printf("Released IP for client %s\n", client_id);
        }
    }
    
    return 0;
}
