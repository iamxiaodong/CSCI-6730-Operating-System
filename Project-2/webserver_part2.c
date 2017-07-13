#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/unistd.h>
#include <arpa/inet.h>
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#ifdef THPOOL_DEBUG
#define THPOOL_DEBUG 1
#else
#define THPOOL_DEBUG 0
#endif

#if !defined(DISABLE_PRINT) || defined(THPOOL_DEBUG)
#define err(str) fprintf(stderr, str)
#else
#define err(str)
#endif

static volatile int threads_keepalive;
static volatile int threads_on_hold;

#define SERVER "webserver/1.0"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
/* =================================== API ======================================= */
typedef struct thpool_* threadpool;

// Original functions
void helper(int s, int sock, struct sockaddr_in peer, int peer_len);

char *get_mime_type(char *name) {
		char *ext = strrchr(name, '.');
		if (!ext) return NULL;
		if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
		if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
		if (strcmp(ext, ".gif") == 0) return "image/gif";
		if (strcmp(ext, ".png") == 0) return "image/png";
		if (strcmp(ext, ".css") == 0) return "text/css";
		if (strcmp(ext, ".au") == 0) return "audio/basic";
		if (strcmp(ext, ".wav") == 0) return "audio/wav";
		if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
		if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
		if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
		return NULL;
}

void send_headers(FILE *f, int status, char *title, char *extra, char *mime, 
				int length, time_t date) {
		time_t now;
		char timebuf[128];

		fprintf(f, "%s %d %s\r\n", PROTOCOL, status, title);
		fprintf(f, "Server: %s\r\n", SERVER);
		now = time(NULL);
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
		fprintf(f, "Date: %s\r\n", timebuf);
		if (extra) fprintf(f, "%s\r\n", extra);
		if (mime) fprintf(f, "Content-Type: %s\r\n", mime);
		if (length >= 0) fprintf(f, "Content-Length: %d\r\n", length);
		if (date != -1) {
				strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&date));
				fprintf(f, "Last-Modified: %s\r\n", timebuf);
		}
		fprintf(f, "Connection: close\r\n");
		fprintf(f, "\r\n");
}

void send_error(FILE *f, int status, char *title, char *extra, char *text) {
		send_headers(f, status, title, extra, "text/html", -1, -1);
		fprintf(f, "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n", status, title);
		fprintf(f, "<BODY><H4>%d %s</H4>\r\n", status, title);
		fprintf(f, "%s\r\n", text);
		fprintf(f, "</BODY></HTML>\r\n");
}

void send_file(FILE *f, char *path, struct stat *statbuf) {
		char data[4096];
		int n;

		FILE *file = fopen(path, "r");
		if (!file) {
				send_error(f, 403, "Forbidden", NULL, "Access denied.");
		} else {
				int length = S_ISREG(statbuf->st_mode) ? statbuf->st_size : -1;
				send_headers(f, 200, "OK", NULL, get_mime_type(path), length, statbuf->st_mtime);

				while ((n = fread(data, 1, sizeof(data), file)) > 0) fwrite(data, 1, n, f);
				fclose(file);
		}
}

int process(FILE *f) {
		char buf[4096];
		char *method;
		char *_path;
		char path[4096];
		char *protocol;
		struct stat statbuf;
		char pathbuf[4096];
		char cwd[1024];
		int len;
		struct sockaddr_in peer;
		int peer_len = sizeof(peer);

		if (!fgets(buf, sizeof(buf), f)) return -1;
		if(getpeername(fileno(f), (struct sockaddr*) &peer, &peer_len) != -1) {
				printf("[pid %d, tid %d] (from %s:%d) URL: %s", getpid(), syscall(__NR_gettid)-getpid(),inet_ntoa(peer.sin_addr), (int)ntohs(peer.sin_port), buf);
		} else {
				printf("[pid %d, tid %d] URL: %s", getpid(), syscall(__NR_gettid)-getpid(), buf);
		}

		method = strtok(buf, " ");
		_path = strtok(NULL, " ");
		protocol = strtok(NULL, "\r");
		if (!method || !_path || !protocol) return -1;

		getcwd(cwd, sizeof(cwd));
		sprintf(path, "%s%s", cwd, _path);

		fseek(f, 0, SEEK_CUR); // Force change of stream direction

		if (strcasecmp(method, "GET") != 0) {
				send_error(f, 501, "Not supported", NULL, "Method is not supported.");
				printf("[pid %d, tid %d] Reply: %s", getpid(), syscall(__NR_gettid)-getpid(), "Method is not supported.\n");
		} else if (stat(path, &statbuf) < 0) {
				send_error(f, 404, "Not Found", NULL, "File not found.");
				printf("[pid %d, tid %d] Reply: File not found - %s", getpid(), syscall(__NR_gettid)-getpid(), path);
		} else if (S_ISDIR(statbuf.st_mode)) {
				len = strlen(path);
				if (len == 0 || path[len - 1] != '/') {
						snprintf(pathbuf, sizeof(pathbuf), "Location: %s/", path);
						send_error(f, 302, "Found", pathbuf, "Directories must end with a slash.");
						printf("[pid %d, tid %d] Reply: %s", getpid(), syscall(__NR_gettid)-getpid(), "Directories mush end with a slash.\n");
				} else {
						snprintf(pathbuf, sizeof(pathbuf), "%s%sindex.html",cwd, path);
						if (stat(pathbuf, &statbuf) >= 0) {
								send_file(f, pathbuf, &statbuf);
								printf("[pid %d, tid %d] Reply: filesend %s\n", getpid(), syscall(__NR_gettid)-getpid(), pathbuf);
						} else {
								DIR *dir;
								struct dirent *de;

								send_headers(f, 200, "OK", NULL, "text/html", -1, statbuf.st_mtime);
								fprintf(f, "<HTML><HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n<BODY>", path);
								fprintf(f, "<H4>Index of %s</H4>\r\n<PRE>\n", path);
								fprintf(f, "Name                             Last Modified              Size\r\n");
								fprintf(f, "<HR>\r\n");
								if (len > 1) fprintf(f, "<A HREF=\"..\">..</A>\r\n");

								dir = opendir(path);
								while ((de = readdir(dir)) != NULL) {
										char timebuf[32];
										struct tm *tm;

										strcpy(pathbuf, path);
										strcat(pathbuf, de->d_name);

										stat(pathbuf, &statbuf);
										tm = gmtime(&statbuf.st_mtime);
										strftime(timebuf, sizeof(timebuf), "%d-%b-%Y %H:%M:%S", tm);

										fprintf(f, "<A HREF=\"%s%s\">", de->d_name, S_ISDIR(statbuf.st_mode) ? "/" : "");
										fprintf(f, "%s%s", de->d_name, S_ISDIR(statbuf.st_mode) ? "/</A>" : "</A> ");
										if (strlen(de->d_name) < 32) fprintf(f, "%*s", 32 - strlen(de->d_name), "");
										if (S_ISDIR(statbuf.st_mode)) {
												fprintf(f, "%s\r\n", timebuf);
										} else {
												fprintf(f, "%s %10d\r\n", timebuf, statbuf.st_size);
										}
								}
								closedir(dir);

								fprintf(f, "</PRE>\r\n<HR>\r\n<ADDRESS>%s</ADDRESS>\r\n</BODY></HTML>\r\n", SERVER);
								printf("[pid %d, tid %d] Reply: SUCCEED\n", getpid(), syscall(__NR_gettid)-getpid());
						}
				}
		} else {
				send_file(f, path, &statbuf);
				printf("[pid %d, tid %d] Reply: filesend %s\n", getpid(), syscall(__NR_gettid)-getpid(), path);
		}

		return 0;
}

/* ==============================================
Functions to implement thread pool
=================================================*/

/* BEGIN */

/* ========================== STRUCTURES ==================== */
/* Binary semaphore */
typedef struct bsem {
	pthread_mutex_t mutex;
	pthread_cond_t   cond;
	int v;
} bsem;


/* Job */
typedef struct job{
	struct job*  prev;                   /* pointer to previous job   */
	void   (*function)(int arg);       /* function pointer          */
	int  arg;                          /* function's argument       */
} job;

/* Job queue */
typedef struct jobqueue{
	pthread_mutex_t rwmutex;             /* used for queue r/w access */
	job  *front;                         /* pointer to front of queue */
	job  *rear;                          /* pointer to rear  of queue */
	bsem *has_jobs;                      /* flag as binary semaphore  */
	int   len;                           /* number of jobs in queue   */
} jobqueue;

/* Thread */
typedef struct thread{
	int       id;                        /* friendly id               */
	pthread_t pthread;                   /* pointer to actual thread  */
	struct thpool_* thpool_p;            /* access to thpool          */
} thread;

/* Threadpool */
typedef struct thpool_{
	thread**   threads;                  /* pointer to threads        */
	volatile int num_threads_alive;      /* threads currently alive   */
	volatile int num_threads_working;    /* threads currently working */
	pthread_mutex_t  thcount_lock;       /* used for thread count etc */
	pthread_cond_t  threads_all_idle;    /* signal to thpool_wait     */
	jobqueue  jobqueue;                  /* job queue                 */
} thpool_;


/* ========================== PROTOTYPES ============================ */
static int  thread_init(thpool_* thpool_p, struct thread** thread_p, int id);
static void* thread_do(struct thread* thread_p);
static void  thread_hold(int sig_id);
static void  thread_destroy(struct thread* thread_p);

static int   jobqueue_init(jobqueue* jobqueue_p);
static void  jobqueue_clear(jobqueue* jobqueue_p);
static void  jobqueue_push(jobqueue* jobqueue_p, struct job* newjob_p);
static struct job* jobqueue_pull(jobqueue* jobqueue_p);
static void  jobqueue_destroy(jobqueue* jobqueue_p);

static void  bsem_init(struct bsem *bsem_p, int value);
static void  bsem_reset(struct bsem *bsem_p);
static void  bsem_post(struct bsem *bsem_p);
static void  bsem_post_all(struct bsem *bsem_p);
static void  bsem_wait(struct bsem *bsem_p);

/* ========================== THREADPOOL ============================ */
/* Initialise thread pool */
struct thpool_* thpool_init(int num_threads){

	threads_on_hold   = 0;
	threads_keepalive = 1;

	if (num_threads < 0){
		num_threads = 0;
	}

	/* Make new thread pool */
	thpool_* thpool_p;
	thpool_p = (struct thpool_*)malloc(sizeof(struct thpool_));
	if (thpool_p == NULL){
		err("thpool_init(): Could not allocate memory for thread pool\n");
		return NULL;
	}
	thpool_p->num_threads_alive   = 0;
	thpool_p->num_threads_working = 0;

	/* Initialise the job queue */
	if (jobqueue_init(&thpool_p->jobqueue) == -1){
		err("thpool_init(): Could not allocate memory for job queue\n");
		free(thpool_p);
		return NULL;
	}

	/* Make threads in pool */
	thpool_p->threads = (struct thread**)malloc(num_threads * sizeof(struct thread *));
	if (thpool_p->threads == NULL){
		err("thpool_init(): Could not allocate memory for threads\n");
		jobqueue_destroy(&thpool_p->jobqueue);
		free(thpool_p);
		return NULL;
	}

	pthread_mutex_init(&(thpool_p->thcount_lock), NULL);
	pthread_cond_init(&thpool_p->threads_all_idle, NULL);

	/* Thread init */
	int n;
	for (n=0; n<num_threads; n++){
		thread_init(thpool_p, &thpool_p->threads[n], n);
			printf("THPOOL_DEBUG: Created thread %d in pool \n", n);
	}

	/* Wait for threads to initialize */
	while (thpool_p->num_threads_alive != num_threads) {}

	return thpool_p;
}

/* Add work to the thread pool */
int thpool_add_work(thpool_* thpool_p, void (*function_p)(int), int arg_p){
	job* newjob;

	newjob=(struct job*)malloc(sizeof(struct job));
	if (newjob==NULL){
		err("thpool_add_work(): Could not allocate memory for new job\n");
		return -1;
	}

	/* add function and argument */
	newjob->function=function_p;
	newjob->arg=arg_p;

	/* add job to queue */
	jobqueue_push(&thpool_p->jobqueue, newjob);

	return 0;
}

/* Wait until all jobs have finished */
void thpool_wait(thpool_* thpool_p){
	pthread_mutex_lock(&thpool_p->thcount_lock);
	while (thpool_p->jobqueue.len || thpool_p->num_threads_working) {
		pthread_cond_wait(&thpool_p->threads_all_idle, &thpool_p->thcount_lock);
	}
	pthread_mutex_unlock(&thpool_p->thcount_lock);
}


/* Destroy the threadpool */
void thpool_destroy(thpool_* thpool_p){
	/* No need to destory if it's NULL */
	if (thpool_p == NULL) return ;

	volatile int threads_total = thpool_p->num_threads_alive;

	/* End each thread 's infinite loop */
	threads_keepalive = 0;

	/* Give one second to kill idle threads */
	double TIMEOUT = 1.0;
	time_t start, end;
	double tpassed = 0.0;
	time (&start);
	while (tpassed < TIMEOUT && thpool_p->num_threads_alive){
		bsem_post_all(thpool_p->jobqueue.has_jobs);
		time (&end);
		tpassed = difftime(end,start);
	}

	/* Poll remaining threads */
	while (thpool_p->num_threads_alive){
		bsem_post_all(thpool_p->jobqueue.has_jobs);
		sleep(1);
	}

	/* Job queue cleanup */
	jobqueue_destroy(&thpool_p->jobqueue);
	/* Deallocs */
	int n;
	for (n=0; n < threads_total; n++){
		thread_destroy(thpool_p->threads[n]);
	}
	free(thpool_p->threads);
	free(thpool_p);
}

/* Pause all threads in threadpool */
void thpool_pause(thpool_* thpool_p) {
	int n;
	for (n=0; n < thpool_p->num_threads_alive; n++){
		pthread_kill(thpool_p->threads[n]->pthread, SIGUSR1);
	}
}

/* Resume all threads in threadpool */
void thpool_resume(thpool_* thpool_p) {
    // resuming a single threadpool hasn't been
    // implemented yet, meanwhile this supresses
    // the warnings
    (void)thpool_p;

	threads_on_hold = 0;
}

int thpool_num_threads_working(thpool_* thpool_p){
	return thpool_p->num_threads_working;
}

/* ============================ THREAD ============================== */
/* Initialize a thread in the thread pool
 *
 * @param thread        address to the pointer of the thread to be created
 * @param id            id to be given to the thread
 * @return 0 on success, -1 otherwise.
 */
static int thread_init (thpool_* thpool_p, struct thread** thread_p, int id){

	*thread_p = (struct thread*)malloc(sizeof(struct thread));
	if (thread_p == NULL){
		err("thread_init(): Could not allocate memory for thread\n");
		return -1;
	}

	(*thread_p)->thpool_p = thpool_p;
	(*thread_p)->id       = id;

	pthread_create(&(*thread_p)->pthread, NULL, (void *)thread_do, (*thread_p));
	pthread_detach((*thread_p)->pthread);
	return 0;
}


/* Sets the calling thread on hold */
static void thread_hold(int sig_id) {
    (void)sig_id;
	threads_on_hold = 1;
	while (threads_on_hold){
		sleep(1);
	}
}

/* What each thread is doing
* In principle this is an endless loop. The only time this loop gets interuppted is once
* thpool_destroy() is invoked or the program exits.
*
* @param  thread        thread that will run this function
* @return nothing
*/
static void* thread_do(struct thread* thread_p){

	/* Set thread name for profiling and debuging */
	char thread_name[128] = {0};
	sprintf(thread_name, "thread-pool-%d", thread_p->id);

#if defined(__linux__)
	/* Use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration */
	prctl(PR_SET_NAME, thread_name);
#elif defined(__APPLE__) && defined(__MACH__)
	pthread_setname_np(thread_name);
#else
	err("thread_do(): pthread_setname_np is not supported on this system");
#endif

	/* Assure all threads have been created before starting serving */
	thpool_* thpool_p = thread_p->thpool_p;

	/* Register signal handler */
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = thread_hold;
	if (sigaction(SIGUSR1, &act, NULL) == -1) {
		err("thread_do(): cannot handle SIGUSR1");
	}

	/* Mark thread as alive (initialized) */
	pthread_mutex_lock(&thpool_p->thcount_lock);
	thpool_p->num_threads_alive += 1;
	pthread_mutex_unlock(&thpool_p->thcount_lock);

	while(threads_keepalive){

		bsem_wait(thpool_p->jobqueue.has_jobs);

		if (threads_keepalive){

			pthread_mutex_lock(&thpool_p->thcount_lock);
			thpool_p->num_threads_working++;
			pthread_mutex_unlock(&thpool_p->thcount_lock);

			/* Read job from queue and execute it */
			void (*func_buff)(int);
			int arg_buff;
			job* job_p = jobqueue_pull(&thpool_p->jobqueue);
			if (job_p) {
				func_buff = job_p->function;
				arg_buff  = job_p->arg;
				func_buff(arg_buff);
				free(job_p);
			}

			pthread_mutex_lock(&thpool_p->thcount_lock);
			thpool_p->num_threads_working--;
			if (!thpool_p->num_threads_working) {
				pthread_cond_signal(&thpool_p->threads_all_idle);
			}
			pthread_mutex_unlock(&thpool_p->thcount_lock);

		}
	}
	pthread_mutex_lock(&thpool_p->thcount_lock);
	thpool_p->num_threads_alive --;
	pthread_mutex_unlock(&thpool_p->thcount_lock);

	return NULL;
}


/* Frees a thread  */
static void thread_destroy (thread* thread_p){
	free(thread_p);
}

/* ============================ JOB QUEUE =========================== */
/* Initialize queue */
static int jobqueue_init(jobqueue* jobqueue_p){
	jobqueue_p->len = 0;
	jobqueue_p->front = NULL;
	jobqueue_p->rear  = NULL;

	jobqueue_p->has_jobs = (struct bsem*)malloc(sizeof(struct bsem));
	if (jobqueue_p->has_jobs == NULL){
		return -1;
	}

	pthread_mutex_init(&(jobqueue_p->rwmutex), NULL);
	bsem_init(jobqueue_p->has_jobs, 0);

	return 0;
}


/* Clear the queue */
static void jobqueue_clear(jobqueue* jobqueue_p){

	while(jobqueue_p->len){
		free(jobqueue_pull(jobqueue_p));
	}

	jobqueue_p->front = NULL;
	jobqueue_p->rear  = NULL;
	bsem_reset(jobqueue_p->has_jobs);
	jobqueue_p->len = 0;

}

/* Add (allocated) job to queue */
static void jobqueue_push(jobqueue* jobqueue_p, struct job* newjob){

	pthread_mutex_lock(&jobqueue_p->rwmutex);
	newjob->prev = NULL;

	switch(jobqueue_p->len){

		case 0:  /* if no jobs in queue */
					jobqueue_p->front = newjob;
					jobqueue_p->rear  = newjob;
					break;

		default: /* if jobs in queue */
					jobqueue_p->rear->prev = newjob;
					jobqueue_p->rear = newjob;

	}
	jobqueue_p->len++;

	bsem_post(jobqueue_p->has_jobs);
	pthread_mutex_unlock(&jobqueue_p->rwmutex);
}


/* Get first job from queue(removes it from queue)
 * Notice: Caller MUST hold a mutex
==================================================*/
static struct job* jobqueue_pull(jobqueue* jobqueue_p){

	pthread_mutex_lock(&jobqueue_p->rwmutex);
	job* job_p = jobqueue_p->front;

	switch(jobqueue_p->len){

		case 0:  /* if no jobs in queue */
		  			break;

		case 1:  /* if one job in queue */
					jobqueue_p->front = NULL;
					jobqueue_p->rear  = NULL;
					jobqueue_p->len = 0;
					break;

		default: /* if >1 jobs in queue */
					jobqueue_p->front = job_p->prev;
					jobqueue_p->len--;
					/* more than one job in queue -> post it */
					bsem_post(jobqueue_p->has_jobs);

	}

	pthread_mutex_unlock(&jobqueue_p->rwmutex);
	return job_p;
}


/* Free all queue resources back to the system */
static void jobqueue_destroy(jobqueue* jobqueue_p){
	jobqueue_clear(jobqueue_p);
	free(jobqueue_p->has_jobs);
}


/* ======================== SYNCHRONISATION ========================= */
/* Init semaphore to 1 or 0 */
static void bsem_init(bsem *bsem_p, int value) {
	if (value < 0 || value > 1) {
		err("bsem_init(): Binary semaphore can take only values 1 or 0");
		exit(1);
	}
	pthread_mutex_init(&(bsem_p->mutex), NULL);
	pthread_cond_init(&(bsem_p->cond), NULL);
	bsem_p->v = value;
}

/* Reset semaphore to 0 */
static void bsem_reset(bsem *bsem_p) {
	bsem_init(bsem_p, 0);
}

/* Post to at least one thread */
static void bsem_post(bsem *bsem_p) {
	pthread_mutex_lock(&bsem_p->mutex);
	bsem_p->v = 1;
	pthread_cond_signal(&bsem_p->cond);
	pthread_mutex_unlock(&bsem_p->mutex);
}

/* Post to all threads */
static void bsem_post_all(bsem *bsem_p) {
	pthread_mutex_lock(&bsem_p->mutex);
	bsem_p->v = 1;
	pthread_cond_broadcast(&bsem_p->cond);
	pthread_mutex_unlock(&bsem_p->mutex);
}


/* Wait on semaphore until semaphore has value 0 */
static void bsem_wait(bsem* bsem_p) {
	pthread_mutex_lock(&bsem_p->mutex);
	while (bsem_p->v != 1) {
		pthread_cond_wait(&bsem_p->cond, &bsem_p->mutex);
	}
	bsem_p->v = 0;
	pthread_mutex_unlock(&bsem_p->mutex);
}

/* END */

void handler(int s) {	
	FILE *f;
	f = fdopen(s, "a+");
	process(f);
	fclose(f);
}

int listener(int port, int Num)
{
		int sock;
		struct sockaddr_in sin;
		struct sockaddr_in peer;
		int peer_len = sizeof(peer);

		sock = socket(AF_INET, SOCK_STREAM, 0);

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = INADDR_ANY;
		sin.sin_port = htons(port);
		bind(sock, (struct sockaddr *) &sin, sizeof(sin));

		listen(sock, 5);
		printf("HTTP server listening on port %d\n", port);

		// let's initialize the thread pool here
		threadpool thpool = thpool_init(Num);

		while (1) {
			int s;
			s = accept(sock, NULL, NULL);
			if(getpeername(s, (struct sockaddr*) &peer, &peer_len) != -1) {
				printf("[pid %d, tid %d] Received a request from %s:%d\n", getpid(), syscall(__NR_gettid) - getpid(), inet_ntoa(peer.sin_addr), (int)ntohs(peer.sin_port));
			}

			//pthread_t thread1;
			if (getpeername(s, (struct sockaddr*) &peer, &peer_len) != -1)
			{
				thpool_add_work(thpool, (void*)handler, s);
			}
		}
		thpool_destroy(thpool);
		close(sock);
}

int main(int argc, char *argv[]) {
		if(argc < 3 || atoi(argv[1]) < 2000 || atoi(argv[1]) > 50000)
		{
				fprintf(stderr, "./webserver PORT(2001 ~ 49999)\n");
				return 0;
		}

		int port = atoi(argv[1]);
		int N = atoi(argv[2]);
		if(N > 100){
			N = 100;
		}
		listener(port, N);

		return 0;
}
