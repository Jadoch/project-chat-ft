/*
** multiclient_server.c -- chat server with multiple channels
This is an implementation of an IRC like chat server that enables clients to chat with “random” people and supports multiple sessions (channels). Server has an admin entity that can issue commands directly to it and control the chatting process.
Author: Artem Gritsenko
Based on the manual from: http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034"   // port we're listening on
#define BUFLEN 1024

struct fdSetList {
    fd_set fdSet_entry;
    int data_usage;
    struct fdSetList *fdSet_next;
}fdSetList;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void PushToFDSet (struct fdSetList **headRef, fd_set value){
    struct fdSetList *newNode;
    newNode = malloc(sizeof(struct fdSetList));
    newNode->fdSet_entry = value;
    newNode->data_usage = 0;
    newNode->fdSet_next = *headRef;
    *headRef = newNode;
}


typedef int bool;
#define true 1
#define false 0

int main(void)
{
    int NUMCONNECTIONS = 100;

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select() for reading the data
    fd_set connected_fds; // file descriptor list for clients wanting to chat
    fd_set wait_for_chat_fds; // file descriptor list for clients wanting to chat
    fd_set flagged_fds; // file descriptor list for flagged clients
    fd_set blocked_fds; // file descriptor list for blocked clients

    struct fdSetList *fdlist, *iter, *prev; // list to maintain the pairs of connected clients

    char list_of_names[64][64] = {0}; // list of names

    int fdmax;        // maximum file descriptor number
    
    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    char outbuf[BUFLEN];
    int nbytes;

    char CHAT_COMMAND[4] = "CHAT";
    char CONNECT_COMMAND[7] = "CONNECT";
    char HELP_COMMAND[4]="HELP";
    char FLAG_COMMAND[4]="FLAG";
    char TRANSFER_COMMAND[8]="TRANSFER";
    char QUIT_COMMAND[4]="QUIT";
    char NAME_COMMAND[4]="NAME";
    char STATS_COMMAND[5]="STATS";
    char THROWOUT_COMMAND[8]="THROWOUT";
    char BLOCK_COMMAND[5]="BLOCK";
    char UNBLOCK_COMMAND[7]="UNBLOCK";

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, k, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);
    FD_ZERO(&connected_fds);
    FD_ZERO(&wait_for_chat_fds);
    FD_ZERO(&flagged_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, NUMCONNECTIONS) == -1) {
        perror("listen");
        exit(3);
    }
    // printf("Add listener\n");
    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // timeout to be used in select function
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    // initialize fdlist with listener
    fdlist = NULL;
    fdlist = malloc(sizeof(struct fdSetList));
    fd_set listen_set;
    FD_ZERO(&listen_set);
    FD_SET(listener, &listen_set);
    fdlist->fdSet_entry = listen_set;
    fdlist->data_usage = 0;
    fdlist->fdSet_next = NULL;


    // main loop
    while(1) {
        //printf("!NEW ITERATION:: Waiting for data or connections\n");
        read_fds = master; // copy it
        FD_SET(STDIN_FILENO, &read_fds);
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        int channel_to_del = -1;

	    if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            FD_CLR(STDIN_FILENO,&read_fds);
            if (fgets(outbuf,BUFLEN, stdin)) {

                // search for stats command
                int pos_search = 0;
                int pos_text;
                for (pos_text = 0; pos_text < BUFLEN; ++pos_text)
                {
                    if(outbuf[pos_search] == STATS_COMMAND[pos_search])
                    {
                        ++pos_search;
                        if(pos_search == sizeof STATS_COMMAND)
                        {
                            // match

                            // count number of connected clients
                            int connected = 0;
                            for(i = 0; i <= fdmax; i++) {
                                if (FD_ISSET(i, &connected_fds)) {
                                    if (i != listener) {
                                        connected += 1;
                                    }
                                }
                            }

                            // count number of clients in chat
                            int channel_id = -1;
                            int chatting = 0;
                            // loop through all the channels
                            for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
                                
                                channel_id += 1;
                                for(i = 0; i <= fdmax; i++) {
                                    if (FD_ISSET(i, &(iter->fdSet_entry))) {
                                        if (i != listener) {
                                            chatting += 1;
                                        }
                                    }
                                }
                            }

                            // count number of clients flagged
                            int flagged = 0;
                            for(i = 0; i <= fdmax; i++) {
                                if (FD_ISSET(i, &flagged_fds)) {
                                    if (i != listener) {
                                        flagged += 1;
                                    }
                                }
                            }

                            printf("1. NUMBER OF THE CLIENTS IN THE CHAT QUEUE: %d\n", connected);
                            printf("2. NUMBER OF CLIENTS CURRENTLY IN CHAT: %d\n", chatting);
                            printf("3. DATA USAGE FOR EACH CHANNEL:\n");
                            // loop through all the channels
                            channel_id = -1;
                            for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
                                
                                channel_id += 1;
                                for(i = 0; i <= fdmax; i++) {
                                    if (FD_ISSET(i, &(iter->fdSet_entry))) {
                                        if (i != listener) {
                                            printf("       channel %d: %d bytes sent/receieved\n", channel_id, iter->data_usage);
                                            break;
                                        }
                                    }
                                }
                            }
                            printf("4. NUMBER OF USERS FLAGGED: %d.\n", flagged);
                            if (flagged>0) {
                                printf("       THIS IS THE LIST:\n");
                                for(i = 0; i <= fdmax; i++) {
                                    if (FD_ISSET(i, &flagged_fds)) {
                                        if (i != listener) {

                                            // check if he is in chat

                                            bool chatting_ = false;

                                            // loop through all the channels
                                            for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
                                                if (FD_ISSET(i, &(iter->fdSet_entry))) {
                                                    chatting_ = true;
                                                }
                                            }

                                            printf("       User ");

                                            //char name[64] = {"0"};
                                            
                                            //strcpy(list_of_names[i], name);
                                            int l = 0;
                                            while(list_of_names[i][l]){
                                                printf("%c", list_of_names[i][l]);
                                                l+=1;
                                            }

                                            if (chatting_) {
                                                printf(" currently chatting.\n");
                                            } else {
                                                printf(" currently in the chat queue.\n");
                                            }

                                        }
                                    }
                                }
                            }
                            
                            break;
                        }
                    }
                    else
                    {
                       break;
                    }
                }

                // TODO search for Throwout command
                pos_search = 0;
                for (pos_text = 0; pos_text < BUFLEN; ++pos_text)
                {
                    if(outbuf[pos_search] == THROWOUT_COMMAND[pos_search])
                    {
                        ++pos_search;
                        if(pos_search == sizeof THROWOUT_COMMAND)
                        {
                            // match
                            printf("THROWOUT COMMAND received\n");

                            // extract user id
                            char user_id[64] = {"0"};
                            int w;
                            for (w=9; w<BUFLEN-1; w++){ // -1 not to send new line
                                if (outbuf[w]) {
                                    user_id[w-9] = outbuf[w];
                                }
                            }
                            
                            int id;
                            sscanf(user_id, "%d", &id);
                            printf("user id received %d\n", id);

                            int channel_id = -1;
                            // loop through all the channels
                            for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
                                channel_id += 1;
                                if (FD_ISSET(id, &(iter->fdSet_entry))) {
                                    if (i != listener) {
                                        channel_to_del = channel_id;
                                    }
                                }
                            }

                            break;
                        }
                    }
                    else
                    {
                       break;
                    }
                }

                // search for Block command
                pos_search = 0;
                for (pos_text = 0; pos_text < BUFLEN; ++pos_text)
                {
                    if(outbuf[pos_search] == BLOCK_COMMAND[pos_search])
                    {
                        ++pos_search;
                        if(pos_search == sizeof BLOCK_COMMAND)
                        {
                            // match
                            printf("BLOCK COMMAND received\n");
                            // add to blocked list

                            // extract user id
                            char user_id[64] = {"0"};
                            int w;
                            for (w=6; w<BUFLEN-1; w++){ // -1 not to send new line
                                if (outbuf[w]) {
                                    user_id[w-6] = outbuf[w];
                                }
                            }
                            
                            int id;
                            sscanf(user_id, "%d", &id);
                            printf("user id received %d\n", id);

                            FD_SET(id, &blocked_fds);
                            // send notification of block
                            if (send(id, "You have been blocked from chatting\n", 36, 0) == -1) {
                                printf("ERROR IN SENDING MESSAGE\n");
                                perror("send ACK\n");
                            }
                            break;
                        }
                    }
                    else
                    {
                       break;
                    }
                }

                // search for Unblock command
                pos_search = 0;
                for (pos_text = 0; pos_text < BUFLEN; ++pos_text)
                {
                    if(outbuf[pos_search] == UNBLOCK_COMMAND[pos_search])
                    {
                        ++pos_search;
                        if(pos_search == sizeof UNBLOCK_COMMAND)
                        {
                            // match
                            printf("UNBLOCK COMMAND received\n");

                            // extract user id
                            char user_id[64] = {"0"};
                            int w;
                            for (w=8; w<BUFLEN-1; w++){ // -1 not to send new line
                                if (outbuf[w]) {
                                    user_id[w-8] = outbuf[w];
                                }
                            }
                            
                            int id;
                            sscanf(user_id, "%d", &id);
                            printf("user id received %d\n", id);

                            // add to connected list
                            FD_CLR(id, &blocked_fds);
                            // send acknoledgement of unblock
                            if (send(id, "You have been unblocked\n", 24, 0) == -1) {
                                printf("ERROR IN SENDING MESSAGE\n");
                                perror("send ACK\n");
                            }
                            break;
                        }
                    }
                    else
                    {
                       break;
                    }
                }


            }
        }

        // run through all the existing clients looking for data to read
        // printf("Received something\n");
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        printf("Added (to master) client number %d\n", newfd);
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        // TODO maintain closed connection
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set 
                    } else {
                        // we got some data from a client

                        // search for connect command
                        int pos_search = 0;
                        int pos_text;
                        for (pos_text = 0; pos_text < nbytes; ++pos_text)
                        {
                            if(buf[pos_search] == CONNECT_COMMAND[pos_search])
                            {
                                ++pos_search;
                                if(pos_search == sizeof CONNECT_COMMAND)
                                {
                                    // match
                                    printf("CONNECT COMMAND received\n");
                                    // add to connected list
                                    FD_SET(i, &connected_fds);
                                    printf("Added (to connected) client number %d\n", i);
                                    // send acknoledgement of connection
                                    if (send(i, "Connection successfull. Please type your name with NAME command\n", 64, 0) == -1) {
                                        printf("ERROR IN SENDING MESSAGE\n");
                                        perror("send ACK\n");
                                    }
                                    // TODO what does that mean that queue is full?
                                    break;
                                }
                            }
                            else
                            {
                               break;
                            }
                        }

                        // search for help command
                        pos_search = 0;
                        for (pos_text = 0; pos_text < nbytes; ++pos_text)
                        {
                            if(buf[pos_search] == HELP_COMMAND[pos_search])
                            {
                                ++pos_search;
                                if(pos_search == sizeof HELP_COMMAND)
                                {
                                    // match
                                    printf("HELP COMMAND received\n");
                                    // add to connected list
                                    FD_SET(i, &connected_fds);
                                    // printf("Added (to connected) client number %d\n", i);
                                    // send respond to help command
                                    if (send(i, "Commands Accepted:\n1. CONNECT.\n2. CHAT\n3. QUIT\n4. TRANSFER\n5. FLAG\n6. HELP\n", 75, 0) == -1) { //TODO send correct number of bytes
                                        printf("ERROR IN SENDING MESSAGE\n");
                                        perror("send HELP\n");
                                    }
                                    // TODO what does that mean that queue is full?
                                    break;
                                }
                            }
                            else
                            {
                               break;
                            }
                        }
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through master file descriptor set

        // count number of clients that connected
        int counter = 0;
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &connected_fds)) {
                if (i != listener) {
                    counter += 1;
                }
            }
        }
        //printf("Number of connected clients %d\n", counter);

        int channel_id = -1;
        // loop through all the channels and transmit the data between them
        for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
            
            channel_id += 1;
            int counter = 0;
            for(i = 0; i <= fdmax; i++) {
                if (FD_ISSET(i, &(iter->fdSet_entry))) {
                    if (i != listener) {
                        counter += 1;
                    }
                }
            }

            //printf("Number of clients on the channel %d\n", counter);

            if(counter > 0){ // if iter->fdSet_entry is not empty

                // loop through the fd list
                for(i = 0; i <= fdmax; i++) {
                    if (FD_ISSET(i, &read_fds ) && FD_ISSET(i, &(iter->fdSet_entry))) {
                        if (i != listener) {


                            bool quit_cmd = false;
                            bool flag_cmd = false;
                            // search for quit command
                            int pos_search = 0;
                            int pos_text;
                            for (pos_text = 0; pos_text < nbytes; ++pos_text)
                            {
                                if(buf[pos_search] == QUIT_COMMAND[pos_search])
                                {
                                    ++pos_search;
                                    if(pos_search == sizeof QUIT_COMMAND)
                                    {
                                        // match
                                        printf("QUIT COMMAND received for channel %d\n", channel_id);
  
                                        // mark channel for deletion
                                        channel_to_del = channel_id;
                                        quit_cmd = true;

                                        if (send(i, "Quited from the channel\n", 24, 0) == -1) {
                                            printf("ERROR IN SENDING MESSAGE\n");
                                            perror("send QUIT\n");
                                        }
                                        // TODO what does that mean that queue is full?
                                        break;
                                        break;
                                    }
                                }
                                else
                                {
                                   break;
                                }
                            }

                            // search for flag command
                            pos_search = 0;
                            for (pos_text = 0; pos_text < nbytes; ++pos_text)
                            {
                                if(buf[pos_search] == FLAG_COMMAND[pos_search])
                                {
                                    ++pos_search;
                                    if(pos_search == sizeof FLAG_COMMAND)
                                    {
                                        // match
                                        printf("FLAG COMMAND received\n");
                                        // add to flagged list
                                        // loop through the fd list
                                        int partner;
                                        for(iter = fdlist; iter != NULL; iter = iter->fdSet_next) {
                                            if (FD_ISSET(i, &(iter->fdSet_entry))) {
                                                int r;
                                                for(r = 0; r <= fdmax; r++) {
                                                    if (FD_ISSET(r, &(iter->fdSet_entry))) {
                                                        if (r != i) {
                                                            partner = r;
                                                            break;
                                                        }
                                                    }
                                                }
                                                break;
                                                // find chatting partner
                                            }
                                        }

                                        FD_SET(partner, &flagged_fds);
                                        flag_cmd = true;
                                        break;
                                        break;
                                    }
                                }
                                else
                                {
                                   break;
                                }
                            }

                            // transfer data
                            for(j = 0; j <= fdmax; j++) {

                                if (FD_ISSET(j, &(iter->fdSet_entry)) ) {
                                    // except the listener and ourselves
                                    if (j != listener && j != i) {
                                        if (!quit_cmd && !flag_cmd) {
                                            char new_buf[256] = {0};

                                            int l = 0;
                                            while(list_of_names[i][l]){
                                                printf("%c", list_of_names[i][l]);
                                                l+=1;
                                            }
                                            printf(" says.\n");

                                            strcpy(new_buf, list_of_names[i]);
                                            strcat(new_buf, ": ");
                                            strcat(new_buf, buf);
                                            if (send(j, new_buf, nbytes+l+2, 0) == -1) {
                                                perror("send");
                                            } else {
                                                printf("Add %d data \n", nbytes);
                                                iter->data_usage += nbytes;
                                            }
                                        } else {
                                            if (quit_cmd) {
                                                if (send(j, "Quited from the channel\n", 24, 0) == -1) {
                                                    printf("ERROR IN SENDING MESSAGE\n");
                                                    perror("send QUIT\n");
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }


        // run through the connected clients
        for(i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &blocked_fds)) {
                if (FD_ISSET(i, &connected_fds) && FD_ISSET(i, &read_fds)) { // we got one!!
                    printf("Got smth from the connected client\n");
                    if (i != listener) { // we don't want to process lisening socket here
                        // handle data from a client
                        // search for chat command
                        int pos_text;
                        int pos_search = 0;
                        for (pos_text = 0; pos_text < nbytes; ++pos_text)
                        {
                            if(buf[pos_search] == CHAT_COMMAND[pos_search])
                            {
                                ++pos_search;
                                if(pos_search == sizeof CHAT_COMMAND)
                                {
                                    // match
                                    printf("CHAT COMMAND received from client number %d\n", i);
                                    // TODO add to wait for chat list
                                    FD_SET(i, &wait_for_chat_fds);
                                    break;
                                }
                            }
                            else
                            {
                               break;
                            }
                        }

                        // search for name command
                        pos_search = 0;
                        for (pos_text = 0; pos_text < nbytes; ++pos_text)
                        {
                            if(buf[pos_search] == NAME_COMMAND[pos_search])
                            {
                                ++pos_search;
                                if(pos_search == sizeof NAME_COMMAND)
                                {
                                    char name[64] = {"0"};
                                    int w;
                                    printf("bytes received %d\n", nbytes);
                                    for (w=5; w<nbytes-1; w++){ // -1 not to send new line
                                        if (buf[w]) {
                                            name[w-5] = buf[w];
                                        }
                                    }
                                    // match
                                    printf("NAME COMMAND received\n");
                                    // add to connected list
                                    //FD_SET(i, &connected_fds);
                                    
                                    strcpy(list_of_names[i], name);
                                    printf("Added a name client number %d ", i);
                                    int l = 0;
                                    while(list_of_names[i][l]){
                                    //for(l = 0; l<64; l++) {
                                        printf("%c", list_of_names[i][l]);
                                        l+=1;
                                    }
                                    printf("\n");
                                    break;
                                }
                            }
                            else
                            {
                               break;
                            }
                        }

                    }
                } // end got new incoming connections
            }
        } // end looping through connected file descriptor set

        // create channels for random pairs of clients from wait_for_chat file descriptor set
        while(1) {
            // count number of clients that wait for chat
            int counter = 0;
            for(i = 0; i <= fdmax; i++) {
                if (FD_ISSET(i, &wait_for_chat_fds)) {
                    if (i != listener) {
                        counter += 1;
                    }
                }
            }

            //printf("Clients waiting to chat: %d\n", counter);

            int r1, r2, fd1, fd2;
            // if this number less than 2 break
            if(counter < 2){
                break;
            } else {
                // if the number is bigger than 2 choose two random clients
                r1 = rand()%counter;
                while(1){
                    r2 = rand()%counter;
                if (r1!=r2) break;
                }
            }

            //printf("Create a channel for two clients\n");

            int ind = 0;
            // find these two clients in the set
            for(i = 0; i <= fdmax; i++) {
                if (FD_ISSET(i, &wait_for_chat_fds)) {
                    if (i != listener) {
                        if(ind == r1){
                            fd1 = i;
                        }
                        if(ind == r2){
                            fd2 = i;
                        }
                        ind += 1;
                    }
                }
            }

            // remove two chosen clients from the waitlist
            FD_CLR(fd1, &wait_for_chat_fds);
            FD_CLR(fd2, &wait_for_chat_fds);

            // create channels for a pair of clients
            fd_set channel;
            FD_ZERO(&channel);
            FD_SET(fd1, &channel);
            // send acknoledgement of chat
            if (send(fd1, "Chat established\n", 17, 0) == -1) {
                printf("ERROR IN SENDING MESSAGE\n");
                perror("send IN SESSION\n");
            } else {
                printf("ACK sent to client number %d\n", fd1);
            }
            FD_SET(fd2, &channel);
            // send acknoledgement of chat
            if (send(fd2, "Chat established\n", 17, 0) == -1) {
                printf("ERROR IN SENDING MESSAGE\n");
                perror("send IN SESSION\n");
            } else {
                printf("ACK sent to client number %d\n", fd2);
            }

            PushToFDSet(&fdlist, channel);
        }

        if (fdlist == NULL) {
            printf("No channels yet\n");
        }

        // Deleting channels

        //printf("CHANNEL TO DELETE %d,\n",channel_to_del);

        if (channel_to_del >= 0) {

            // loop through all the channels
            printf("Remove the channel\n");
            int channel_id = -1;
            prev = NULL;
            for(iter = fdlist; iter != NULL; prev = iter, iter = iter->fdSet_next) {
                channel_id += 1;
                if (channel_id == channel_to_del) {
                    printf("Channel found\n");
                    if (prev == NULL) {
                        fdlist = iter->fdSet_next;
                    } else {
                        prev->fdSet_next = iter->fdSet_next;
                    }
                    for(j = 0; j <= fdmax; j++) {
                        if (FD_ISSET(j, &(iter->fdSet_entry)) ) {
                            FD_SET(j, &connected_fds);
                        }
                    }

                    free(iter);

                    break;
                }
            }

        }

    } 
    
    return 0;
}
