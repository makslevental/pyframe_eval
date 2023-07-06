#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>

namespace py = pybind11;

#define Py_BUILD_CORE
#include <internal/pycore_frame.h>
#undef Py_BUILD_CORE

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

// #define DEBUG

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
    auto maybeCode = rewriteCodeCb(thp);
    DEBUG_TRACE0("done calling rewriteCodeCb");
    if (!maybeCode.is_none()) {
      DEBUG_TRACE0("maybeCode is not None");
      if (!evalCustomCodeCb.is_none()) {
        DEBUG_TRACE0("evalCustomCodeCb is not None");
        auto res = evalCustomCodeCb(maybeCode, thp);
        if (res && !res.is_none()) {
          DEBUG_TRACE0("res is not None");
          result = res.ptr();
        } else {
          DEBUG_TRACE0("res is None");
        }
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

PYBIND11_MODULE(_pyframe_eval, m) {
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