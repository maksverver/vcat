/* cat with a visual progress bar.

Usage:

	vcat <file...>

Copies the contents of each file argument to standard output, just like cat, but
also displays a progress bar while copying each file. The progress bar is output
to stderr, which must refer to an ANSI terminal.

Standard output must be directed at something other than a TTY.

Example usage:

	% vcat /some/file/path >output
	Copies a file from one location to another.

	% vcat /file/a /file/b /file/c >concatenated
	Concatenates several files.

	% vcat - </some/file/path >output
	Reads from standard input. For the progress bar to be displayed
	correctly, standard input must refer to an open file, not a FIFO or a
	socket. To read a file called "-", use "./-" to open it.

	% vcat /.xyzzy >/dev/null
	Display a progress bar for testing.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FD_IN 0
#define FD_OUT 1
#define FD_ERR 2

#define MAX_WIDTH 9999

#define TRAILER "[ETA %2d:%02d] %3d%%"

/* Terminal window size. Used by update_progress() to determine how wide the
   progress bar should be. */
static struct winsize ws;

/* Current file properties. st.st_size is used by update_progress() to determine
   how much progress has been made (and to estimate the time remaining). */
static struct stat st;  /* Current file properties. */

/* Time we started copying the current file. Initialized by start_progress().
   Used by update_progress() to estimate the time remaining. */
static time_t time_start;

/* Writes `filename` in the buffer of the given size, terminating the result
   with a newline character. If strlen(filename) >= len, then leading path
   components are stripped until strlen(filename) < len. If that's not enough,
   then the filename itself is truncated to len - 1 characters. */
static void write_filename(const char *filename, char *buf, size_t len) {
	size_t n = strlen(filename);
	while (n >= len) {
		const char *p = strchr(filename, '/');
		if (p == NULL) {
			break;
		}
		++p;
		n -= p - filename;
		filename = p;
	}
	snprintf(buf, len, "%s", filename);
}

/* Renders the progress bar, written to standard error. */
static void update_progress(const char *filename, int64_t pos) {
	static char buffer[MAX_WIDTH + 1];

	/* Check terminal size again, in case user resized the window. */
	int previous_width = ws.ws_col;
        if (ioctl(FD_ERR, TIOCGWINSZ, &ws) != 0 || ws.ws_col <= 0 || ws.ws_col > MAX_WIDTH) {
		return;  /* invalid width */
	}
	if (previous_width != ws.ws_col) {
		/* Clear from cursor to end of screen. */
		fputs("\033[0J", stderr);
	}

	/* Calculate progress as a percentage. */
	int percentage = 100;
	if (st.st_size > 0 && pos < st.st_size) {
		percentage = 100 * pos / st.st_size;
	}

	/* Estimate remaining time. */
	int minutes = 99;
	int seconds = 99;
	if (pos >= st.st_size) {
		minutes = seconds = 0;
	} else if (pos > 0) {
		int64_t remaining = (time(NULL) - time_start) * (st.st_size - pos) / pos + 1;
		if (remaining < 6000) {
			minutes = remaining/60;
			seconds = remaining%60;
		}
	}

	/* Fill status line: " /path/to/filename     [ETA 00:00]   0%" */
	memset(buffer, '\0', ws.ws_col);
	int trailer_size = snprintf(NULL, 0, TRAILER, minutes, seconds, percentage);
	if (trailer_size > ws.ws_col) {
		trailer_size = ws.ws_col;
	}
	int filename_size = (int)ws.ws_col - trailer_size - 2;
	if (filename_size > 0) {
		write_filename(filename, buffer + 1, filename_size + 1);
	}
	snprintf(buffer + ws.ws_col - trailer_size, trailer_size + 1, TRAILER, minutes, seconds, percentage);

	/* Select colors. Bright, white foreground. Green background. */
	fputs("\033[1;37;42m", stderr);
	int x = ws.ws_col;  /* cross-over point */
	if (st.st_size > 0) {
		x = (int64_t)x * pos / st.st_size;
	}
	for (int i = 0; i < ws.ws_col; ++i) {
		if (i == x) {
			/* Change background color to blue. */
			fputs("\033[44m", stderr);
		}
		fputc(buffer[i] ? buffer[i] : ' ', stderr);
	}
	/* Return to start of the line. */
	fputc('\r', stderr);
	/* Reset colors. */
	fputs("\033[0m", stderr);
	/* Flush to make sure terminal is up-to-date. */
	fflush(stderr);
}

static void start_progress(const char *filename) {
	time_start = time(NULL);
	update_progress(filename, 0);
}

static void end_progress() {
	fputc('\n', stderr);
}

/* Simulates the copying of a file. Useful for testing. */
static void run_test() {
	const char *filename = "/some/example/filename.xyz";
	int64_t pos = 0;
	st.st_size = 5000000000LL;
	start_progress(filename);
	while (pos < st.st_size) {
		sleep(1);
		pos += 456789012;
		update_progress(filename, pos);
	}
	end_progress();
}

/* Processes the given file, while updating the status bar.
   Returns 0 on success, 1 on error. */
static int cat(const char *filename, int fd) {
	/* Implementation note: CIFS filesystems fail mysteriously reading the last
	   few bytes of large files when we try to read 1 MiB at a time. So instead,
	   we read smaller blocks, and only update the progress bar once for every
	   megabyte. */
	static char buffer[1<<16];  /* 64 KiB */
	start_progress(filename);
	int64_t pos = 0, nread;
	while ((nread = read(fd, buffer, sizeof(buffer))) > 0 &&
			write(FD_OUT, buffer, nread) == nread) {
		int64_t prev_pos = pos;
		pos += nread;
		if (pos >> 20 > prev_pos >> 20) {
			update_progress(filename, pos);
		}
	}
	/* Update a final time, because we may not have hit a 1 MiB boundary in the last iteration. */
	update_progress(filename, pos);
	end_progress();
	if (nread != 0) {
		/* Note: if nread < 0, then read() failed. If nread > 0, then write() failed. */
		perror(filename);
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: vcat <file...>\n");
		return 1;
	}
	if (isatty(FD_OUT)) {
		fprintf(stderr, "Standard output is a TTY!\n");
		return 1;
	}
	if (!isatty(FD_ERR)) {
		fprintf(stderr, "Standard output is not a TTY!\n");
		return 1;
	}
	if (ioctl(FD_ERR, TIOCGWINSZ, &ws) != 0) {
		perror(NULL);
		return 1;
	}
	if (ws.ws_col <= 0 || ws.ws_col > MAX_WIDTH) {
		fprintf(stderr, "Invalid terminal width: %d (max: %d)\n", ws.ws_col, MAX_WIDTH);
		return 1;
	}

	int failed = 0;
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "/.xyzzy") == 0) {
			run_test();
			continue;
		}
		if (strcmp(argv[i], "-") == 0) {
			failed |= cat("<stdin>", FD_IN);
			continue;
		}
		if (stat(argv[i], &st) != 0) {
			perror(argv[i]);
			failed = 1;
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			fprintf(stderr, "%s: Is a directory.\n", argv[i]);
			failed = 1;
			continue;
		}
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			perror(argv[i]);
			failed = 1;
			continue;
		}
		failed |= cat(argv[i], fd);
		close(fd);
	}
	return failed;
}
