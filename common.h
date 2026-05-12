#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <vector>
#include <sstream>

using namespace std;

#define EXIT_OK 0
#define EXIT_BAD_ARGS 10
#define EXIT_IPC_FAIL 20
#define EXIT_CHILD_FAIL 30
#define EXIT_IO_FAIL 40
#define EXIT_SIGINT_CODE 130
#define EXIT_SIGTERM_CODE 143

#define CHUNK_SIZE 1024
#define MAX_QUEUE 256
#define MAX_CATEGORIES 100
#define MAX_PRODUCTS 200
#define TOP_N 3

struct Chunk {
  int chunk_id;
  int byte_count;
  int source_file_id;
  int is_eof;
  char data[CHUNK_SIZE];
};

struct ProductRecord {
  char product[64];
  float revenue;
};

struct CategoryRecord {
  char category[64];
  float total_revenue;
  int product_count;
  ProductRecord products[MAX_PRODUCTS];
};

struct SharedData {
  int total_records;
  int count;
  CategoryRecord records[MAX_CATEGORIES];
};

#endif
