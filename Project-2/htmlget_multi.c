#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>

int create_tcp_socket();
char *get_ip(char *host);
char *build_get_query(char *host, char *page);
void usage();
void *client(void *arg);
int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1);

#define HOST "coding.debuntu.org"
#define PAGE "index.html"
#define USERAGENT "HTMLGET 1.0"

#define MAX_THREAD 100

char *host;
char *page;
int port;

int main(int argc, char **argv)
{
		if(argc < 3){
				usage();
				exit(2);
		}

  struct timeval tvBegin, tvEnd, tvDiff;

		host = argv[1];
		port = atoi(argv[2]);
		int num_thread = atoi(argv[3]);
		if(num_thread > MAX_THREAD) num_thread = MAX_THREAD;
		
		page = PAGE; 
		gettimeofday(&tvBegin, NULL);

		pthread_t p[MAX_THREAD];
		int i;
		for(i = 0; i < num_thread; i++)
		{
				pthread_create(&(p[i]), NULL, client, NULL);
		}

		for(i = 0; i < num_thread; i++)
		{
				pthread_join(p[i], NULL);
		}
		gettimeofday(&tvEnd, NULL);
		timeval_subtract(&tvDiff, &tvEnd, &tvBegin);
		printf("Time to handle %d requests: %ld.%06ld sec\n", num_thread, tvDiff.tv_sec, tvDiff.tv_usec);
}

void *client(void *arg)
{
		struct sockaddr_in *remote;
		int sock;
		int tmpres;
		char *get;
		char buf[BUFSIZ+1];
		char **argv = arg;
		char *ip;

		sock = create_tcp_socket();
		ip = get_ip(host);
		//fprintf(stderr, "IP is %s:%d\n", ip, port); 
		remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
		remote->sin_family = AF_INET;
		tmpres = inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
		if( tmpres < 0)  
		{
				perror("Can't set remote->sin_addr.s_addr");
				exit(1);
		}else if(tmpres == 0)
		{
				fprintf(stderr, "%s is not a valid IP address\n", ip);
				exit(1);
		}
		remote->sin_port = htons(port);

		if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0){
				perror("Could not connect");
				exit(1);
		}
		get = build_get_query(host, page);
		//fprintf(stderr, "Query is:\n<<START>>\n%s<<END>>\n", get);

		//Send the query to the server
		int sent = 0;
		while(sent < strlen(get))
		{ 
				tmpres = send(sock, get+sent, strlen(get)-sent, 0);
				if(tmpres == -1){
						perror("Can't send query");
						exit(1);
				}
				sent += tmpres;
		}
		//now it is time to receive the page
		memset(buf, 0, sizeof(buf));
		int htmlstart = 0;
		char * htmlcontent;
		while((tmpres = recv(sock, buf, BUFSIZ, 0)) > 0);

		if(tmpres < 0)
		{
				perror("Error receiving data");
		}
		free(get);
		free(remote);
		free(ip);
		close(sock);
		return 0;
}

void usage()
{
		fprintf(stderr, "USAGE: htmlget host port #_thread\n");
		fprintf(stderr, "\t- host: the website hostname. ex: 127.0.0.1\n");
		fprintf(stderr, "\t- port: the port number. ex: 9999 \n");
		fprintf(stderr, "\t- #_thread: number of thread. ex: 10\n");
}


int create_tcp_socket()
{
		int sock;
		if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
				perror("Can't create TCP socket");
				exit(1);
		}
		return sock;
}


char *get_ip(char *host)
{
		struct hostent *hent;
		int iplen = 15; //XXX.XXX.XXX.XXX
		char *ip = (char *)malloc(iplen+1);
		memset(ip, 0, iplen+1);
		if((hent = gethostbyname(host)) == NULL)
		{
				herror("Can't get IP");
				exit(1);
		}
		if(inet_ntop(AF_INET, (void *)hent->h_addr_list[0], ip, iplen) == NULL)	{
				perror("Can't resolve host");
				exit(1);
		}
		return ip;
}

char *build_get_query(char *host, char *page)
{
		char *query;
		char *getpage = page;
		char *tpl = "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
		if(getpage[0] == '/'){
				getpage = getpage + 1;
				fprintf(stderr,"Removing leading \"/\", converting %s to %s\n", page, getpage);
		}
		// -5 is to consider the %s %s %s in tpl and the ending \0
		query = (char *)malloc(strlen(host)+strlen(getpage)+strlen(USERAGENT)+strlen(tpl)-5);
		sprintf(query, tpl, getpage, host, USERAGENT);
		return query;
}

int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
		long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
		result->tv_sec = diff / 1000000;
		result->tv_usec = diff % 1000000;

		return (diff<0);
}
