#include "common.h"

volatile sig_atomic_t stop_now = 0;
volatile sig_atomic_t show_stats = 0;
int files_processed = 0;
int chunks_sent = 0;
int bytes_sent = 0;

void on_term(int sig) {
  stop_now = sig;
}

void on_usr1(int) {
  show_stats = 1;
}

void print_tag(const string &name, const string &msg) {
  cerr << "[" << name << " PID=" << getpid() << " PPID=" << getppid() << "] " << msg << endl;
}

bool write_all_local(int fd, const void *buf, int n) {
  const char *p = (const char *)buf;
  int left = n;

  while (left > 0) {
    int w = write(fd, p, left);
    if (w <= 0)
      return false;
    left -= w;
    p += w;
  }

  return true;
}

int main(int argc, char *argv[]) {
  if (argc < 3)
  {
    cerr << "ingester bad args" << endl;
    return EXIT_BAD_ARGS;
  }

  string input_dir = argv[1];
  string fifo_path = argv[2];

  signal(SIGINT, on_term);
  signal(SIGTERM, on_term);
  signal(SIGUSR1, on_usr1);

  string input_file = input_dir + "/input.txt";
  ifstream list(input_file.c_str());
  if (!list.is_open()) {
    print_tag("ingester", "input.txt not found");
    return EXIT_IO_FAIL;
  }

  vector<string> files;
  string name;

  while (getline(list, name)) {
    if (name.size() == 0)
      continue;
    files.push_back(name);
  }
  list.close();

  if (files.size() == 0)
  {
    print_tag("ingester", "input.txt empty");
    return EXIT_IO_FAIL;
  }

  int fd = open(fifo_path.c_str(), O_WRONLY);
  if (fd < 0)
  {
    print_tag("ingester", "fifo open failed");
    return EXIT_IO_FAIL;
  }

  print_tag("ingester", "started reading csv files");

  int chunk_id = 1;
  int file_id = 0;

  for (int i = 0; i < (int)files.size(); i++) {
    if (show_stats) {
      show_stats = 0;
      cerr << "[ingester PID=" << getpid() << " PPID=" << getppid() << "] "
           << "stats files=" << files_processed
           << " chunks=" << chunks_sent
           << " bytes=" << bytes_sent << endl;
    }

    if (stop_now)
      break;

    string full = input_dir + "/" + files[i];
    ifstream fin(full.c_str());
    if (!fin.is_open())
      continue;

    print_tag("ingester", "processing file: " + files[i]);
    files_processed++;

    string line;

    while (!stop_now) {
      Chunk c;
      c.chunk_id = chunk_id++;
      c.byte_count = 0;
      c.source_file_id = file_id;
      c.is_eof = 0;
      memset(c.data, 0, CHUNK_SIZE);

      int pos = 0;
      int lines = 0;

      while (getline(fin, line)) {
        if ((int)line.size() + 1 + pos >= CHUNK_SIZE)
          break;

        for (int k = 0; k < (int)line.size(); k++)
          c.data[pos++] = line[k];

        c.data[pos++] = '\n';
        lines++;

        if (lines >= 20)
          break;
      }

      c.byte_count = pos;

      if (c.byte_count == 0)
        break;

      if (!write_all_local(fd, &c, sizeof(Chunk))) {
        print_tag("ingester", "write to fifo failed");
        close(fd);
        return EXIT_IO_FAIL;
      }
      chunks_sent++;
      bytes_sent += c.byte_count;

      if (fin.eof())
        break;
    }

    fin.close();
    file_id++;
  }

  Chunk endc;
  memset(&endc, 0, sizeof(endc));
  endc.is_eof = 1;
  endc.chunk_id = chunk_id;
  if (!write_all_local(fd, &endc, sizeof(Chunk)))
    print_tag("ingester", "warning: eof chunk write failed");

  close(fd);

  print_tag("ingester", "finished");

  if (stop_now == SIGINT)
    return EXIT_SIGINT_CODE;
  if (stop_now == SIGTERM)
    return EXIT_SIGTERM_CODE;

  return EXIT_OK;
}
