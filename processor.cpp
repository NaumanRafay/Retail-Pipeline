#include "common.h"

struct QueueItem {
  int is_poison;
  Chunk chunk;
};

QueueItem qbuf[MAX_QUEUE];
int q_size = 50;
int q_head = 0;
int q_tail = 0;

sem_t sem_empty;
sem_t sem_full;
pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;

CategoryRecord table_data[MAX_CATEGORIES];
int table_count = 0;
int total_records = 0;
pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

int fifo_fd = -1;
int worker_count = 2;

void print_tag(const string &name, const string &msg) {
  cerr << "[" << name << " PID=" << getpid() << " PPID=" << getppid() << "] " << msg << endl;
}

bool read_all_local(int fd, void *buf, int n) {
  char *p = (char *)buf;
  int left = n;

  while (left > 0) {
    int r = read(fd, p, left);
    if (r <= 0)
      return false;

    left -= r;
    p += r;
  }

  return true;
}

int to_int(const string &s) {
  stringstream ss(s);
  int x = 0;
  ss >> x;
  return x;
}

float to_float(const string &s) {
  stringstream ss(s);
  float x = 0;
  ss >> x;
  return x;
}

vector<string> split_line(const string &line) {
  vector<string> out;
  string cur;

  for (int i = 0; i < (int)line.size(); i++) {
    if (line[i] == ',') {
      out.push_back(cur);
      cur = "";
    }
    else {
      cur += line[i];
    }
  }

  out.push_back(cur);
  return out;
}

void queue_push(QueueItem item) {
  sem_wait(&sem_empty);

  pthread_mutex_lock(&q_mutex);
  qbuf[q_tail] = item;
  q_tail = (q_tail + 1) % q_size;
  pthread_mutex_unlock(&q_mutex);

  sem_post(&sem_full);
}

QueueItem queue_pop() {
  QueueItem item;

  sem_wait(&sem_full);

  pthread_mutex_lock(&q_mutex);
  item = qbuf[q_head];
  q_head = (q_head + 1) % q_size;
  pthread_mutex_unlock(&q_mutex);

  sem_post(&sem_empty);
  return item;
}

int find_category(const string &cat) {
  for (int i = 0; i < table_count; i++) {
    if (cat == table_data[i].category)
      return i;
  }

  return -1;
}

int find_product(int ci, const string &prod) {
  for (int i = 0; i < table_data[ci].product_count; i++) {
    if (prod == table_data[ci].products[i].product)
      return i;
  }

  return -1;
}

void add_record(const string &cat, const string &prod, float rev) {
  pthread_mutex_lock(&table_mutex);

  int ci = find_category(cat);
  if (ci == -1 && table_count < MAX_CATEGORIES) {
    ci = table_count;
    memset(&table_data[ci], 0, sizeof(CategoryRecord));
    strncpy(table_data[ci].category, cat.c_str(), 63);
    table_data[ci].product_count = 0;
    table_data[ci].total_revenue = 0;
    table_count++;
  }

  if (ci != -1) {
    table_data[ci].total_revenue += rev;

    int pi = find_product(ci, prod);
    if (pi == -1 && table_data[ci].product_count < MAX_PRODUCTS) {
      pi = table_data[ci].product_count;
      memset(&table_data[ci].products[pi], 0, sizeof(ProductRecord));
      strncpy(table_data[ci].products[pi].product, prod.c_str(), 63);
      table_data[ci].products[pi].revenue = 0;
      table_data[ci].product_count++;
    }

    if (pi != -1)
      table_data[ci].products[pi].revenue += rev;
  }

  total_records++;
  pthread_mutex_unlock(&table_mutex);
}

void parse_chunk(const Chunk &c) {
  string data(c.data, c.byte_count);
  stringstream ss(data);
  string line;

  while (getline(ss, line)) {
    if (line.size() == 0)
      continue;

    vector<string> p = split_line(line);
    if (p.size() < 4)
      continue;

    string category = p[0];
    string product = p[1];
    float price = to_float(p[2]);
    float qty = to_float(p[3]);
    float revenue = price * qty;

    add_record(category, product, revenue);
  }
}

void *reader_fn(void *) {
  while (1) {
    Chunk c;

    if (!read_all_local(fifo_fd, &c, sizeof(Chunk)))
      break;

    if (c.is_eof) {
      // yahan poison daal rahe hain taake har worker aram se exit kare
      for (int i = 0; i < worker_count; i++) {
        QueueItem p;
        p.is_poison = 1;
        memset(&p.chunk, 0, sizeof(Chunk));
        queue_push(p);
      }
      break;
    }

    QueueItem item;
    item.is_poison = 0;
    item.chunk = c;
    queue_push(item);
  }

  return 0;
}

void *worker_fn(void *) {
  while (1) {
    QueueItem item = queue_pop();

    if (item.is_poison)
      break;

    parse_chunk(item.chunk);
  }

  return 0;
}

void mark_top3(CategoryRecord &r) {
  for (int i = 0; i < TOP_N; i++) {
    int best = -1;

    for (int j = 0; j < r.product_count; j++) {
      if (r.products[j].revenue < 0)
        continue;

      if (best == -1 || r.products[j].revenue > r.products[best].revenue)
        best = j;
    }

    if (best == -1)
      break;

    r.products[best].revenue = -r.products[best].revenue;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 5)
  {
    cerr << "processor bad args" << endl;
    return EXIT_BAD_ARGS;
  }

  string fifo_path = argv[1];
  worker_count = to_int(argv[2]);
  string shm_name = argv[3];
  string sem_name = argv[4];

  if (worker_count <= 0)
    worker_count = 2;

  if (worker_count > 50)
    worker_count = 50;

  sem_init(&sem_empty, 0, q_size);
  sem_init(&sem_full, 0, 0);

  fifo_fd = open(fifo_path.c_str(), O_RDONLY);
  if (fifo_fd < 0)
  {
    print_tag("processor", "fifo open failed");
    return EXIT_IO_FAIL;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 1024 * 1024);

  pthread_t reader;
  pthread_create(&reader, &attr, reader_fn, 0);

  vector<pthread_t> workers(worker_count);
  for (int i = 0; i < worker_count; i++)
    pthread_create(&workers[i], &attr, worker_fn, 0);

  pthread_join(reader, 0);
  for (int i = 0; i < worker_count; i++)
    pthread_join(workers[i], 0);

  pthread_attr_destroy(&attr);

  for (int i = 0; i < table_count; i++)
    mark_top3(table_data[i]);

  int shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
  if (shm_fd < 0)
  {
    print_tag("processor", "shm_open failed");
    return EXIT_IPC_FAIL;
  }

  SharedData *sh = (SharedData *)mmap(0, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (sh == (void *)-1) {
    print_tag("processor", "mmap failed");
    close(shm_fd);
    return EXIT_IPC_FAIL;
  }

  memset(sh, 0, sizeof(SharedData));
  sh->total_records = total_records;
  sh->count = table_count;

  for (int i = 0; i < table_count; i++)
    sh->records[i] = table_data[i];

  munmap(sh, sizeof(SharedData));
  close(shm_fd);

  sem_t *rep_sem = sem_open(sem_name.c_str(), 0);
  if (rep_sem == SEM_FAILED)
  {
    print_tag("processor", "sem_open failed");
    return EXIT_IPC_FAIL;
  }

  sem_post(rep_sem);
  sem_close(rep_sem);

  close(fifo_fd);
  sem_destroy(&sem_empty);
  sem_destroy(&sem_full);

  print_tag("processor", "done");
  return EXIT_OK;
}
