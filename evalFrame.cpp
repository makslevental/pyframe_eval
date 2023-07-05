#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pytypedefs.h>

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

//#define DEBUG

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

static py::object rewriteCodeCb = py::none();
static py::object evalCustomCodeCb = py::none();

struct PyInterpreterFrame {
  PyInterpreterFrame(_PyInterpreterFrame *frame) : frame(frame) {}

  PyObject *f_builtins() const {
    PyObject *res = this->frame->f_builtins;
    Py_XINCREF(res);
    return res;
  }
  PyCodeObject *f_code() const {
    PyCodeObject *res = this->frame->f_code;
    Py_XINCREF(res);
    return res;
  }
  PyFunctionObject *f_func() const {
    PyFunctionObject *res = this->frame->f_func;
    Py_XINCREF(res);
    return res;
  }
  PyObject *f_globals() const {
    PyObject *res = this->frame->f_globals;
    Py_XINCREF(res);
    return res;
  }
  PyObject *f_locals() const {
    PyObject *res = this->frame->f_locals;
    Py_XINCREF(res);
    return res;
  }

  PyFrameObject *frame_obj() const {
    PyFrameObject *res = this->frame->frame_obj;
    Py_XINCREF(res);
    return res;
  }

  PyInterpreterFrame *previous() const {
    PyInterpreterFrame *res;
    if (this->frame->previous)
      res = new PyInterpreterFrame(this->frame->previous);
    return res;
  }

  PyObject *f_lasti() const {
    return PyLong_FromLong(_PyInterpreterFrame_LASTI(this->frame));
  }

  std::vector<PyObject *> localsplus() const {
    PyObject **res = _PyFrame_GetLocalsArray(this->frame);
    std::vector<PyObject *> localsplus(
        res, res + this->frame->f_code->co_nlocalsplus);
    for (auto &item : localsplus)
      Py_XINCREF(item);
    return localsplus;
  }

  _PyInterpreterFrame *frame; // Borrowed reference
};

inline static const char *name(_PyInterpreterFrame *frame) {
  DEBUG_CHECK(PyUnicode_Check(frame->f_code->co_name));
  return PyUnicode_AsUTF8(frame->f_code->co_name);
}

static PyObject *evalFrameTrampoline(PyThreadState *tstate,
                                     _PyInterpreterFrame *frame, int exc);

void enableEvalFrame(bool enable) {
  if (enable)
    _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                         evalFrameTrampoline);
  else
    _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(),
                                         _PyEval_EvalFrameDefault);
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
  DEBUG_TRACE("code->co_nfreevars: %zi, frame->f_code->co_nfreevars: %i",
              nfrees, frame->f_code->co_nfreevars);
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
  _PyInterpreterFrame *shadow =
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

static size_t dynamic_frame_state_extra_index = -2;

// inline static PyObject *getFrameState(PyCodeObject *code) {
//   PyObject *extra = nullptr;
//   _PyCode_GetExtra((PyObject *)code, dynamic_frame_state_extra_index,
//                    (void **)&extra);
//   return extra;
// }
//
// inline static void setFrameState(PyCodeObject *code, PyObject *extra) {
//   _PyCode_SetExtra((PyObject *)code, dynamic_frame_state_extra_index, extra);
// }

inline bool endsWith(std::string const &value, std::string const &ending) {
  if (ending.size() > value.size())
    return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static PyObject *evalFrameTrampoline(PyThreadState *tstate,
                                     _PyInterpreterFrame *frame, int exc) {
  std::string name_ = name(frame);
  DEBUG_TRACE("begin %s %s %i %i", name_.data(),
              PyUnicode_AsUTF8(frame->f_code->co_filename),
              frame->f_code->co_firstlineno, _PyInterpreterFrame_LASTI(frame));
  auto thp = PyInterpreterFrame(frame);
  // this prevents recursion/reentrancy - ie the callback stackframe brings you
  // back here
  enableEvalFrame(false);
  PyObject *result = nullptr;
  if (!rewriteCodeCb.is_none() && !endsWith(name_, "__updated")) {
    enableEvalFrame(false);
    auto maybeCode = rewriteCodeCb(thp);
    DEBUG_TRACE0("done calling rewriteCodeCb");
    if (!maybeCode.is_none()) {
      DEBUG_TRACE0("maybeCode is not None");
      if (!evalCustomCodeCb.is_none()) {
        DEBUG_TRACE0("evalCustomCodeCb is not None");
        enableEvalFrame(false);
        auto res = evalCustomCodeCb(maybeCode, thp);
        if (res && !res.is_none()) {
          DEBUG_TRACE0("res is not None");
          result = res.ptr();
        } else {
          DEBUG_TRACE0("res is None");
        }
      } else {
        auto code = (PyCodeObject *)(maybeCode.ptr());
        enableEvalFrame(true);
        result = evalCustomCode(tstate, frame, code, exc);
      }
    }
  }
  enableEvalFrame(true);
  if (!result) {
    DEBUG_TRACE0("result is None");
    result = _PyEval_EvalFrameDefault(tstate, frame, exc);
  } else {
    DEBUG_TRACE0("result is not None");
  }
  return result;
}

PYBIND11_MODULE(pyframe_eval, m) {
#define DECLARE_PYOBJ_ATTR(name)                                               \
  .def_property_readonly(                                                      \
      "f_" #name,                                                              \
      [](const PyInterpreterFrame &self) {                                     \
        auto ob = (PyObject *)self.f_##name();                                 \
        if (ob)                                                                \
          return py::reinterpret_borrow<py::object>(ob);                       \
        else                                                                   \
          return py::none().cast<py::object>();                                \
      },                                                                       \
      py::return_value_policy::reference)

  py::class_<PyInterpreterFrame>(m, "_PyInterpreterFrame") DECLARE_PYOBJ_ATTR(
      builtins) DECLARE_PYOBJ_ATTR(code) DECLARE_PYOBJ_ATTR(func)
      DECLARE_PYOBJ_ATTR(globals) DECLARE_PYOBJ_ATTR(lasti)
          DECLARE_PYOBJ_ATTR(locals)
              .def_property_readonly(
                  "previous",
                  [](const PyInterpreterFrame &self) {
                    auto ob = self.previous();
                    if (ob)
                      return py::cast(ob);
                    else
                      return py::none().cast<py::object>();
                  },
                  py::return_value_policy::reference)
              .def_property_readonly(
                  "frame_obj",
                  [](const PyInterpreterFrame &self) {
                    auto ob = self.frame_obj();
                    if (ob)
                      return py::reinterpret_borrow<py::object>((PyObject *)ob);
                    else
                      return py::none().cast<py::object>();
                  },
                  py::return_value_policy::reference)
              .def_property_readonly(
                  "localsplus",
                  [](const PyInterpreterFrame &self) {
                    auto localsplus = self.localsplus();
                    py::list localsplus_(localsplus.size());
                    for (int j = 0; j < localsplus.size(); ++j) {
                      if (localsplus[j])
                        localsplus_[j] =
                            py::reinterpret_borrow<py::object>(localsplus[j]);
                      else
                        localsplus_[j] = py::none();
                    }
                    return localsplus_;
                  },
                  py::return_value_policy::reference);
#undef DECLARE_PYOBJ_ATTR

  m.def("set_rewrite_code_callback", [](const py::object &, py::object cb) {
    rewriteCodeCb = std::move(cb);
  });
  m.def("set_eval_custom_code_callback", [](const py::object &, py::object cb) {
    evalCustomCodeCb = std::move(cb);
  });
  m.def("enable_eval_frame",
        [](const py::object &, bool enable) { enableEvalFrame(enable); });

  m.def("co_localsplusnames", [](const py::object &, const py::object &co) {
    return py::reinterpret_borrow<py::object>(
        ((PyCodeObject *)co.ptr())->co_localsplusnames);
  });
}