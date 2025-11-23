#define WIN32_LEAN_AND_MEAN
#include <ovtest.h>

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#include <ole2.h>

#include <ovprintf.h>

#include "drop.h"

struct test_drop_target {
  IDropTarget vtbl;
  LONG refcount;
};

static HRESULT STDMETHODCALLTYPE test_droptarget_query_interface(IDropTarget *this, REFIID riid, void **ppvObject) {
  (void)this;
  (void)riid;
  (void)ppvObject;
  return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE test_droptarget_add_ref(IDropTarget *this) {
  struct test_drop_target *self = (struct test_drop_target *)this;
  return (ULONG)InterlockedIncrement(&self->refcount);
}

static ULONG STDMETHODCALLTYPE test_droptarget_release(IDropTarget *this) {
  struct test_drop_target *self = (struct test_drop_target *)this;
  ULONG count = (ULONG)InterlockedDecrement(&self->refcount);
  if (count == 0) {
    OV_FREE(&self);
  }
  return count;
}

static HRESULT STDMETHODCALLTYPE
test_droptarget_drag_enter(IDropTarget *this, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
  (void)this;
  (void)pDataObj;
  (void)grfKeyState;
  (void)pt;
  *pdwEffect = DROPEFFECT_COPY;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE test_droptarget_drag_over(IDropTarget *this,
                                                           DWORD grfKeyState,
                                                           POINTL pt,
                                                           DWORD *pdwEffect) {
  (void)this;
  (void)grfKeyState;
  (void)pt;
  *pdwEffect = DROPEFFECT_COPY;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE test_droptarget_drag_leave(IDropTarget *this) {
  (void)this;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE
test_droptarget_drop(IDropTarget *this, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect) {
  (void)this;
  (void)pDataObj;
  (void)grfKeyState;
  (void)pt;
  *pdwEffect = DROPEFFECT_COPY;
  return S_OK;
}

static IDropTargetVtbl test_droptarget_vtbl = {
    test_droptarget_query_interface,
    test_droptarget_add_ref,
    test_droptarget_release,
    test_droptarget_drag_enter,
    test_droptarget_drag_over,
    test_droptarget_drag_leave,
    test_droptarget_drop,
};

static struct test_drop_target *create_test_droptarget(void) {
  struct test_drop_target *target = NULL;
  if (OV_REALLOC(&target, 1, sizeof(*target))) {
    target->vtbl.lpVtbl = &test_droptarget_vtbl;
    target->refcount = 1;
  }
  return target;
}

static bool
mock_dataobj_extract(void *dataobj, struct gcmz_file_list *file_list, void *userdata, struct ov_error *const err) {
  (void)dataobj;
  (void)file_list;
  (void)userdata;
  (void)err;
  return true;
}

static bool mock_cleanup_temp_files(wchar_t const *const path, void *userdata, struct ov_error *const err) {
  (void)path;
  (void)userdata;
  (void)err;
  return true;
}

static bool mock_project_data_provider(struct gcmz_project_data *data, void *userdata, struct ov_error *const err) {
  (void)data;
  (void)userdata;
  (void)err;
  return true;
}

static void test_drop_null_safety(void) {
  struct ov_error err = {0};
  gcmz_drop_destroy(NULL);

  TEST_CHECK(!gcmz_drop_simulate_drop(NULL, NULL, 0, 0, true, NULL, &err));
  TEST_CHECK(ov_error_is(&err, ov_error_type_generic, ov_error_generic_invalid_argument));
  OV_ERROR_DESTROY(&err);
}

// Test real COM integration
static void test_drop_real_com_integration(void) {
  struct gcmz_drop *d = NULL;
  struct ov_error err = {0};
  HWND real_window = NULL;
  struct test_drop_target *test_target = NULL;

  TEST_ASSERT(SUCCEEDED(OleInitialize(NULL)));

  real_window = CreateWindowExW(0,
                                L"STATIC",
                                L"TestWindow",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                300,
                                200,
                                NULL,
                                NULL,
                                GetModuleHandle(NULL),
                                NULL);
  if (!TEST_CHECK(real_window != NULL)) {
    goto cleanup;
  }

  test_target = create_test_droptarget();
  if (!TEST_CHECK(test_target != NULL)) {
    goto cleanup;
  }

  if (!TEST_CHECK(SUCCEEDED(RegisterDragDrop(real_window, (IDropTarget *)test_target)))) {
    goto cleanup;
  }

  d = gcmz_drop_create(
      mock_dataobj_extract, mock_cleanup_temp_files, mock_project_data_provider, NULL, NULL, NULL, &err);
  if (!TEST_CHECK(d != NULL)) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

  if (!TEST_CHECK(gcmz_drop_register_window(d, real_window, &err))) {
    OV_ERROR_DESTROY(&err);
    goto cleanup;
  }

cleanup:
  if (real_window) {
    DestroyWindow(real_window);
  }
  if (d) {
    gcmz_drop_destroy(&d);
  }
  if (test_target) {
    IDropTarget_Release((IDropTarget *)test_target);
  }
  OleUninitialize();
}

TEST_LIST = {
    {"drop_null_safety", test_drop_null_safety},
    {"drop_real_com_integration", test_drop_real_com_integration},
    {NULL, NULL},
};
