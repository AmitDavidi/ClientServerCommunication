#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <Windows.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#pragma comment(lib, "ws2_32.lib")

#define JOB_FAILED -1
#define PUSH_TO_FIFO_FAILED 0

typedef struct {
    int *data;
    int head;
    int tail;
    int count;
    int responses_to_send;
    CRITICAL_SECTION lock;
    HANDLE job_available;
    HANDLE space_available;
} Fifo;

// globals
Fifo* fifo;
int receiving_data = 1;
clock_t start_time;
FILE *logfile;
int job_counter = 0;
int MAX_FIFO_SIZE;
double mu;


/* stantard initialization of multithread protected fifo */
void init_fifo(int QSize) {

    fifo = (Fifo*)malloc(sizeof(Fifo));
    fifo->data = (int*)calloc(QSize, sizeof(int));
    MAX_FIFO_SIZE = QSize;

    if (fifo == NULL) {
        printf("Memory allocation failed\n.");
        exit(1);
    }
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
    InitializeCriticalSection(&fifo->lock);
    fifo->job_available = CreateEvent(NULL, FALSE, FALSE, NULL);
    fifo->space_available = CreateEvent(NULL, FALSE, FALSE, NULL);
}

/* stantard multithread protected push to  fifo */
int push(int data) {
    EnterCriticalSection(&fifo->lock);
    // add to FIFO only if the fifo isn't full
    if(fifo->count < MAX_FIFO_SIZE) {
        fifo->data[fifo->tail] = data;
        fifo->tail = (fifo->tail + 1) % MAX_FIFO_SIZE;
        fifo->count = fifo->count + 1;

        fprintf(logfile, "%f %d\n", (double)(clock() - start_time) / CLOCKS_PER_SEC, fifo->count);

        SetEvent(fifo->job_available);
        LeaveCriticalSection(&fifo->lock);
        return 1; // fifo push successfull 
    }
    else {
        LeaveCriticalSection(&fifo->lock);
        return 0; // fifo push unsuccessfull
    }
}

/* standard multithread protected pop from  fifo */ 
int pop() {
    EnterCriticalSection(&fifo->lock);
    
    while (fifo->count == 0) {
        //no jobs available. wait for jobs to be added.
        LeaveCriticalSection(&fifo->lock);
        WaitForSingleObject(fifo->job_available, INFINITE);
        EnterCriticalSection(&fifo->lock);
    }
    int data = fifo->data[fifo->head];
    fifo->head = (fifo->head + 1) % MAX_FIFO_SIZE;
    fifo->count--;

    fprintf(logfile, "%f %d\n", (double)(clock() - start_time) / CLOCKS_PER_SEC, fifo->count);

    SetEvent(fifo->space_available);
    LeaveCriticalSection(&fifo->lock);
    return data;
}

/* RNG based job handling */
void performJob(int num) {
    // printf("Performing job %d\n", num);
    double t = -1.0 * log(1.0 - ((double)rand() / RAND_MAX)) / mu;
    Sleep(t * 1000);
}

DWORD WINAPI worker_thread(LPVOID arg) {
    
    SOCKET sockfd = *(SOCKET*)arg;
    int response;
    int result;
    int job;
    while (receiving_data || fifo->count > 0) {

        job = pop();
        performJob(job);
        response = job; // send back the number of job that is complete

        // send response to client
        result = send(sockfd, (const char*)&response, sizeof(response), 0);
        if (result == SOCKET_ERROR) {
            printf("--Thread-- send failed with error: %d\n", WSAGetLastError());
            closesocket(sockfd);
            return 1;
        }
    }
}



int main(int argc, char** argv) {
    if (argc < 6) {
        printf("Usage: %s <port> <seed> <run_id> <mu> <QSize>\n", argv[0]);
        return 1;
    }
    int serv_port = atoi(argv[1]);
    int seed = atoi(argv[2]);
    int run_id = atoi(argv[3]);
    mu = atof(argv[4]);
    int QSize = atoi(argv[5]);
    int result;
    srand(seed);

    char filename[50] = { 0 };
    sprintf(filename, "server_%d.log", run_id);
    logfile = fopen(filename, "w");
    fprintf(logfile, "server_%d.log: seed=%d, mu=%f, QSize=%d\n", run_id, seed, mu, QSize);


    // Initialize FIFO
    init_fifo(QSize);

    // init winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // create socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("Error creating socket: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // bind socket to port
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(serv_port);
    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        printf("Bind failed with error: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    int flag = 1;
    if (setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0) {
        printf("setsockopt failed\n");
    }

    // Listen for incoming connections
    if (listen(server_socket, 10) == SOCKET_ERROR) {
        perror("listen failed");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    

    SOCKADDR_IN clientAddress;
    int clientAddressLength = sizeof(clientAddress);
    SOCKET clientConnection = accept(server_socket, (SOCKADDR*)&clientAddress, &clientAddressLength);
    if (clientConnection == INVALID_SOCKET) {
        printf("accept failed with error: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // call thread
    HANDLE worker_thread_handle = CreateThread(NULL, 0, worker_thread, (LPVOID)&clientConnection, 0, NULL);

    int response = 0;
    int integer = 0;
    // Main server loop
    start_time = clock();
    while (true) {

        // Receive data from client
        result = recv(clientConnection, (char*)&integer, sizeof(integer), 0);
        if (result == SOCKET_ERROR) {
            printf("recv failed with error: %d\n", WSAGetLastError());
            break; // exit loop if there was an error receiving data
        }

        if (result == 0) {
            printf("Client called shutdown.\n");
            break; // exit loop if client closed the connection
        }

        if (push(job_counter) == PUSH_TO_FIFO_FAILED) {
            response = JOB_FAILED;
            result = send(clientConnection, (const char*)&response, sizeof(response), 0);
        }
        job_counter++;
    }

    receiving_data = 0;
    /*
    // for debugging
    while (fifo->count > 0) {
        Sleep(1000);
        printf("Jobs left in Queue = %d\n", fifo->count);
    }
    */
    
    if(worker_thread_handle)
        WaitForSingleObject(worker_thread_handle, INFINITE); // wait for the last operations to end
    
    fprintf(stderr, "server_%d.log: seed=%d, mu=%f, QSize=%d\n", run_id, seed, mu, QSize);
    // shut down sockets.
    shutdown(clientConnection, SD_SEND);
    shutdown(server_socket, SD_SEND);

    // Close the sockets
    closesocket(clientConnection);
    closesocket(server_socket);
    
    free(fifo->data);
    free(fifo);

    fclose(logfile);

    // cleanup Winsock
    WSACleanup();

    return 0;
}



