#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
#include <string.h>

#define DEF_PROCS 2
#define MAX_PROCS 32

#define MAX_LINE_BYTES 65535

#define CHILD_READER  pipe_in_out[0]
#define PARENT_WRITER pipe_in_out[1]

void check_fail(int st, char *err_msg) {
  if (st < 0) {
    perror(err_msg);
    exit(1);
  }
}

void usage() {
  fprintf(stderr,
"usage: pcat [-p num-procs] cmd [cmd-arguments]\n"
"       cmd: stdin line-level parallelism: cmd and any arguments given at the\n"
"             end of the command line are invoked in parallel, passed some\n" 
"             subset of standard input lines.  Any strings containing %%%%\n"
"             will have the process number substituted.\n"
"       -p: specify number of parallel processes (default: %d)\n", DEF_PROCS
  );
  exit(1);
}

int main(int argc, char *argv[]) {
  int num_procs = 0;
  char **cmd, line[MAX_LINE_BYTES], *start, num_buf[3];
  int i, j, max_fd = 0, done, pipe_in_out[2], fd[MAX_PROCS], opt, cmdc;
  pid_t child_pid;
  size_t len;
  fd_set wfds;

  while ((opt = getopt(argc, argv, "hp:")) != -1) {
    switch (opt) {
      case 'p':
        num_procs = atoi(optarg);
        break;
      default:
        usage();
    }
  }

  // clean up & default num_procs arg
  if (!num_procs) num_procs = DEF_PROCS;
  else if (num_procs > MAX_PROCS) num_procs = MAX_PROCS;

  // make sure there's a command
  if (optind >= argc) usage();

  // the rest of the line is the command and its args
  cmd  = argv + optind;
  cmdc = argc - optind;

  for (i = 0; i < num_procs; i++) {
    // create pipe to write to child
    check_fail(pipe(pipe_in_out), "Couldn't create pipe");

    // fork and exec child process
    check_fail(child_pid = fork(), "Couldn't fork");

    if (child_pid == 0) {
      // in child process
      check_fail(close(PARENT_WRITER), "Couldn't close parent writer in child");
      check_fail(dup2(CHILD_READER, STDIN_FILENO), "Couldn't set file desc");

      // replace %% with the 01-<num-procs>
      check_fail(sprintf(num_buf, "%02d", i+1), "formatting num failed");
      for (j = 0; j < cmdc; j++)
        if ((start = strstr(cmd[j], "%%")) != NULL) memcpy(start, num_buf, 2);

      if (execvp(cmd[0], cmd) < 0) {
        perror("Couldn't exec");
        close(CHILD_READER);
        exit(1);
      }
    }

    fd[i] = PARENT_WRITER;
    check_fail(close(CHILD_READER), "Couldn't close child reader in parent");

    // maintain max_fd
    if (PARENT_WRITER > max_fd) max_fd = PARENT_WRITER;
  }
  max_fd++;

  // main stdin-reading loop
  done = 0;
  while (!done) {
    // reset the list of things we're listening for
    FD_ZERO(&wfds);
    for (i = 0; i < num_procs; i++) FD_SET(fd[i], &wfds);

    // block for available writer
    check_fail(select(max_fd, NULL, &wfds, NULL, NULL), "select() failed");

    for (i = 0; !done && i < num_procs; i++) {
      if (!FD_ISSET(fd[i], &wfds)) continue;
      while (1) {
        if (fgets(line, MAX_LINE_BYTES, stdin) == NULL) {
          done = 1;
          break;
        }
        len = strlen(line);
        check_fail(write(fd[i], line, len), "write() failed");
        if (len < MAX_LINE_BYTES-1 || line[MAX_LINE_BYTES-2] == '\n') break;
      }
    }
  }

  for (i = 0; i < num_procs; i++) 
    check_fail(close(fd[i]), "failed to close parent writer");

  return 0;
}
