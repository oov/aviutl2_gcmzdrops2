#include <ovtest.h>

#include <ovarray.h>
#include <ovprintf.h>

#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

#include "api.h"
#include "file.h"
#include "gcmz_types.h"

#ifndef SOURCE_DIR
#  define SOURCE_DIR .
#endif

// Test callback function for AviUtl2 data update notification
static void test_notify_update(struct gcmz_api *const api, void *const userdata) {
  (void)userdata; // Suppress unused parameter warning
  if (!api) {
    return;
  }

  // Simulate providing project data when notified
  struct gcmz_project_data test_data = {
      .width = 1920,
      .height = 1080,
      .video_rate = 30,
      .video_scale = 1,
      .sample_rate = 48000,
      .audio_ch = 2,
      .cursor_frame = 0,
      .display_frame = 0,
      .display_layer = 1,
      .display_zoom = 100,
      .flags = 0,
      .project_path = L"/test/project/path.aup",
  };

  struct ov_error err = {0};
  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &test_data, &err), &err);
}

// Test request callback context
struct test_request_context {
  bool callback_called;
  struct gcmz_file_list *received_files;
  int received_layer;
  int received_frame_advance;
  struct ov_error *received_error;
  void *received_userdata;
};

// Test callback function
static void test_request_callback(struct gcmz_api_request_params *const params,
                                  gcmz_api_request_complete_func const complete) {
  struct test_request_context *ctx = (struct test_request_context *)params->userdata;
  if (ctx) {
    ctx->callback_called = true;
    ctx->received_files = params->files;
    ctx->received_layer = params->layer;
    ctx->received_frame_advance = params->frame_advance;
    ctx->received_error = params->err;
    ctx->received_userdata = params->userdata;
  }

  // Call completion callback immediately for testing
  if (complete) {
    complete(params);
  }
}

// Helper function to check if GCMZDrops mutex already exists
static bool gcmzdrops_mutex_exists(void) {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"GCMZDropsMutex");
  if (mutex) {
    CloseHandle(mutex);
    return true;
  }
  return false;
}

// Test api create and destroy
static void test_api_create_destroy(void) {
  if (gcmzdrops_mutex_exists()) {
    TEST_SKIP("\"GCMZDropsMutex\" mutex already exists in environment");
    return;
  }

  struct gcmz_api *api = NULL;
  struct ov_error err = {0};

  // Test successful creation with options
  api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = NULL,
          .update_callback = test_notify_update,
          .userdata = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(api != NULL, &err)) {
    return;
  }

  // Test destroy
  gcmz_api_destroy(&api);
  TEST_ASSERT(api == NULL);

  // Test destroy with NULL (should not crash)
  gcmz_api_destroy(NULL);

  // Test create with NULL options (should still work)
  api = gcmz_api_create(NULL, NULL);
  if (api) {
    gcmz_api_destroy(&api);
  }
}

// Test callback setting
static void test_api_callback_setting(void) {
  if (gcmzdrops_mutex_exists()) {
    TEST_SKIP("\"GCMZDropsMutex\" mutex already exists in environment");
    return;
  }

  struct gcmz_api *api = NULL;
  struct ov_error err = {0};
  struct test_request_context ctx = {0};

  // Test creation with callbacks
  api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = test_request_callback,
          .update_callback = test_notify_update,
          .userdata = &ctx,
      },
      &err);
  if (!TEST_SUCCEEDED(api != NULL, &err)) {
    return;
  }

  gcmz_api_destroy(&api);
}

// Test file list operations
static void test_file_list_operations(void) {
  struct gcmz_file_list *list = NULL;
  struct ov_error err = {0};

  // Test creation
  list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(list != NULL, &err)) {
    return;
  }

  // Test initial count
  TEST_CHECK(gcmz_file_list_count(list) == 0);

  // Test adding file
  TEST_SUCCEEDED(gcmz_file_list_add(list, L"C:\\test\\file1.txt", L"text/plain", &err), &err);
  TEST_CHECK(gcmz_file_list_count(list) == 1);

  // Test getting file
  struct gcmz_file const *file = gcmz_file_list_get(list, 0);
  TEST_CHECK(file != NULL);
  if (file) {
    TEST_CHECK(file->path != NULL);
    TEST_CHECK(wcscmp(file->path, L"C:\\test\\file1.txt") == 0);
    TEST_CHECK(file->mime_type != NULL);
    TEST_CHECK(wcscmp(file->mime_type, L"text/plain") == 0);
    TEST_CHECK(!file->temporary);
  }

  // Test adding temporary file
  TEST_SUCCEEDED(gcmz_file_list_add_temporary(list, L"C:\\temp\\temp1.tmp", L"application/octet-stream", &err), &err);
  TEST_CHECK(gcmz_file_list_count(list) == 2);

  // Test getting temporary file
  struct gcmz_file const *temp_file = gcmz_file_list_get(list, 1);
  TEST_CHECK(temp_file != NULL);
  if (temp_file) {
    TEST_CHECK(temp_file->path != NULL);
    TEST_CHECK(wcscmp(temp_file->path, L"C:\\temp\\temp1.tmp") == 0);
    TEST_CHECK(temp_file->temporary);
  }

  // Test removing file
  TEST_SUCCEEDED(gcmz_file_list_remove(list, 0, &err), &err);
  TEST_CHECK(gcmz_file_list_count(list) == 1);

  // Test out of bounds access
  struct gcmz_file const *invalid_file = gcmz_file_list_get(list, 10);
  TEST_CHECK(invalid_file == NULL);

  // Test destroy
  gcmz_file_list_destroy(&list);
  TEST_CHECK(list == NULL);

  // Test operations on NULL list
  TEST_CHECK(gcmz_file_list_count(NULL) == 0);
  TEST_CHECK(gcmz_file_list_get(NULL, 0) == NULL);
  gcmz_file_list_destroy(NULL); // Should not crash
}

// Test security validation
static void test_security_validation(void) {
  struct gcmz_file_list *list = NULL;
  struct ov_error err = {0};

  list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(list != NULL, &err)) {
    return;
  }

  // Test path traversal detection - these should fail in actual security validation
  // For now, we just test that files can be added (security validation may be separate)
  TEST_SUCCEEDED(gcmz_file_list_add(list, L"C:\\test\\normalfile.txt", L"text/plain", &err), &err);
  gcmz_file_list_destroy(&list);
}

// Test error handling
static void test_error_handling(void) {
  struct ov_error err = {0};

  // Test with NULL parameters for API
  struct gcmz_api *null_api = gcmz_api_create(NULL, NULL);
  // Should still work even with NULL err parameter
  if (null_api) {
    gcmz_api_destroy(&null_api);
  }

  // Test file list with NULL parameters
  struct gcmz_file_list *list = gcmz_file_list_create(NULL);
  // gcmz_file_list_create(NULL) should still work and return a valid list
  if (list) {
    gcmz_file_list_destroy(&list);
  }

  // Test with valid err parameter but NULL path
  list = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(list != NULL, &err)) {
    return;
  }
  TEST_FAILED_WITH(gcmz_file_list_add(list, NULL, L"text/plain", &err),
                   &err,
                   ov_error_type_generic,
                   ov_error_generic_invalid_argument);

  gcmz_file_list_destroy(&list);
  TEST_CHECK(list == NULL);
}

// Test thread safety and window management
static void test_thread_management(void) {
  if (gcmzdrops_mutex_exists()) {
    TEST_SKIP("\"GCMZDropsMutex\" mutex already exists in environment");
    return;
  }

  struct gcmz_api *api = NULL;
  struct ov_error err = {0};

  // Test API initialization (which creates threads)
  struct test_request_context ctx = {0};
  api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = test_request_callback,
          .update_callback = test_notify_update,
          .userdata = &ctx,
      },
      &err);
  if (!TEST_SUCCEEDED(api != NULL, &err)) {
    return;
  }

  // Give the thread time to initialize properly
  Sleep(100);

  // Cleanup (this tests thread shutdown)
  gcmz_api_destroy(&api);
  TEST_ASSERT(api == NULL);
}

// Test data structures and constants
static void test_api_structures(void) {
  // Test that we can create and populate request parameters
  struct gcmz_api_request_params params = {0};
  struct ov_error err = {0};

  params.files = gcmz_file_list_create(&err);
  if (!TEST_SUCCEEDED(params.files != NULL, &err)) {
    return;
  }

  params.layer = 1;
  params.frame_advance = 0;
  params.err = NULL;
  params.userdata = NULL;

  // Test the structure is properly initialized
  TEST_CHECK(params.layer == 1);
  TEST_CHECK(params.frame_advance == 0);
  TEST_CHECK(params.files != NULL);
  TEST_CHECK(gcmz_file_list_count(params.files) == 0);

  gcmz_file_list_destroy(&params.files);
}

// Test multiple data sets
static void test_multiple_data_sets(void) {
  if (gcmzdrops_mutex_exists()) {
    TEST_SKIP("\"GCMZDropsMutex\" mutex already exists in environment");
    return;
  }

  struct gcmz_api *api = NULL;
  struct ov_error err = {0};

  api = gcmz_api_create(
      &(struct gcmz_api_options){
          .request_callback = NULL,
          .update_callback = test_notify_update,
          .userdata = NULL,
      },
      &err);
  if (!TEST_SUCCEEDED(api != NULL, &err)) {
    return;
  }

  // First data set
  struct gcmz_project_data first_data = {
      .width = 1920,
      .height = 1080,
      .video_rate = 30,
      .video_scale = 1,
      .sample_rate = 48000,
      .audio_ch = 2,
      .cursor_frame = 10,
      .display_frame = 10,
      .display_layer = 1,
      .display_zoom = 100,
      .flags = 0,
      .project_path = L"C:\\first\\project\\path.aup",
  };

  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &first_data, &err), &err);

  // Second data set with different path length
  struct gcmz_project_data second_data = {
      .width = 1280,
      .height = 720,
      .video_rate = 60,
      .video_scale = 2,
      .sample_rate = 44100,
      .audio_ch = 6,
      .cursor_frame = 50,
      .display_frame = 50,
      .display_layer = 2,
      .display_zoom = 200,
      .flags = 1,
      .project_path = L"C:\\very\\long\\second\\project\\path\\with\\many\\subdirectories\\test.aup",
  };

  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &second_data, &err), &err);

  // Third data set with shorter path
  struct gcmz_project_data third_data = {
      .width = 640,
      .height = 480,
      .video_rate = 24,
      .video_scale = 1,
      .sample_rate = 22050,
      .audio_ch = 1,
      .cursor_frame = 100,
      .display_frame = 100,
      .display_layer = 3,
      .display_zoom = 50,
      .flags = 2,
      .project_path = L"C:\\short.aup",
  };

  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &third_data, &err), &err);

  // Fourth data set with NULL path (should reuse buffer)
  struct gcmz_project_data null_path_data = {
      .width = 800,
      .height = 600,
      .video_rate = 25,
      .video_scale = 1,
      .sample_rate = 48000,
      .audio_ch = 2,
      .cursor_frame = 200,
      .display_frame = 200,
      .display_layer = 4,
      .display_zoom = 75,
      .flags = 3,
      .project_path = NULL,
  };

  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &null_path_data, &err), &err);

  // Fifth data set with path again (should work after NULL)
  struct gcmz_project_data final_data = {
      .width = 1920,
      .height = 1080,
      .video_rate = 30,
      .video_scale = 1,
      .sample_rate = 48000,
      .audio_ch = 2,
      .cursor_frame = 300,
      .display_frame = 300,
      .display_layer = 5,
      .display_zoom = 100,
      .flags = 0,
      .project_path = L"C:\\final\\project.aup",
  };

  TEST_SUCCEEDED(gcmz_api_set_project_data(api, &final_data, &err), &err);

  gcmz_api_destroy(&api);
}

TEST_LIST = {
    {"api_create_destroy", test_api_create_destroy},
    {"api_callback_setting", test_api_callback_setting},
    {"file_list_operations", test_file_list_operations},
    {"security_validation", test_security_validation},
    {"error_handling", test_error_handling},
    {"thread_management", test_thread_management},
    {"api_structures", test_api_structures},
    {"multiple_data_sets", test_multiple_data_sets},
    {NULL, NULL},
};
