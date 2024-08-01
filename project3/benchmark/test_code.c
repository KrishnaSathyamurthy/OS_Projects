// Author : Krishna Sathyamurthy
// username of iLab: ks2025
// iLab Server: kill.cs.rutgers.edu
// gcc -g -w -o test.out test.c ../my_vm64.a

#include "../my_vm.h"
#include <stdio.h>
#include <stdlib.h>

typedef page_t data_var;

#define MAX_RW_VALUE 1024*1024
#define MAX_RW_TEST 1024
#define MAX_GRID 64
#define MAX_GRID_TEST 64
#define VAL_SIZE sizeof(data_var) // since internal fragmentation is not handled, this makes it easier...

int test_malloc() {
  printf("Testing memory allocation and free\n");
  for (int i = 0; i < 1024; i++) {
#ifdef DEBUG_TEST
    printf("%d\n", i);
#endif
    size_t size = VAL_SIZE*(1024*1024*4)*(i+1);
    void *test = t_malloc(size);
    if (test == NULL) {
      // To be compared against the maximum number of frames that is feasible for the current configuration
      // If it is "indeed" above limits, it is not an issue...
      // TODO : automate calc of max size that is feasible...
      printf("Exceeded track limits %ld\n", size);
      return -1;
    }
    t_free((page_t)test, size);
  }
  printf("Testing memory allocation and free success\n");
  return 0;
}

int check_rw_opr() {
  int level = (rand() % MAX_RW_VALUE) + 1;
  void *test = t_malloc(VAL_SIZE*level);
  data_var *val_array = (data_var*)malloc(VAL_SIZE*level);
  for (int i = 0; i < level; i++) {
    data_var data = rand();
    put_value((page_t)(test+(i*VAL_SIZE)), &data, VAL_SIZE);
    val_array[i] = data;
  }
  void *start = test;
  for (int i = 0; i < level; i++, start+=VAL_SIZE) {
    data_var val = 0;
    get_value((page_t)start, &val, VAL_SIZE);

    if (val != val_array[i]) {
      printf("\nValue at pos %d::%lld, is not matching with value obtained %lld\n", i, val_array[i], val);
      return -1;
    } else {
#ifdef DEBUG_TEST
      printf("%lld::%lld ", val, val_array[i]);
#endif
    }
  }
#ifdef DEBUG_TEST
  printf("\n");
#endif
  t_free((page_t)test, VAL_SIZE*level);
  free(val_array);
  return 0;
}

void test_rw_opr() {
  printf("Testing for read/write operations...\n");
  for (int i = 0; i < MAX_RW_TEST; i++) {
    if (check_rw_opr() == -1) {
      printf("Testing Read/Write failed at iteration %d\n", i+1);
      exit(-1);
    }
  }
  printf("Read/write operations were a success\n");
  print_TLB_missrate();
}

int verify_mat_values(page_t mat_c, data_var *mat_z, int col, int row, const char *opr, int count) {
  page_t address_c;
  data_var value_c, value_z;
  for (size_t i = 0; i < col; i++) {
    for (size_t j = 0; j < row; j++) {
      address_c = mat_c + (i*row*VAL_SIZE) + (j*VAL_SIZE);
      value_z = *(mat_z + (i*row) + j);
      get_value(address_c, &value_c, VAL_SIZE);
      if (value_c != value_z) {
        printf("%s operation failed during iteration %d, value at matrix[%ld][%ld]=%lld does not match with value obtained %lld\n", opr, count, i, j, value_z, value_c);
        return 1;
      }
    }
  }
  return 0;
}

int check_and_fill_mat(page_t mat_m, data_var *mat_n, int col, int row, int count) {
  page_t address_m;
  data_var value;
  for (size_t i = 0; i < col; i++) {
    for (size_t j = 0; j < row; j++) {
      value = rand();
      address_m = mat_m + (i*row*VAL_SIZE) + (j*VAL_SIZE);
      *(mat_n + (i*row) + j) = value;
      put_value(address_m, &value, VAL_SIZE);
    }
  }

  return verify_mat_values(mat_m, mat_n, col, row, "Matrix Init", count);
}

void mat_mult_page(data_var *l, data_var *r, data_var *o, size_t col_l, size_t row_r, size_t common) {
  data_var value_l, value_r, value_o;
  for (size_t i = 0; i < col_l; i++) {
    for (size_t j = 0; j < row_r; j++) {
      value_o = 0; //[i][j] = [i][k] * [k][j]
      for (size_t k = 0; k < common; k++) {
        value_l = *(l + (i*common) + k);
        value_r = *(r + (k*row_r) + j);
        value_o += (value_l*value_r);
      }
      *(o + (i*row_r) + j) = value_o;
    }
  }
}

#ifdef DEBUG_TEST
void print_mat(const data_var *mat, int col, int row) {
  for (int i = 0; i < col; i++) {
    for (int j = 0; j < row; j++) {
      printf("%lld  ", *(mat + (i*row) + j));
    }
    printf("\n");
  }
}

void print_t_mat(page_t mat, int col, int row) {
  page_t address;
  data_var value;
  for (int i = 0; i < col; i++) {
    value = 0;
    for (int j = 0; j < row; j++) {
      address = mat + (i*row*VAL_SIZE) + (j*VAL_SIZE);
      get_value(address, &value, VAL_SIZE);
      printf("%lld  ", value);
    }
    printf("\n");
  }
}

void print_t_mats(void *mat_a, void *mat_b, void *mat_c, int col, int row, int common) {
  printf("Matrix A %lld :: %dx%d\n", (page_t)mat_a, col, common);
  print_t_mat((page_t)mat_a, col, common);
  printf("Matrix B %lld :: %dx%d\n", (page_t)mat_b, common, row);
  print_t_mat((page_t)mat_b, common, row);
  printf("Matrix C %lld :: %dx%d\n", (page_t)mat_c, col, row);
  print_t_mat((page_t)mat_c, col, row);
}

void print_mats(data_var *mat_a, data_var *mat_b, data_var *mat_c, int col, int row, int common) {
  printf("Matrix X :: %dx%d\n", col, common);
  print_mat(mat_a, col, common);
  printf("Matrix Y :: %dx%d\n", common, row);
  print_mat(mat_b, common, row);
  printf("Matrix Z :: %dx%d\n", col, row);
  print_mat(mat_c, col, row);
}
#endif

void check_mat_mult(int col, int row, int count) {
  int common = rand() % MAX_GRID + 1;
  void *mat_a = t_malloc(VAL_SIZE*col*common);
  void *mat_b = t_malloc(VAL_SIZE*common*row);
  void *mat_c = t_malloc(VAL_SIZE*col*row);
  data_var *mat_x = (data_var*)malloc(VAL_SIZE*col*common);
  data_var *mat_y = (data_var*)malloc(VAL_SIZE*common*row);
  data_var *mat_z = (data_var*)malloc(VAL_SIZE*col*row);
  int ret = 0;
  ret |= check_and_fill_mat((page_t)mat_a, mat_x, col, common, count);
  ret |= check_and_fill_mat((page_t)mat_b, mat_y, common, row, count);
  mat_mult((page_t)mat_a, (page_t)mat_b, (page_t)mat_c, col, row, common, VAL_SIZE);
  // mat_mult((page_t)mat_a, (page_t)mat_b, (page_t)mat_c, col, row, common);
  mat_mult_page(mat_x, mat_y, mat_z, col, row, common);
  ret |= verify_mat_values((page_t)mat_c, mat_z, col, row, "Matrix Mult", count);
#ifdef DEBUG_TEST
  print_t_mats(mat_a, mat_b, mat_c, col, row, common);
  print_mats(mat_x, mat_y, mat_z, col, row, common);
#endif
  t_free((page_t)mat_a, VAL_SIZE*col*common);
  t_free((page_t)mat_b, VAL_SIZE*common*row);
  t_free((page_t)mat_c, VAL_SIZE*col*row);
  free(mat_x);
  free(mat_y);
  free(mat_z);

  if (ret) {
    exit(-1);
  }
}

void test_mat_mult() {
  printf("Testing for matrix multiplication and related operations...\n");
  int count = 0;
  for (int i = 0; i < MAX_GRID_TEST; i++) {
    int col = rand() % MAX_GRID + 1;
    int row = rand() % MAX_GRID + 1;
    check_mat_mult(col, row, count++);
  }
  printf("Matrix multiplication was a success\n");
  print_TLB_missrate();
}

int main() {
  srand(time(NULL));
  test_malloc();
  test_rw_opr();
  test_mat_mult();
  return 0;
}
