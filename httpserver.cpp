#include <unistd.h> 
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>


#define BUFFER_SIZE 4096

// struct to store info for client request
struct httpObject {
    char method[4];         
    char filename[28];      
    char httpversion[9];
    char timestamp[20];    
    ssize_t content_length; 
    int status_code;
    uint8_t buffer[BUFFER_SIZE];
    struct stat s;
};

// checks for error from function calls and prints associated function
void check(int n, char *msg) {
	if(n < 0) {
		err(1, "%s", msg);
	}
}

// gets specified address  
unsigned long getaddr(char *name) {
	unsigned long res;
	struct addrinfo hints;
	struct addrinfo* info;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	
	if(getaddrinfo(name, NULL, &hints, &info) != 0 || info == NULL) {
		fprintf(stderr, "error finding %s\n", name);
		exit(1);
	}
	res = ((struct sockaddr_in*)info->ai_addr)->sin_addr.s_addr;
	freeaddrinfo(info);
	return res;
}

// converts time to string
void stringconv(time_t num, char* string){
    int i = 0;
    char first[100];
    while(num != 0){
        int r = num % 10;
        if(r >= 10){
            first[i] = 65 + (r - 10);
        }
        else{
            first[i] = 48 + r;
        }
        num = num / 10;
        i++;
    }
    i--;
    int j = 0;
    while(i != -1){
        string[j] = first[i];
        i--;
        j++;
    }
    string[j] = '\0';
}

// parses client request into struct
void parserequest(ssize_t comm_fd, struct httpObject* message){
    uint8_t buff[BUFFER_SIZE + 1];
    ssize_t bytes = recv(comm_fd, buff, BUFFER_SIZE, 0);
    buff[bytes] = 0; 
    printf("[+] received %ld bytes from client\n[+] request: \n", bytes);
    
    write(STDOUT_FILENO, buff, bytes); //received message
    
    if (bytes < 0){
        message->status_code = 400; // Bad request
    }
    
    // get method 
    char* tok = strtok((char*) &buff, " ");
    strcpy(message->method, tok);

    // get filename 
    if (tok != NULL){
        tok = strtok(NULL, " ");
        tok = tok + 1; // get rid of "/" by adding 1
        strcpy(message->filename, tok); 
    }   

    // get HTTP version
    if (tok != NULL){
        tok = strtok(NULL, "\r\n");
        strcpy(message->httpversion, tok);
    }

    // skip stuff
	for(int i = 0; i < 3; i++){
		tok = strtok(NULL, "\r\n\r\n");
	}

    // get content length if there is one
    ssize_t cl;
    if (tok != NULL){
        tok = strtok(NULL, "\r\n\r\n");
        if (tok != NULL){
            if (strlen(tok) > 0){
                sscanf(tok, "Content-Length: %zd", &cl); //int s =
                message->content_length = cl;
            }
        }
    }
}

// sets appropriate status code for response
void setstatuscode(ssize_t comm_fd, struct httpObject* message){
	//printf("Processing Request\n");
    
    // check if file name valid - must be alphanumeric or -/_
    for (size_t i = 0; i < strlen(message->filename); i++){
		if(isalnum(message->filename[i]) == 0){
            //printf("bad\n");
			message->status_code = 400;
   		    break;
		}
	}

	// check if file length is equal to 10 and if httpversion is correct
	if(strlen(message->filename) != 10 || strcmp(message->httpversion, "HTTP/1.1") != 0){
		message->status_code = 400;
	}

    // check if method is GET or PUT
    if(strcmp(message->method, "GET") != 0 && strcmp(message->method, "PUT") != 0){
        message->status_code = 500;
    }

	// we can proceed with the other checks
	if(message->status_code != 400 && message->status_code != 500){
	
	// if valid GET request
    	if (strcmp(message->method, "GET") == 0){
            // try to open file
            int fd = open(message->filename, O_RDONLY);
            
            stat(message->filename, &message->s);

            if (!(message->s.st_mode & R_OK)){
                message->status_code = 403; 
            }       
            
            // check for permissions
            if (fd == -1){
                if(errno == EACCES) {
                    message->status_code = 403; 
                }
                else if(errno == ENOENT) {
                    message->status_code = 404; 
                }
                else {
                    message->status_code = 500; 
                }
            }
            else{
                if (message->status_code != 403){
                    // set response code
                    message->status_code = 200; 
                
                    ssize_t len;

                    // File_NAME contents
                    while ((len = read(fd, message->buffer, sizeof(message->buffer))) > 0){
                        message->content_length += len;
                    }  
                }
 
            } 
            close(fd);
        }
    	// if valid PUT request
    	else if (strcmp(message->method, "PUT") == 0){       
            int fd = open(message->filename, O_WRONLY);
            
            // check if file has already been created or not
            if (fd > 0) {
                message->status_code = 200; // OK
            }
            else{
                message->status_code = 201; 
            }
            close(fd);
            // Open file and set permissions while writing contents from buffer to new file
	        fd = open(message->filename, O_CREAT | O_WRONLY, 0666);
            fchmod(fd, S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR);
            if (message->content_length > BUFFER_SIZE){ //if content length larger than buffer size
                for (int i = 0; i <= (message->content_length/BUFFER_SIZE); i++){
                    uint8_t buff[BUFFER_SIZE];
                    ssize_t bytes_read = recv(comm_fd, buff, BUFFER_SIZE, 0);
                    write(fd, buff, bytes_read);
                }
            }
            else{
                uint8_t buff_put[message->content_length + 1];
                ssize_t bytes_read = recv(comm_fd, buff_put, message->content_length, 0);
                write(fd, buff_put, bytes_read); 
            }

            close(fd);
        }
	}
}

// creates and sends response back to client
void sendresponse(ssize_t comm_fd, int flag, struct httpObject* message){
    //printf("Constructing Response\n");
    
    // Status Code
    char status[29];
    switch(message->status_code){
        case 200 :
            strcpy(status, "200 OK");
            break;
        case 201 :
            strcpy(status, "201 Created");
            break;
        case 400 :
            strcpy(status, "400 Bad Request");
            break;
        case 403 :
            strcpy(status, "403 Forbidden");
            break;
        case 404 :
            strcpy(status, "404 Not Found");
            break;
        case 500 :
            strcpy(status, "500 Internal Server Error");
            break;
    }

    // content length for PUT and any non-200 set to 0
    if (strcmp(message->method, "PUT") == 0 || message->status_code != 200 || flag == 1){ //added flag condition temporarily for early testing but will need to adjust later: for b and r
        message->content_length = 0;
    }

    
    printf("%s %s\r\nContent-Length: %ld\r\n\r\n", 
        message->httpversion, 
        status, 
        message->content_length);
    
    // Send response header
    dprintf(comm_fd, "%s %s\r\nContent-Length: %ld\r\n\r\n", 
        message->httpversion, 
        status, 
        message->content_length);   

    // if GET send file content
    if (strcmp(message->method, "GET") == 0 && flag != 1){                //added flag condition temporarily for early testing but will need to adjust later: doesnt work for b l r 
        if (message->status_code == 200){
            int fd = open(message->filename, O_RDONLY);
            ssize_t len;
            while ((len = read(fd, message->buffer, sizeof(message->buffer))) > 0){
                if (send(comm_fd, message->buffer, len, 0) < 0){
                    break;
                }
            }   
        }
    }

    // clear message object for next client request
    message->status_code = 0;
    message->content_length = 0; 
    memset(message->buffer, 0, sizeof(message->buffer));
}



int extrajobs(ssize_t comm_fd, struct httpObject* message){
    //if filename is "b", "l", "r", or "r/[timestamp]"
    int random; 
    char letters[5] = "blr/";
    char backup[7] = "backup";
    if(strcmp(message->method, "GET") != 0){
        return 0;
    }
    if(strlen(message->filename) == 1 || (message->filename[0] == letters[2] && message->filename[1] == letters[3])){
        //put timestamp into message->timestamp
        char timestampbuff[BUFFER_SIZE];
        memset(timestampbuff, 0, sizeof(timestampbuff));
        if(strcmp(message->filename, "b") == 0){   //if backup, make copies of files that we can GET
            message->status_code = 200;
            
            time_t timer;
            time(&timer);
            char backupstr[18];
            char timestampstr[11];
            stringconv(timer, timestampstr);
            strcpy(backupstr, backup);
            strcat(backupstr, "-");
            strcat(backupstr, timestampstr);
            printf("%s\n", backupstr);
            mkdir(backupstr, 0777);

            char save1[30];
            DIR *dr = opendir("."); //open present directory
            struct dirent *en;
            struct httpObject tempmessage; 
            if (dr) {
                while ((en = readdir(dr)) != NULL) {
                    (&tempmessage)->status_code = 0;
                    (&tempmessage)->content_length = 0;
                    strcpy(save1, en->d_name);
                    strcpy(message->filename,save1);
                    char* tok = strtok((char*) &en->d_name, "-");
                    if(strcmp(backup,tok) == 0){
                        continue;
                    }
                    strcpy((&tempmessage)->method, message->method);
                    strcpy((&tempmessage)->httpversion, message->httpversion);
                    strcpy((&tempmessage)->filename, save1);
                    setstatuscode(0, &tempmessage);  //comm_fd doesn't matter here since this only applies to GET requests
                    if((&tempmessage)->status_code != 200){
                        continue;
                    }
                    // Open file and set permissions while writing contents from buffer to new file
                    char newfile[19+strlen(save1)];
                    strcpy(newfile, backupstr);
                    strcat(newfile, "/");
                    strcat(newfile, save1);
                    int oldfd = open(save1, O_RDONLY);
	                int fd = open(newfile, O_CREAT | O_WRONLY, 0666);
                    fchmod(fd, S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR);
                    int len;
                    while ((len = read(oldfd, (&tempmessage)->buffer, sizeof((&tempmessage)->buffer))) > 0){
                        if (write(fd, (&tempmessage)->buffer, len) < 0){
                            break;
                        }
                    }   
                    close(oldfd);
                    close(fd);
                }
            }
        }
        else if(strcmp(message->filename, "l") == 0){ //if list, list all backups with timestamps
            char status[29];
            char save2[30];
            strcpy(status, "200 OK");
            DIR *dr;
            struct dirent *en;
            dr = opendir("."); //open present directory
            if (dr) {
                while ((en = readdir(dr)) != NULL) {
                    strcpy(save2, en->d_name);
                    printf("save: %s\n", save2); //print filename
                    char* tok = strtok((char*) &en->d_name, "-");
                    if(strcmp(backup,tok) == 0){
                        tok = strtok(NULL, " ");
                        if(strlen(timestampbuff) == 0){
                            strcpy(timestampbuff, tok);
                            strcat(timestampbuff, "\n");
                        }
                        else{
                            strcat(timestampbuff, tok);
                            strcat(timestampbuff, "\n");
                        }
                        printf("tok: %s\n", tok);
                        printf("timestampbuff: %s", timestampbuff);
                        //printf("timestamp: %s\n", tok);
                    }
                }
                closedir(dr); //close all directory
            }
            printf("end: %s", timestampbuff);
            //strcpy(status, "Conten")
             // Send response header
            dprintf(comm_fd, "%s %s\r\nContent-Length: %ld\r\n\r\n", message->httpversion, status, strlen(timestampbuff));
            send(comm_fd, timestampbuff, strlen(timestampbuff), 0);
            //message->status_code = 200;
        }
        else if(message->filename[0] == letters[2]){ //if recovery
            int specific = 0;
            char save3[30];
            char chosentimestamp[10];
            char chosentimestampstring[18];
            char picktimestamp[18];
            strcpy(chosentimestampstring, backup);
            strcat(chosentimestampstring, "-");
            if(strlen(message->filename) != 1){  //if specified timestamp
                specific = 1;
                //printf("filename length: %d\n", strlen(message->filename));
                if(strlen(message->filename) != 12){
                    message->status_code = 400;
                    return 0;
                }
                //get the specific timestamp
                for(int i = 0; i < 10; i++){
                    chosentimestamp[i] = message->filename[i+2];
                }
                //printf("chosen timestamp: %s\n", chosentimestamp);
                strcat(chosentimestampstring, chosentimestamp);
                //printf("new name: %s\n", chosentimestampstring);
                //check timestamp against backup timestamps
                int check = 0;
                DIR *dr = opendir("."); //open present directory
                struct dirent *en;
                if (dr) {
                    while ((en = readdir(dr)) != NULL) {
                        strcpy(save3, en->d_name);
                        //printf("save: %s   strcmp: %d\n",save3,strcmp(chosentimestampstring,save3));
                        if(strcmp(chosentimestampstring, save3) == 0){
                            check = 1;
                            break;
                        }
                    }
                }
                if(check != 1){
                    memset(chosentimestamp, 0, sizeof(chosentimestamp));
                    memset(chosentimestampstring, 0, sizeof(chosentimestampstring));
                    message->status_code = 404; //timestamp not found
                    return 0;
                }
                else{
                    message->status_code = 200;
                }
            }
            else{    //choose timestamp here
                char save4[30];
                memset(picktimestamp, 0, sizeof(picktimestamp));
                memset(save4, 0, sizeof(save4));
                DIR *dr = opendir("."); //open present directory
                struct dirent *en;
                int check = 0;
                if (dr) {
                    while ((en = readdir(dr)) != NULL) {
                        strcpy(save4, en->d_name);
                        char* tok = strtok((char*) &en->d_name, "-");
                        if(strcmp(backup,tok) == 0){
                            check = 1;
                            tok = strtok(NULL, " ");
                            //printf("strcmp: %d   picktimestamp: %s  save4: %s\n", strcmp(picktimestamp,save4), picktimestamp, save4);
                            if(strcmp(picktimestamp,save4) < 0){
                                strcpy(picktimestamp,save4);
                            }
                        }
                    }
                }
                if(check != 1){
                    message->status_code = 404;
                    return 1;
                }
                //printf("mostrecent: %s\n", picktimestamp);
                message->status_code = 200;
            }
            //printf("this is the save: %s\n", save3);
            //do recovery here (take original files from backup that we can GET and then overwrite current files)
            // message->status_code = 200;

            char save5[30];
            DIR *dr;
            // open directory of backup folder either specified by user or the one we chose
            if(specific == 1){
                dr = opendir(chosentimestampstring); 
            }
            else{
                dr = opendir(picktimestamp); 
            }
            struct dirent *en;
            struct httpObject tempmessageoriginal; 
            int x;
            if (dr) {
                while ((en = readdir(dr)) != NULL) {
                    (&tempmessageoriginal)->status_code = 0;
                    (&tempmessageoriginal)->content_length = 0;
                    strcpy(save5, en->d_name);
                    //strcpy(message->filename,save4);
                    char* tok = strtok((char*) &en->d_name, "-");
                    if(strcmp(backup,tok) == 0){
                        continue;
                    }
                    char newfile[19+strlen(save5)];
                    if(specific == 1){
                        strcpy(newfile, chosentimestampstring);
                    }
                    else{
                        strcpy(newfile, picktimestamp);
                    }
                    strcat(newfile, "/");
                    strcat(newfile, save5);
                    strcpy((&tempmessageoriginal)->method, message->method);
                    strcpy((&tempmessageoriginal)->httpversion, message->httpversion);
                    strcpy((&tempmessageoriginal)->filename, save5);
                    setstatuscode(0, &tempmessageoriginal);   //comm_fd doesn't matter here since this only applies to GET requests
                    if((&tempmessageoriginal)->status_code != 200 && (&tempmessageoriginal)->status_code != 404){
                        continue;
                    }
                    printf("saveorig: %s\nsaverecfolder: %s\n", save5, newfile);
                    // Do PUT here to recover files from backup
                    int recfolderfd = open(newfile, O_RDONLY);
	                int originalfd = open(save5, O_CREAT | O_WRONLY, 0666);
                    printf("recfd: %d\noriginalfd: %d\n", recfolderfd, originalfd);
                    fchmod(originalfd, S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR);
                    int len;
                    while ((len = read(recfolderfd, (&tempmessageoriginal)->buffer, sizeof((&tempmessageoriginal)->buffer))) > 0){
                        if (write(originalfd, (&tempmessageoriginal)->buffer, len) < 0){
                            break;
                        }
                    }   
                    close(recfolderfd);
                    close(originalfd);
                }
            }

            memset(chosentimestamp, 0, sizeof(chosentimestamp));
            memset(chosentimestampstring, 0, sizeof(chosentimestampstring));
        }
        else{
            return 0; //if filename is 1 letter but not any of the characters we want
        }
        return 1;
    }
    else{
        return 0;
    } 
}

int main(int argc, char *argv[]) {
	 // Set default port to 80 
	 unsigned short PORT = 80;
	
    //Check if there is a port argument or not enough arguments given
    if(argc == 3){
    	PORT = atoi(argv[2]);
    } else if (argc < 2) {
        fprintf(stderr, "Address argument required \n");
    	exit(1);
    }
   	
	// Set address variable to send to getaddr() 	 
	char address[strlen(argv[1] + 1)];
   	strcpy(address, argv[1]);

	struct sockaddr_in serv_addr; 
	memset(&serv_addr, 0, sizeof(serv_addr));
	
	// Setup sockaddr_in struct 
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_port = htons(PORT);
   	serv_addr.sin_addr.s_addr = getaddr(address); 

	// Setup socket and check listen function for error	
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	check(listen_fd, (char*) "listenfd");

	// Configure server socket
	int enable = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

	// Bind socket and listen for connections, and check for errors from each function
	check(bind(listen_fd,(struct sockaddr*) &serv_addr, sizeof serv_addr),(char*) "bind");
	check(listen(listen_fd, 500), (char*) "listen");


	// Object for one http request to store info on
	struct httpObject message; 

	// Accept and process connections 	
	while(1) {
		// Display waiting prompt and accept connection 
		printf("Waiting for connection\n\n");
		int comm_fd = accept(listen_fd, NULL, NULL);
        int flag;

		// Read and parse the request
		parserequest(comm_fd, &message);

        // Do backup, list, or recovery if neccessary and set appropriate status code
        // flag indicates if we are doing backup, list, or recovery, or if it's just a normal request
        flag = extrajobs(comm_fd, &message);

         // If normal request, set status code for response
        if(flag == 0 && (&message)->status_code != 404 && (&message)->status_code != 400){
		    setstatuscode(comm_fd, &message);
        }
		
        if(strcmp((&message)->filename, "l") != 0){
		    // Construct the response header and send it
 	        sendresponse(comm_fd, flag, &message);
        }

        // Close connection with client once response is sent
        close(comm_fd);
	}
	return 0; 								
}