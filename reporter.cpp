#include "common.h"

void print_tag(const string &name, const string &msg) {
  cerr << "[" << name << " PID=" << getpid() << " PPID=" << getppid() << "] " << msg << endl;
}

int main(int argc, char *argv[]) {
  if (argc < 4)
  {
    print_tag("reporter", "bad args");
    return EXIT_BAD_ARGS;
  }

  string shm_name = argv[1];
  string out_dir = argv[2];
  string sem_name = argv[3];

  sem_t *rep_sem = sem_open(sem_name.c_str(), 0);
  if (rep_sem == SEM_FAILED)
  {
    print_tag("reporter", "sem_open failed");
    return EXIT_IPC_FAIL;
  }

  print_tag("reporter", "waiting for processor");

  if (sem_wait(rep_sem) != 0) {
    print_tag("reporter", "sem_wait failed");
    sem_close(rep_sem);
    return EXIT_IPC_FAIL;
  }

  int shm_fd = shm_open(shm_name.c_str(), O_RDONLY, 0666);
  if (shm_fd < 0) {
    print_tag("reporter", "shm_open failed");
    sem_close(rep_sem);
    return EXIT_IPC_FAIL;
  }

  SharedData *sh = (SharedData *)mmap(0, sizeof(SharedData), PROT_READ, MAP_SHARED, shm_fd, 0);
  if (sh == (void *)-1) {
    print_tag("reporter", "mmap failed");
    close(shm_fd);
    sem_close(rep_sem);
    return EXIT_IPC_FAIL;
  }

  string txt_path = out_dir + "/report.txt";
  string csv_path = out_dir + "/report.csv";

  int fd = open(txt_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (fd < 0) {
    print_tag("reporter", "open report.txt failed");
    munmap(sh, sizeof(SharedData));
    close(shm_fd);
    sem_close(rep_sem);
    return EXIT_IO_FAIL;
  }

  // simple demo for dup + dup2, pehle stdout save then file pe redirect then restore
  int old_out = dup(STDOUT_FILENO);
  dup2(fd, STDOUT_FILENO);

  cout << "Retail Report\n";
  cout << "Total Records: " << sh->total_records << "\n\n";

  for (int i = 0; i < sh->count; i++) {
    cout << "Category: " << sh->records[i].category << "\n";
    cout << "Revenue: " << sh->records[i].total_revenue << "\n";
    cout << "Top Products:\n";

    int shown = 0;
    for (int j = 0; j < sh->records[i].product_count; j++) {
      if (sh->records[i].products[j].revenue < 0) {
        shown++;
        cout << "  " << shown << ") " << sh->records[i].products[j].product
             << " -> " << (-sh->records[i].products[j].revenue) << "\n";

        if (shown == 3)
          break;
      }
    }
    cout << "\n";
  }

  cout.flush();

  dup2(old_out, STDOUT_FILENO);
  close(old_out);
  close(fd);

  ofstream out(csv_path.c_str());
  if (!out.is_open()) {
    print_tag("reporter", "open report.csv failed");
    munmap(sh, sizeof(SharedData));
    close(shm_fd);
    sem_close(rep_sem);
    return EXIT_IO_FAIL;
  }

  out << "category,total_revenue,top1,top2,top3\n";

  for (int i = 0; i < sh->count; i++) {
    out << sh->records[i].category << "," << sh->records[i].total_revenue;

    int shown = 0;
    for (int j = 0; j < sh->records[i].product_count; j++) {
      if (sh->records[i].products[j].revenue < 0) {
        out << "," << sh->records[i].products[j].product;
        shown++;

        if (shown == 3)
          break;
      }
    }

    while (shown < 3) {
      out << ",";
      shown++;
    }

    out << "\n";
  }

  out.close();

  // yeh signal dispatcher ko batata hai ke report ban gayi hai
  kill(getppid(), SIGUSR1);

  munmap(sh, sizeof(SharedData));
  close(shm_fd);
  sem_close(rep_sem);

  print_tag("reporter", "report files written");

  return EXIT_OK;
}
