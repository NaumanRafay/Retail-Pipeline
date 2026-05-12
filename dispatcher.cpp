#include "common.h"

pid_t child1 = -1;
pid_t child2 = -1;
pid_t child3 = -1;

const char *fifo_path = 0;
const char *shm_name = 0;
const char *sem_name = "/retail_rep_sem";

volatile sig_atomic_t got_term = 0;
volatile sig_atomic_t term_sig = 0;

void print_tag(const string &name, const string &msg) {
  cerr << "[" << name << " PID=" << getpid() << " PPID=" << getppid() << "] " << msg << endl;
}

void cleanup() {
  if (fifo_path)
    unlink(fifo_path);
  if (shm_name)
    shm_unlink(shm_name);
  sem_unlink(sem_name);
}

void stop_children() {
  if (child1 > 0)
    kill(child1, SIGTERM);
  if (child2 > 0)
    kill(child2, SIGTERM);
  if (child3 > 0)
    kill(child3, SIGTERM);
}

void handle_stop(int sig) {
  got_term = 1;
  term_sig = sig;
  stop_children();
}

void handle_usr1(int) {
  print_tag("dispatcher", "report ready signal received");
}

int main(int argc, char *argv[]) {
  if (argc < 6)
  {
    cerr << "dispatcher bad args" << endl;
    return EXIT_BAD_ARGS;
  }

  char *input_dir = argv[1];
  char *output_dir = argv[2];
  char *threads = argv[3];
  fifo_path = argv[4];
  shm_name = argv[5];

  signal(SIGINT, handle_stop);
  signal(SIGTERM, handle_stop);
  signal(SIGUSR1, handle_usr1);
  atexit(cleanup);
  print_tag("dispatcher", "starting pipeline");

  unlink(fifo_path);
  if (mkfifo(fifo_path, 0666) < 0)
  {
    print_tag("dispatcher", "mkfifo failed");
    return EXIT_IPC_FAIL;
  }

  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd < 0) {
    print_tag("dispatcher", "shm_open failed");
    cleanup();
    return EXIT_IPC_FAIL;
  }

  if (ftruncate(shm_fd, sizeof(SharedData)) < 0) {
    print_tag("dispatcher", "ftruncate failed");
    close(shm_fd);
    cleanup();
    return EXIT_IPC_FAIL;
  }
  close(shm_fd);

  sem_unlink(sem_name);
  sem_t *s = sem_open(sem_name, O_CREAT, 0666, 0);
  if (s == SEM_FAILED) {
    print_tag("dispatcher", "sem_open failed");
    cleanup();
    return EXIT_IPC_FAIL;
  }
  sem_close(s);

  mkdir("logs", 0777);

  child1 = fork();
  if (child1 < 0)
  {
    print_tag("dispatcher", "fork ingester failed");
    cleanup();
    return EXIT_CHILD_FAIL;
  }
  if (child1 == 0) {
    int fd = open("logs/ingester.log", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    char *args[] = {(char *)"./ingester", input_dir, (char *)fifo_path, 0};
    execvp(args[0], args);
    _exit(EXIT_CHILD_FAIL);
  }

  child2 = fork();
  if (child2 < 0)
  {
    print_tag("dispatcher", "fork processor failed");
    stop_children();
    cleanup();
    return EXIT_CHILD_FAIL;
  }
  if (child2 == 0) {
    int fd = open("logs/processor.log", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    char *args[] = {(char *)"./processor", (char *)fifo_path, threads, (char *)shm_name, (char *)sem_name, 0};
    execvp(args[0], args);
    _exit(EXIT_CHILD_FAIL);
  }

  child3 = fork();
  if (child3 < 0)
  {
    print_tag("dispatcher", "fork reporter failed");
    stop_children();
    cleanup();
    return EXIT_CHILD_FAIL;
  }
  if (child3 == 0) {
    int fd = open("logs/reporter.log", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    char *args[] = {(char *)"./reporter", (char *)shm_name, output_dir, (char *)sem_name, 0};
    execvp(args[0], args);
    _exit(EXIT_CHILD_FAIL);
  }

  int status = 0;
  waitpid(child1, &status, 0);
  waitpid(child2, &status, 0);
  waitpid(child3, &status, 0);

  cleanup();
  print_tag("dispatcher", "all children finished");

  if (got_term && term_sig == SIGINT)
    return EXIT_SIGINT_CODE;
  if (got_term && term_sig == SIGTERM)
    return EXIT_SIGTERM_CODE;

  return EXIT_OK;
}
