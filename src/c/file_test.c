#include <ovtest.h>

#include "file.h"

static void test_file_list_functionality(void) {
  struct gcmz_file_list *list = NULL;
  struct ov_error err = {0};

  list = gcmz_file_list_create(&err);
  TEST_CHECK(list != NULL);

  TEST_CHECK(gcmz_file_list_add(list, L"C:\\test\\image1.jpg", L"image/jpeg", &err));
  TEST_CHECK(gcmz_file_list_add(list, L"C:\\test\\image2.png", L"image/png", &err));
  TEST_CHECK(gcmz_file_list_count(list) == 2);

  struct gcmz_file const *file = gcmz_file_list_get(list, 0);
  if (TEST_CHECK(file != NULL)) {
    TEST_CHECK(wcscmp(file->path, L"C:\\test\\image1.jpg") == 0);
    TEST_CHECK(wcscmp(file->mime_type, L"image/jpeg") == 0);
    TEST_CHECK(file->temporary == false);
  }

  TEST_CHECK(gcmz_file_list_remove(list, 0, &err));
  TEST_CHECK(gcmz_file_list_count(list) == 1);

  file = gcmz_file_list_get(list, 0);
  if (TEST_CHECK(file != NULL)) {
    TEST_CHECK(wcscmp(file->path, L"C:\\test\\image2.png") == 0);
  }
  gcmz_file_list_destroy(&list);
}

TEST_LIST = {{"test_file_list_functionality", test_file_list_functionality}, {NULL, NULL}};
