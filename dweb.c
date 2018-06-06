#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define LEN(a) (sizeof(a)/sizeof(*(a)))

static int chatty;		/* set if isatty(STDIN) */

static const char *argv0;
static const char *browser;	/* $BROWSER or "w3m" by default */
static const char *pager;	/* $PAGER or "more" by default */

static char *links[512];

/*
 * Extracts links in the format "[1] http://example.com", assigning the
 * appropriate index in the links array with a copy of the URL.
 */
static void
extract_link(char *line)
{
	long idx;
	char *rest;
	char *link;
	size_t len;

	if (line[0] != '[' || !line[1])
		return;
	
	idx = strtol(&line[1], &rest, 10);
	if (idx < 0 || (size_t)idx >= LEN(links))
		return;
	if (rest[0] != ']' || rest[1] != ' ' || !rest[2])
		return;

	if (!(link = strdup(&rest[2]))) {
		perror(argv0);
		return;
	}

	len = strlen(link);
	if (link[len-1] == '\n')
		link[len-1] = '\0';

	if (links[idx])
		free(links[idx]);

	links[idx] = link;
}

/*
 * Starts $BROWSER with dump flags (see inside), returning a read handle to
 * the standard output, or NULL on error (setting errno).
 */
static FILE *
open_browser(pid_t *pid, char *url)
{
	int out_fds[2];
	int ret;
	int errno_saved;
	FILE *f;

	if (pipe(out_fds) == -1)
		return NULL;

	switch ((*pid = fork())) {
	case -1:
		errno_saved = errno;
		close(out_fds[0]);
		close(out_fds[1]);
		errno = errno_saved;
		return NULL;
	case 0:
		dup2(out_fds[1], STDOUT_FILENO);
		close(out_fds[0]);
		close(out_fds[1]);
		ret = execlp(browser, browser, "-dump", "-o",
		    "display_link_num=1", url, NULL);
		if (ret == -1) {
			perror(argv0);
			exit(1);
		}
		exit(0);
	}

	close(out_fds[1]);

	if (!(f = fdopen(out_fds[0], "r"))) {
		errno_saved = errno;
		close(out_fds[0]);
		waitpid(*pid, NULL, 0);
		errno = errno_saved;
		return NULL;
	}

	return f;
}

/*
 * Starts $PAGER, returning a write handle to its standard input, or NULL on
 * failure (setting errno). Closes `toclose` in the child process, if set.
 */
static FILE *
open_pager(pid_t *pid, FILE *toclose)
{
	int in_fds[2];
	int errno_saved;
	FILE *f;

	if (pipe(in_fds) == -1)
		return NULL;

	switch ((*pid = fork())) {
	case -1:
		errno_saved = errno;
		close(in_fds[0]);
		close(in_fds[1]);
		errno = errno_saved;
		return NULL;
	case 0:
		dup2(in_fds[0], STDIN_FILENO);
		close(in_fds[0]);
		close(in_fds[1]);
		if (toclose)
			fclose(toclose);
		if (execlp(pager, pager, NULL) == -1) {
			perror(argv0);
			exit(1);
		}
		exit(0);
	}

	close(in_fds[0]);

	if (!(f = fdopen(in_fds[1], "w"))) {
		close(in_fds[1]);
		waitpid(*pid, NULL, 0);
		return NULL;
	}

	return f;
}

/*
 * Displays the given URL with $BROWSER through $PAGER, putting itself in the
 * pipe to intercept links, storing them to the `links` global. Existing links
 * are cleared.
 */
static void
browse(char *url)
{
	pid_t browser_pid;
	pid_t pager_pid;
	FILE *browser_f;
	FILE *pager_f;
	char *line = NULL;
	size_t linecap = 0;
	size_t i;

	if (!(browser_f = open_browser(&browser_pid, url))) {
		perror(argv0);
		return;
	}

	if (!(pager_f = open_pager(&pager_pid, browser_f))) {
		perror(argv0);
		fclose(browser_f);
		wait(NULL);
		return;
	}

	for (i = 0; i < LEN(links); i++) {
		if (links[i]) {
			free(links[i]);
			links[i] = NULL;
		}
	}

	url = NULL; /* may be invalid after freeing links */

	while (getline(&line, &linecap, browser_f) != -1) {
		extract_link(line);
		fputs(line, pager_f);
	}

	if (ferror(browser_f) || ferror(pager_f))
		perror(NULL);

	fclose(browser_f);
	fclose(pager_f);
	waitpid(browser_pid, NULL, 0);
	waitpid(pager_pid, NULL, 0);
}

int
main(int argc, char **argv)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	long link_idx;
	char *endptr;

	(void)argc;

	chatty = isatty(STDIN_FILENO);
	argv0 = argv[0];

	if (chatty) {
		puts("dweb usage:\n"
		     " <url>     go to URL\n"
		     " <number>  follow link\n"
		     " q         quit\n");
	}

	if (!(browser = getenv("BROWSER")))
		browser = "w3m";
	if (!(pager = getenv("PAGER")))
		pager = "more";

	while (1) {
		if (chatty)
			fputs("> ", stdout);
		if ((len = getline(&line, &cap, stdin)) == -1)
			break;
		if (line[len-1] == '\n')
			line[len-1] = '\0';
		if (!line[0])
			continue;

		if (!strcmp(line, "q"))
			break;

		link_idx = strtol(line, &endptr, 10);
		if (*endptr == '\0') {
			if (link_idx < 0 || (size_t)link_idx >= LEN(links))
				fprintf(stderr, "%s: index out of range\n\n",
				    argv0);
			else if (!links[link_idx])
				fprintf(stderr, "%s: no such link\n\n",
				    argv0);
			else {
				if (chatty)
					printf("(%s)\n", links[link_idx]);
				browse(links[link_idx]);	
			}
		} else
			browse(line);

		if (chatty)
			putchar('\n');
	}

	if (ferror(stdin)) {
		perror(argv0);
		return 1;
	}

	return 0;
}
