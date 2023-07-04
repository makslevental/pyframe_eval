#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

namespace py = pybind11;

#define Py_BUILD_CORE
#include "cpythonDefs.h"
#include <internal/pycore_frame.h>
#include <internal/pycore_pystate.h>
#undef Py_BUILD_CORE

#include <utility>

#ifdef _WIN32
#define unlikely(x) (x)
#else
#define unlikely(x) __builtin_expect((x), 0)
#endif

#define NULL_CHECK(val)                                                        \
  if (unlikely((val) == nullptr)) {                                            \
    fprintf(stderr, "NULL ERROR: %s:%d\n", __FILE__, __LINE__);                \
    PyErr_Print();                                                             \
    abort();                                                                   \
  } else {                                                                     \
  }

#define CHECK(cond)                                                            \
  if (unlikely(!(cond))) {                                                     \
    fprintf(stderr, "DEBUG CHECK FAILED: %s:%d\n", __FILE__, __LINE__);        \
    abort();                                                                   \
  } else {                                                                     \
  }

#ifdef DEBUG

#define DEBUG_CHECK(cond) CHECK(cond)
#define DEBUG_NULL_CHECK(val) NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...)                                                  \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__, __VA_ARGS__)
#define DEBUG_TRACE0(msg)                                                      \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__)

#else

#define DEBUG_CHECK(cond)
#define DEBUG_NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...)
#define DEBUG_TRACE0(msg)

#endif

static py::object globalCb;

#define DECLARE_PYOBJ_ATTR(name)                                               \
  PyObject *name() const {                                                     \
    PyObject *res = (PyObject *)this->frame->name;                             \
    /*Increment the reference count for object o. The object may be NULL, in   \
     * which case the macro has no effect.*/                                   \
    Py_XINCREF(res);                                                           \
    return res;                                                                \
  }

struct PyInterpreterFrame {
  PyInterpreterFrame(_PyInterpreterFrame *frame) : frame(frame) {}

  DECLARE_PYOBJ_ATTR(f_builtins)
  DECLARE_PYOBJ_ATTR(f_code)
  DECLARE_PYOBJ_ATTR(f_func)
  DECLARE_PYOBJ_ATTR(f_globals)
  DECLARE_PYOBJ_ATTR(f_locals)
  DECLARE_PYOBJ_ATTR(frame_obj)
  PyInterpreterFrame *previous() const {
    auto *res = new PyInterpreterFrame(this->frame->previous);
    return res;
  }

  PyObject *f_lasti() const {
    return PyLong_FromLong(_PyInterpreterFrame_LASTI(this->frame));
  }
  _PyInterpreterFrame *frame; // Borrowed reference
};
#undef DECLARE_PYOBJ_ATTR

inline static const char *name(_PyInterpreterFrame *frame) {
  DEBUG_CHECK(PyUnicode_Check(frame->f_code->co_name));
  return PyUnicode_AsUTF8(frame->f_code->co_name);
}

inline static PyObject *evalCustomCode(PyThreadState *tstate,
                                       _PyInterpreterFrame *frame,
                                       PyCodeObject *code, int throw_flag) {
  Py_ssize_t ncells = 0;
  Py_ssize_t nfrees = 0;
  Py_ssize_t nlocals_new = code->co_nlocals;
  Py_ssize_t nlocals_old = frame->f_code->co_nlocals;
  ncells = code->co_ncellvars;
  nfrees = code->co_nfreevars;
  DEBUG_NULL_CHECK(tstate);
  DEBUG_NULL_CHECK(frame);
  DEBUG_NULL_CHECK(code);
  DEBUG_CHECK(nlocals_new >= nlocals_old);
  DEBUG_CHECK(ncells == frame->f_code->co_ncellvars);
  DEBUG_TRACE("code->co_nfreevars: %zi, frame->f_code->co_nfreevars: %i", nfrees,
              frame->f_code->co_nfreevars);
  DEBUG_CHECK(nfrees == frame->f_code->co_nfreevars);

  // Generate Python function object and _PyInterpreterFrame in a way similar to
  // https://github.com/python/cpython/blob/e715da6db1d1d70cd779dc48e1ba8110c51cc1bf/Python/ceval.c#L1130
  PyFunctionObject *func =
      _PyFunction_CopyWithNewCode((PyFunctionObject *)frame->f_func, code);
  if (func == nullptr) {
    return nullptr;
  }

  size_t size = code->co_nlocalsplus + code->co_stacksize + FRAME_SPECIALS_SIZE;
  // _EVAL_API_FRAME_OBJECT (_PyInterpreterFrame) is a regular C struct, so
  // it should be safe to use system malloc over Python malloc, e.g.
  // PyMem_Malloc
  auto *shadow =
      (_PyInterpreterFrame *)malloc(size * sizeof(_PyInterpreterFrame *));
  if (shadow == nullptr) {
    Py_DECREF(func);
    return nullptr;
  }

  Py_INCREF(func);
  // consumes reference to func
  _PyFrame_InitializeSpecials(shadow, func, nullptr, code->co_nlocalsplus);

  PyObject **fastlocals_old = frame->localsplus;
  PyObject **fastlocals_new = shadow->localsplus;

  // localsplus are XINCREF'd by default eval frame, so all values must be
  // valid.
  for (int i = 0; i < code->co_nlocalsplus; i++) {
    fastlocals_new[i] = nullptr;
  }

  // copy from old localsplus to new localsplus:
  // for i, name in enumerate(localsplusnames_new):
  //   name_to_idx[name] = i
  // for i, name in enumerate(localsplusnames_old):
  //   fastlocals_new[name_to_idx[name]] = fastlocals_old[i]
  PyObject *name_to_idx = PyDict_New();
  if (name_to_idx == nullptr) {
    DEBUG_TRACE0("unable to create localsplus name dict");
    _PyFrame_Clear(shadow);
    free(shadow);
    Py_DECREF(func);
    return nullptr;
  }

  for (Py_ssize_t i = 0; i < code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(code->co_localsplusnames, i);
    PyObject *idx = PyLong_FromSsize_t(i);
    if (name == nullptr || idx == nullptr ||
        PyDict_SetItem(name_to_idx, name, idx) != 0) {
      Py_DECREF(name_to_idx);
      _PyFrame_Clear(shadow);
      free(shadow);
      Py_DECREF(func);
      return nullptr;
    }
  }

  for (Py_ssize_t i = 0; i < frame->f_code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(frame->f_code->co_localsplusnames, i);
    PyObject *idx = PyDict_GetItem(name_to_idx, name);
    Py_ssize_t new_i = PyLong_AsSsize_t(idx);
    if (name == nullptr || idx == nullptr ||
        (new_i == (Py_ssize_t)-1 && PyErr_Occurred() != nullptr)) {
      Py_DECREF(name_to_idx);
      _PyFrame_Clear(shadow);
      free(shadow);
      Py_DECREF(func);
      return nullptr;
    }
    Py_XINCREF(fastlocals_old[i]);
    fastlocals_new[new_i] = fastlocals_old[i];
  }

  Py_DECREF(name_to_idx);

  PyObject *result = _PyEval_EvalFrameDefault(tstate, shadow, throw_flag);

  _PyFrame_Clear(shadow);
  free(shadow);
  Py_DECREF(func);

  return result;
}

static PyObject *evalFrameTrampoline(PyThreadState *tstate,
                                     _PyInterpreterFrame *frame, int exc) {
  DEBUG_TRACE("begin %s %s %i %i", name(frame),
              PyUnicode_AsUTF8(frame->f_code->co_filename),
              frame->f_code->co_firstlineno, _PyInterpreterFrame_LASTI(frame));
  auto thp = PyInterpreterFrame(frame);
  // this prevents recursion/reentrancy - ie the callback stackframe brings you
  // back here
  _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                       _PyEval_EvalFrameDefault);
  PyObject *result;
  if (!globalCb.is_none()) {
    auto maybeCode = globalCb(thp);
    DEBUG_TRACE0("done calling function");
    if (!maybeCode.is_none()) {
      auto code = (PyCodeObject *)(maybeCode.ptr());
      DEBUG_NULL_CHECK(code)
      _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                           evalFrameTrampoline);
      result = evalCustomCode(tstate, frame, code, exc);
    } else
      result = _PyEval_EvalFrameDefault(tstate, frame, exc);
  } else
    result = _PyEval_EvalFrameDefault(tstate, frame, exc);
  _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                       evalFrameTrampoline);
  return result;
}

PYBIND11_MODULE(pyframe_eval, m) {
#define DECLARE_PYOBJ_ATTR(name)                                               \
  .def_property_readonly(                                                      \
      "f_" #name,                                                              \
      [](const PyInterpreterFrame &self) {                                     \
        return py::reinterpret_borrow<py::object>(                             \
            (PyObject *)self.f_##name());                                      \
      },                                                                       \
      py::return_value_policy::reference)

  py::class_<PyInterpreterFrame>(m, "_PyInterpreterFrame")
      DECLARE_PYOBJ_ATTR(builtins) DECLARE_PYOBJ_ATTR(code)
          DECLARE_PYOBJ_ATTR(func) DECLARE_PYOBJ_ATTR(globals)
              DECLARE_PYOBJ_ATTR(lasti) DECLARE_PYOBJ_ATTR(locals)
                  .def_property_readonly(
                      "previous",
                      [](const PyInterpreterFrame &self) {
                        return py::reinterpret_borrow<py::object>(
                            (PyObject *)self.previous());
                      },
                      py::return_value_policy::reference)
                  .def_property_readonly(
                      "frame_obj",
                      [](const PyInterpreterFrame &self) {
                        return py::reinterpret_borrow<py::object>(
                            (PyObject *)self.frame_obj());
                      },
                      py::return_value_policy::reference);
#undef DECLARE_PYOBJ_ATTR

  m.def("set_eval_frame", [](const py::object &, py::object cb) {
    globalCb = std::move(cb);
    _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                         evalFrameTrampoline);
  });
}