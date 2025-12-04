#include "gcmz_dataobj.h"

#ifndef COBJMACROS
#  define COBJMACROS
#endif
#ifndef CONST_VTABLE
#  define CONST_VTABLE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ole2.h>

static IID const iid_gcmz_api_props = {0xe2c1e605, 0x5249, 0x4ce7, {0xaa, 0xec, 0x44, 0xea, 0xf0, 0xa6, 0x19, 0x61}};

struct i_gcmz_api_props {
  struct gcmz_api_props_vtbl const *lpVtbl;
};

struct gcmz_api_props_vtbl {
  // IUnknown methods
  HRESULT(STDMETHODCALLTYPE *QueryInterface)(struct i_gcmz_api_props *This, REFIID riid, void **ppvObject);
  ULONG(STDMETHODCALLTYPE *AddRef)(struct i_gcmz_api_props *This);
  ULONG(STDMETHODCALLTYPE *Release)(struct i_gcmz_api_props *This);

  // Custom methods
  HRESULT(STDMETHODCALLTYPE *IsConvertExoEnabled)(struct i_gcmz_api_props *This, bool *enabled);
  HRESULT(STDMETHODCALLTYPE *IsFromExternalApi)(struct i_gcmz_api_props *This, bool *from_external);
};

// IDataObject implementation that also implements gcmz_api_props
struct gcmz_dataobj {
  IDataObject dataobj;           ///< IDataObject interface (must be first)
  struct i_gcmz_api_props props; ///< gcmz_api_props interface (second interface)
  IDataObject *orig;             ///< Wrapped IDataObject
  LONG ref_count;                ///< Shared reference count
  bool use_exo_converter;        ///< EXO conversion flag
  bool from_external_api;        ///< Whether drop originated from external API
};

static inline struct gcmz_dataobj *get_impl_from_dataobj(IDataObject *const This) {
  return (struct gcmz_dataobj *)This;
}

static inline struct gcmz_dataobj *get_impl_from_props(struct i_gcmz_api_props *const This) {
  return (struct gcmz_dataobj *)((uintptr_t)This - offsetof(struct gcmz_dataobj, props));
}

#define GET_IMPL(This)                                                                                                 \
  _Generic((This), IDataObject *: get_impl_from_dataobj, struct i_gcmz_api_props *: get_impl_from_props)(This)

static HRESULT
    STDMETHODCALLTYPE i_data_object_query_interface(IDataObject *const This, REFIID riid, void **const ppvObject) {
  if (!ppvObject) {
    return E_POINTER;
  }

  struct gcmz_dataobj *const impl = GET_IMPL(This);

  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
    *ppvObject = &impl->dataobj;
    IDataObject_AddRef(&impl->dataobj);
    return S_OK;
  }
  if (IsEqualIID(riid, &iid_gcmz_api_props)) {
    *ppvObject = &impl->props;
    IDataObject_AddRef(&impl->dataobj);
    return S_OK;
  }

  return IDataObject_QueryInterface(impl->orig, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE i_data_object_add_ref(IDataObject *const This) {
  struct gcmz_dataobj *const impl = GET_IMPL(This);
  return (ULONG)InterlockedIncrement(&impl->ref_count);
}

static ULONG STDMETHODCALLTYPE i_data_object_release(IDataObject *const This) {
  struct gcmz_dataobj *impl = GET_IMPL(This);
  ULONG const ref = (ULONG)InterlockedDecrement(&impl->ref_count);
  if (ref == 0) {
    if (impl->orig) {
      IDataObject_Release(impl->orig);
    }
    OV_FREE(&impl);
  }
  return ref;
}

static HRESULT STDMETHODCALLTYPE i_data_object_get_data(IDataObject *const This,
                                                        FORMATETC *const pformatetcIn,
                                                        STGMEDIUM *const pmedium) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_GetData(impl->orig, pformatetcIn, pmedium);
}

static HRESULT STDMETHODCALLTYPE i_data_object_get_data_here(IDataObject *const This,
                                                             FORMATETC *const pformatetc,
                                                             STGMEDIUM *const pmedium) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_GetDataHere(impl->orig, pformatetc, pmedium);
}

static HRESULT STDMETHODCALLTYPE i_data_object_query_get_data(IDataObject *const This, FORMATETC *const pformatetc) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_QueryGetData(impl->orig, pformatetc);
}

static HRESULT STDMETHODCALLTYPE i_data_object_get_canonical_format_etc(IDataObject *const This,
                                                                        FORMATETC *const pformatetcIn,
                                                                        FORMATETC *const pformatetcOut) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_GetCanonicalFormatEtc(impl->orig, pformatetcIn, pformatetcOut);
}

static HRESULT STDMETHODCALLTYPE i_data_object_set_data(IDataObject *const This,
                                                        FORMATETC *const pformatetc,
                                                        STGMEDIUM *const pmedium,
                                                        BOOL const fRelease) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_SetData(impl->orig, pformatetc, pmedium, fRelease);
}

static HRESULT STDMETHODCALLTYPE i_data_object_enum_format_etc(IDataObject *const This,
                                                               DWORD const dwDirection,
                                                               IEnumFORMATETC **const ppenumFormatEtc) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_EnumFormatEtc(impl->orig, dwDirection, ppenumFormatEtc);
}

static HRESULT STDMETHODCALLTYPE i_data_object_d_advise(IDataObject *const This,
                                                        FORMATETC *const pformatetc,
                                                        DWORD const advf,
                                                        IAdviseSink *const pAdvSink,
                                                        DWORD *const pdwConnection) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_DAdvise(impl->orig, pformatetc, advf, pAdvSink, pdwConnection);
}

static HRESULT STDMETHODCALLTYPE i_data_object_d_unadvise(IDataObject *const This, DWORD const dwConnection) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_DUnadvise(impl->orig, dwConnection);
}

static HRESULT STDMETHODCALLTYPE i_data_object_enum_d_advise(IDataObject *const This,
                                                             IEnumSTATDATA **const ppenumAdvise) {
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  return IDataObject_EnumDAdvise(impl->orig, ppenumAdvise);
}

static HRESULT STDMETHODCALLTYPE i_gcmz_api_props_query_interface(struct i_gcmz_api_props *const This,
                                                                  REFIID riid,
                                                                  void **const ppvObject) {
  struct gcmz_dataobj *const impl = GET_IMPL(This);
  return i_data_object_query_interface(&impl->dataobj, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE i_gcmz_api_props_add_ref(struct i_gcmz_api_props *const This) {
  struct gcmz_dataobj *const impl = GET_IMPL(This);
  return i_data_object_add_ref(&impl->dataobj);
}

static ULONG STDMETHODCALLTYPE i_gcmz_api_props_release(struct i_gcmz_api_props *const This) {
  struct gcmz_dataobj *const impl = GET_IMPL(This);
  return i_data_object_release(&impl->dataobj);
}

static HRESULT STDMETHODCALLTYPE i_gcmz_api_props_is_convert_exo_enabled(struct i_gcmz_api_props *const This,
                                                                         bool *const enabled) {
  if (!enabled) {
    return E_POINTER;
  }
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  *enabled = impl->use_exo_converter;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE i_gcmz_api_props_is_from_external_api(struct i_gcmz_api_props *const This,
                                                                       bool *const from_external) {
  if (!from_external) {
    return E_POINTER;
  }
  struct gcmz_dataobj const *const impl = GET_IMPL(This);
  *from_external = impl->from_external_api;
  return S_OK;
}

// Public API

NODISCARD void *gcmz_dataobj_create(void *const dataobj,
                                    bool const use_exo_converter,
                                    bool const from_external_api,
                                    struct ov_error *const err) {
  if (!dataobj) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_invalid_argument);
    return NULL;
  }

  static IDataObjectVtbl const dataobj_vtbl = {
      .QueryInterface = i_data_object_query_interface,
      .AddRef = i_data_object_add_ref,
      .Release = i_data_object_release,
      .GetData = i_data_object_get_data,
      .GetDataHere = i_data_object_get_data_here,
      .QueryGetData = i_data_object_query_get_data,
      .GetCanonicalFormatEtc = i_data_object_get_canonical_format_etc,
      .SetData = i_data_object_set_data,
      .EnumFormatEtc = i_data_object_enum_format_etc,
      .DAdvise = i_data_object_d_advise,
      .DUnadvise = i_data_object_d_unadvise,
      .EnumDAdvise = i_data_object_enum_d_advise,
  };
  static struct gcmz_api_props_vtbl const props_vtbl = {
      .QueryInterface = i_gcmz_api_props_query_interface,
      .AddRef = i_gcmz_api_props_add_ref,
      .Release = i_gcmz_api_props_release,
      .IsConvertExoEnabled = i_gcmz_api_props_is_convert_exo_enabled,
      .IsFromExternalApi = i_gcmz_api_props_is_from_external_api,
  };

  struct gcmz_dataobj *impl = NULL;
  void *result = NULL;

  if (!OV_REALLOC(&impl, 1, sizeof(*impl))) {
    OV_ERROR_SET_GENERIC(err, ov_error_generic_out_of_memory);
    goto cleanup;
  }

  *impl = (struct gcmz_dataobj){
      .dataobj = {.lpVtbl = &dataobj_vtbl},
      .props = {.lpVtbl = &props_vtbl},
      .orig = (IDataObject *)dataobj,
      .ref_count = 1,
      .use_exo_converter = use_exo_converter,
      .from_external_api = from_external_api,
  };
  IDataObject_AddRef(impl->orig);

  result = &impl->dataobj;
  impl = NULL;

cleanup:
  if (impl) {
    OV_FREE(&impl);
  }
  return result;
}

bool gcmz_dataobj_is_exo_convert_enabled(void *const dataobj) {
  if (!dataobj) {
    return false;
  }
  struct i_gcmz_api_props *props = NULL;
  HRESULT hr = IDataObject_QueryInterface((IDataObject *)dataobj, &iid_gcmz_api_props, (void **)&props);
  if (FAILED(hr) || !props) {
    return false;
  }
  bool enabled = true;
  hr = props->lpVtbl->IsConvertExoEnabled(props, &enabled);
  props->lpVtbl->Release(props);
  if (FAILED(hr)) {
    return false;
  }

  return enabled;
}

bool gcmz_dataobj_is_from_external_api(void *const dataobj) {
  if (!dataobj) {
    return false;
  }
  struct i_gcmz_api_props *props = NULL;
  HRESULT hr = IDataObject_QueryInterface((IDataObject *)dataobj, &iid_gcmz_api_props, (void **)&props);
  if (FAILED(hr) || !props) {
    return false;
  }
  bool from_external = false;
  hr = props->lpVtbl->IsFromExternalApi(props, &from_external);
  props->lpVtbl->Release(props);
  if (FAILED(hr)) {
    return false;
  }

  return from_external;
}
