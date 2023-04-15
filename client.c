#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#pragma comment(lib, "ws2_32.lib")

#define JOB_FAILED -1

typedef struct {
    double gen_time;
    double end_time;
    double total_time;

} job_information;

clock_t start_time;
job_information all_jobs_list[100000];
int jobs_sent = 0;
int jobs_completed = 0;
int total_drops = 0;
int total_pkts = 0;

// Handles responses coming in from the server, 
DWORD WINAPI ResponseThread(LPVOID lpParam)
{
    SOCKET client_socket = *(SOCKET*)lpParam;
    int response = 0, response_count = 0, bytes_received = 0, result;

    while (true) {
        // read from socket
        result = recv(client_socket, (char*)&response, sizeof(response), 0);
        if (result == SOCKET_ERROR) {
            printf("--Thread-- recv failed with error: %d\n", WSAGetLastError());
            break;
        }
        if (result == 0) {
            //printf("--Thread-- Server disconnected.\n");
            break;
        }
        
        // keep track of times, for the logger
        if (response != JOB_FAILED) {
            all_jobs_list[response].end_time = (double)(clock() - start_time) / (CLOCKS_PER_SEC );
            all_jobs_list[response].total_time = all_jobs_list[response].end_time - all_jobs_list[response].gen_time;
            //printf("Job %d completed in Time %f\n", response, all_jobs_list[response].end_time);

        }
        else {
            all_jobs_list[response].end_time = 0;
            all_jobs_list[response].total_time = 0;
            total_drops++;
        }

        jobs_completed++;

    }
    return 0;
}


void logger(int run_id, int seed, double lambda, double T) {
    char filename[50] = { 0 };
    sprintf(filename, "client_%d.log", run_id);
    FILE* logfile = fopen(filename, "w");
    fprintf(logfile, "client_%d.log: seed = %d, lambda = %f, T = %f, total_ pkts = %d, total_drops = %d\n", run_id, seed, lambda, T, total_pkts, total_drops);

    for (int i = 0; i < jobs_sent; i++) {
        fprintf(logfile, "%d %f %f %f\n", i, all_jobs_list[i].gen_time, all_jobs_list[i].end_time, all_jobs_list[i].total_time);
    }

    fclose(logfile);
}


int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <server ip address> <port> <seed> <run_id> <Lambda> <T>\n", argv[0]);
        return 1;
    }
    
    char *server_ip_address = argv[1];
    int port_number = atoi(argv[2]);
    int seed = atoi(argv[3]);
    int run_id = atoi(argv[4]);
    double Lambda = atof(argv[5]);
    double T = atof(argv[6]);

    srand(seed);

    // initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
    
    // create a TCP/IP socket
    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET) {
        printf("socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int flag = 1;
    // use TCP_NODELAY
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0) {
        printf("setsockopt failed\n");
    }
    
    // connect to the server
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port_number); // argv[2] = server port
    inet_pton(AF_INET, server_ip_address, &servaddr.sin_addr); // argv[1] = serv_ip
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == SOCKET_ERROR) {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

     // create a response thread
    HANDLE response_thread = CreateThread(NULL, 0, ResponseThread, (LPVOID)&sockfd, 0, NULL);
    

    double t = 0.0;  // current time in ms
    double elapsed_time = 0.0;
    double next_job;
    int job = 1;
    int result;
    start_time = clock(); // get start time

    // main loop
    while (elapsed_time < T) {
        next_job = -1.0 * log(1.0 - ((double)rand() / RAND_MAX)) / Lambda;  // x = -ln( 1- U{0,1} ) / Lambda. | F(x) = 1 - e**(-Lambda * x)
        Sleep(next_job * 1000);  // sleep for the inter-arrival time in milliseconds
        
        elapsed_time = (double)(clock() - start_time) / CLOCKS_PER_SEC; // update time


        result = send(sockfd, (const char*)&job, sizeof(job), 0);
        if (result == SOCKET_ERROR) {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket(sockfd);
            return 1;
        }
        //printf("Job number %d sent in %f\n", jobs_sent, elapsed_time);
        all_jobs_list[jobs_sent++].gen_time = elapsed_time; // keep track of gen_time
        
        total_pkts++;

    }
    shutdown(sockfd, SD_SEND); // close the socket for writing.
   
   // for debugging
    /*
    printf("finished generating jobs at %f\n", elapsed_time);
    while (jobs_completed < jobs_sent) { 
        Sleep(1000);
        printf("Responses received = %d/%d\n", jobs_completed, jobs_sent);
    }
    */

     // wait for all responses from the server
    if(response_thread)
        WaitForSingleObject(response_thread, INFINITE);
    
    logger(run_id, seed, Lambda, T);
    // close the socket
    closesocket(sockfd);
    
    // cleanup Winsock
    WSACleanup();
    
    return 0;
}
