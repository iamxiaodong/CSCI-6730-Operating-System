/*************************************************
	* C program to count no of lines, words and 	 *
	* characters in a file.			 *
	By Xiaodong Jiang
	Feb. 5, 2017
	Department of Statistis
	*************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define FILEPATH "/tmp/CSCI4730/books"

typedef struct count_t {
	int linecount;
	int wordcount;
	int charcount;
} count_t;

count_t word_count(char *file) {
	//printf("in  in  in  \n");
	FILE *fp;
	char ch;

	count_t count;
	// Initialize counter variables
	count.linecount = 0;
	count.wordcount = 0;
	count.charcount = 0;

	// Open file in read-only mode
	//printf("file name: %s\n", file);
	fp = fopen(file, "r");
	// If file opened successfully, then write the string to file
	if (fp) {
		//Repeat until End Of File character is reached.
		while ((ch = getc(fp)) != EOF) {
			// Increment character count if NOT new line or space
			if (ch != ' ' && ch != '\n') { ++count.charcount; }

			// Increment word count if new line or space character
			if (ch == ' ' || ch == '\n') { ++count.wordcount; }

			// Increment line count if new line character
			if (ch == '\n') { ++count.linecount; }
		}

		if (count.charcount > 0) {
			++count.linecount;
			++count.wordcount;
		}

		fclose(fp);
	} else {
		printf("Failed to open the file: %s\n", file);
	}

	return count;
}

#define MAX_PROC 100

int main(int argc, char **argv) {
	int i, numFiles;
	char filename[100];
	count_t count, tmp;

	if (argc < 2) {
		printf("usage: wc <# of files to count(1-10)>\n");
		return 0;
	}

	numFiles = atoi(argv[1]);
	if (numFiles <= 0 || numFiles > 10) {
		printf("usage: wc <# of files to count(1-10)>\n");
		return 0;
	}

	count.linecount = 0;
	count.wordcount = 0;
	count.charcount = 0;

    printf("=========================================\n");
	printf("counting %d files..\n", numFiles);
    
	int data_processed = 0;
	pid_t pid;

	// create pipe pair
	int fp[2];
	if (pipe(fp) == 0) {
		//create pipe
		//fork children process
		for (i = 0; i < numFiles; i++) {
			pid = fork();
			if (pid == -1) {
				fprintf(stderr, "Fork failure");
				exit(EXIT_FAILURE);
			}
			if (pid == 0) {

				printf("*Child[%d] is create \n", getpid());
				sprintf(filename, "%s/text.%02d", FILEPATH, i);
				printf("read: %s\n", filename);

				tmp = word_count(filename);
				printf("Child[%d] result: chars count is %d,lines count is %d,words count is %d\n", getpid(), tmp.charcount, tmp.linecount,
					   tmp.wordcount);
				close(fp[0]);

				data_processed = write(fp[1], &tmp, sizeof(tmp));
				printf("Child[%d] is exit. \n", getpid());
				exit(EXIT_SUCCESS);
			}
        }

		close(fp[1]);
		int j;
		
		for (j = 0; j < numFiles; j++) {
			count_t kk[1];
			data_processed = read(fp[0], kk, sizeof(kk));
			count.charcount += kk->charcount;
			count.linecount += kk->linecount;
			count.wordcount += kk->wordcount;
		}
		//exit(EXIT_SUCCESS);
	}


	printf("=========================================\n");
	printf("Total Lines : %d \n", count.linecount);
	printf("Total Words : %d \n", count.wordcount);
	printf("Total Characters : %d \n", count.charcount);
	printf("=========================================\n");

	return (0);
}
