/*
** client.c -- client
This is an implementation of a client that is able to connect to an IRC-like server.
Author: Artem Gritsenko
Based on the manual from: http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
*/

#include<stdio.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>

#include <netinet/in.h>

#include <netdb.h>

#define BUFLEN 1000

#define SERVER "localhost"
int main()
{

	system("clear");
	printf("----Welcome to the Online Chatting System!----\nPlease type 'CONNECT' to connect to the server.\nTo check supported commands, input 'HELP'.\n");
        int sockfd,x;
        struct sockaddr_in server;  
        fd_set rfds, wfds;
        struct timeval tv;
        struct hostent *sp;

    	int retval;
    	char outbuff[BUFLEN];
    	char inbuff[BUFLEN];
    	char namebuff[BUFLEN];

    	memset(&outbuff,0,sizeof(outbuff));

    	sockfd = socket(AF_INET , SOCK_STREAM , 0);
    
    	memset(&server,0,sizeof server);
    
	
	sp = gethostbyname(SERVER);
	memcpy(&server.sin_addr, sp->h_addr, sp->h_length);


	server.sin_family = AF_INET;
	server.sin_port = htons( 9034 );
    	connect(sockfd , (struct sockaddr *)&server , sizeof(server));


    	while(1) {

        	FD_ZERO(&wfds);
        	FD_ZERO(&rfds);
        	FD_SET(STDIN_FILENO, &rfds);
        	FD_SET(sockfd,&rfds);
        	tv.tv_sec = 1;
        	tv.tv_usec = 0;

        	if(strlen(outbuff)!=0) 
        	{
        	FD_SET(sockfd,&wfds);
		}

        	if(retval = select(sockfd + 1, &rfds, &wfds, NULL, &tv)<0)
        	{     
           		sleep(5);
            		continue;
        	}	
       
	    	if (FD_ISSET(sockfd,&rfds))
	    	{
			FD_CLR(sockfd,&rfds);
		    	memset(&inbuff,0,sizeof(inbuff));
		    	int length=0;
		    	if (length=recv(sockfd, inbuff, sizeof(inbuff)-1,0)<=0)
		    	{
				close(sockfd);
				break;
			} else 
			{	
				
				if (strncmp(inbuff,": TRANSFER",10)==0)
				{
				
					memset(namebuff,0,sizeof namebuff);
				 	memcpy(namebuff, &inbuff[10], 31);
					namebuff[31] = '\0';
					printf("You are receiving %s",namebuff);
					memset(inbuff,0,sizeof inbuff);
					
					FILE *fp2=fopen("Received.txt","w");
					fwrite(inbuff,sizeof(char),1024,fp2);
					
					memset(inbuff,0,sizeof inbuff);
					printf("File %s received", namebuff);
					fclose(fp2);
					
					
				
				}else printf("%s",inbuff);  
	    		}
		}
	    	
	    	if(FD_ISSET(sockfd, &wfds))
	    	{
		    	FD_CLR(sockfd, &wfds);
		    	write(sockfd, outbuff, strlen(outbuff));
		    	memset(&outbuff,0,sizeof(outbuff));
		}      

		if (FD_ISSET(STDIN_FILENO, &rfds)) 
		{
            		FD_CLR(STDIN_FILENO,&rfds);
            		if (fgets(outbuff,BUFLEN, stdin)) 
            		{
	            		if (strncmp(outbuff, "quit", 4) == 0) 
	            		{
					exit(0);
				
				}else if( send(sockfd, outbuff, strlen(outbuff) , 0) < 0) 
				{
                        		puts("Send failed");
                         		return 1;
                    		}else if(strncmp(outbuff,"TRANSFER",8)==0)
				{
					memset(namebuff,0,sizeof namebuff);
				 	memcpy(namebuff, &outbuff[9], 31);
					namebuff[31] = '\0';
					printf("You are transfering %s",namebuff);
					memset(outbuff,0,sizeof outbuff);
					
					char fileobuf[1024];
					FILE *fp1=fopen(namebuff,"r");
					memset(fileobuf,0,sizeof fileobuf);
					int file_block_length=0;
					while(file_block_length=fread(fileobuf,sizeof(char),1024,fp1))
					{
						printf("file_block_length= %d\n",file_block_length);
					
						send(sockfd,fileobuf,sizeof fileobuf,0);
					}
					memset(fileobuf,0,sizeof fileobuf);
					fclose(fp1);
					printf("Transfer %s finished",namebuff);	
		            	}
		            	
		            	memset(outbuff,0,sizeof outbuff);
            		}
         	}

	}
    	return 0;
}

