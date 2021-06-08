#include "../headers/server.h"

int serverEndFlag = 0;
int serverNumConnections = 0;
atomic_int serverNumMessages = 0;
atomic_long serverTaskId = 0;
pthread_mutex_t serverSocketLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t serverBufferLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serverWriterCV = PTHREAD_COND_INITIALIZER;
pthread_cond_t serverReaderCV = PTHREAD_COND_INITIALIZER;
TaskPackage serverBuffer;
ListItem serverListRoot;

struct WriterThreadData{
    int fd;
    char user[16];
    int endFlag;
};

void* serverWriterThread(void * voidParams){
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct WriterThreadData* threadData = (struct WriterThreadData*) voidParams;

    while (!serverEndFlag){
        pthread_cond_wait(&serverWriterCV, &lock);
        if(strcmp(threadData->user, serverBuffer.task.owner) == 0 && !threadData->endFlag){
            pthread_mutex_lock(&serverSocketLock);
            write(threadData->fd, &serverBuffer, sizeof(TaskPackage));
            pthread_mutex_unlock(&serverSocketLock);
        }
        serverNumMessages++;
        if(serverNumMessages == serverNumConnections){
            pthread_cond_signal(&serverReaderCV);
        }
    }
    return NULL;
}

void* serverReaderThread(void * voidParams){
    int sockfd = *(int*) voidParams;
    char user[16];
    TaskPackage taskPackage;
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    //handshake
    read(sockfd, &taskPackage, sizeof(TaskPackage));
    strcpy(user, taskPackage.task.owner);

    pthread_mutex_lock(&serverBufferLock);
    //send all user's tasks
    ListItem* currentItem = serverListRoot.next;
    while (currentItem != NULL){
        if(strcmp(currentItem->task.owner, user) == 0){
            taskPackage.task = currentItem->task;
            taskPackage.type = REQUEST_TYPE_ADD;
            pthread_mutex_lock(&serverSocketLock);
            write(sockfd, &taskPackage, sizeof(TaskPackage));
            pthread_mutex_unlock(&serverSocketLock);
        }
        currentItem = currentItem->next;
    }

    serverNumConnections++;
    printf("Server accepted the client. Username is %s. Now %i connections\n", user, serverNumConnections);
    pthread_t writer;
    struct WriterThreadData data;
    data.fd = sockfd;
    data.endFlag = 0;
    strcpy(data.user, user);
    pthread_create(&writer, NULL, serverWriterThread, &data);
    pthread_mutex_unlock(&serverBufferLock);

    while(recv(sockfd, &taskPackage, sizeof(TaskPackage), 0) > 0) {
        printf("From client:\n\t Title : %s", taskPackage.task.title);
        printf("\t Description: %s", taskPackage.task.description);
        struct tm tm = *localtime(&taskPackage.task.creation_time);
        printf("\t Created: %d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        strcpy(taskPackage.task.owner, user);
        if(taskPackage.type == REQUEST_TYPE_ADD){
            taskPackage.task.creation_time = time(NULL);
            taskPackage.task.id = serverTaskId++;
        }
        pthread_mutex_lock(&serverBufferLock);
        processRequest(taskPackage, &serverListRoot);
        serverNumMessages = 0;
        serverBuffer = taskPackage;
        pthread_cond_broadcast(&serverWriterCV);
        pthread_cond_wait(&serverReaderCV, &lock);
        pthread_mutex_unlock(&serverBufferLock);
    }
    data.endFlag = 1;
    pthread_mutex_lock(&serverBufferLock);
    serverNumConnections--;
    printf("User %s disconnected. Now %i connections\n", user, serverNumConnections);
    pthread_cancel(writer);
    pthread_mutex_unlock(&serverBufferLock);
    return NULL;
}

void* listenerThread(void * voidParams){
    int sock_desc, conn_desc, length;
    struct sockaddr_in servaddr, cli;
    pthread_t clientReader;

    serverListRoot.next = NULL;

    // socket create and verification
    sock_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_desc == -1) {
        printf("Socket can't be created(listenerThread())\n");
        return NULL;
    }
    else
        printf("Socket created...\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    // Binding newly created socket to given IP and verification
    if ((bind(sock_desc, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("Socket can't be binded\n");
        return NULL;
    }
    else
        printf("Socket binded...\n");

    // Now server is ready to listen and verification
    if ((listen(sock_desc, 5)) != 0) {
        printf("Can't listen the socket with port: 25080");
        return NULL;
    }
    else
        printf("Server listening...\n");
    length = sizeof(cli);

    while (!serverEndFlag){
        conn_desc = accept(sock_desc, (SA*)&cli, &length);
        if (conn_desc < 0) {
            printf("Server can't accept a connection\n");
            return NULL;
        }
        else{
            pthread_create(&clientReader, NULL, serverReaderThread, (void *) &conn_desc);
        }
    }

    close(sock_desc);
    return NULL;
}

int serverMode(){
    pthread_t listener;
    pthread_create(&listener, NULL, listenerThread, NULL);
    while (!serverEndFlag){
        char c = getchar();
        if(c == 'Q'){
            serverEndFlag = 1;
        }
    }
    return 0;
}