#include <exception>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <regex>
#include <string>
#include <vector>

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
static std::vector<std::string> skipList = {".*pyframe_eval/__init__.*",
                                            ".*lib/python3.11/\\w+?py"};
static std::set<std::string> skipListCache = {};

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

bool skip(const std::string &maybe) {
  if (endsWith(maybe, "__updated") || skipListCache.count(maybe)) {
    DEBUG_TRACE("skipping rewriting %s", maybe.c_str());
    return true;
  }
  if (std::any_of(skipList.cbegin(), skipList.cend(),
                  [&maybe](const std::string &skip) {
                    DEBUG_TRACE("checking %s against %s", skip.c_str(),
                                maybe.c_str());
                    const std::regex skip_regex(skip);
                    return std::regex_match(maybe, skip_regex);
                  })) {
    DEBUG_TRACE("skipping rewriting %s", maybe.c_str());
    skipListCache.insert(maybe);
    return true;
  }
  return false;
}

static void pyPrintStr(PyObject *o) {
  PyObject_Print(o, stdout, Py_PRINT_RAW);
  printf("\n");
}

static void pyPrintRepr(PyObject *o) {
  PyObject_Print(o, stdout, 0);
  printf("\n");
}

#define COPY_FIELD(f1, f2, field)                                              \
  Py_XINCREF((f2)->func_##field);                                              \
  (f1)->func_##field = (f2)->func_##field;

// Not actually copied from CPython, but loosely based on
// https://github.com/python/cpython/blob/e715da6db1d1d70cd779dc48e1ba8110c51cc1bf/Objects/funcobject.c
// Makes a new PyFunctionObject copy of `o`, but with the code object fields
// determined from `code`.
// Ensure that all fields defined in the PyFunctionObject struct in
// https://github.com/python/cpython/blob/e715da6db1d1d70cd779dc48e1ba8110c51cc1bf/Include/cpython/funcobject.h
// are accounted for.
PyFunctionObject *_PyFunction_CopyWithNewCode(PyFunctionObject *o,
                                              PyCodeObject *code) {
  PyFunctionObject *op = PyObject_GC_New(PyFunctionObject, &PyFunction_Type);
  if (op == nullptr) {
    throw py::value_error("Couldn't allocate new function object.");
  }
  Py_XINCREF(code);
  op->func_code = (PyObject *)code;
  Py_XINCREF(code->co_name);
  op->func_name = code->co_name;
  Py_XINCREF(code->co_qualname);
  op->func_qualname = code->co_qualname;
  COPY_FIELD(op, o, globals);
  COPY_FIELD(op, o, builtins);
  COPY_FIELD(op, o, defaults);
  COPY_FIELD(op, o, kwdefaults);
  COPY_FIELD(op, o, closure);
  COPY_FIELD(op, o, doc);
  COPY_FIELD(op, o, dict);
  op->func_weakreflist = nullptr;
  COPY_FIELD(op, o, module);
  COPY_FIELD(op, o, annotations);
  op->vectorcall = o->vectorcall;
  op->func_version = o->func_version;
  PyObject_GC_Track(op);
  return op;
}

inline static PyObject *evalCustomCode(PyThreadState *tstate,
                                       _PyInterpreterFrame *frame,
                                       PyCodeObject *code, int throw_flag) {

  // Generate Python function object and _PyInterpreterFrame in a way similar to
  // https://github.com/python/cpython/blob/e715da6db1d1d70cd779dc48e1ba8110c51cc1bf/Python/ceval.c#L1130
  DEBUG_TRACE0("copying func");
  PyFunctionObject *func =
      _PyFunction_CopyWithNewCode((PyFunctionObject *)frame->f_func, code);
  DEBUG_TRACE0("copied func");
  Py_INCREF(func);
  PyFrameObject *shadow =
      PyFrame_New(tstate, code, frame->f_globals, frame->f_locals);

  // consumes reference to func
  _PyFrame_InitializeSpecials(shadow->f_frame, func, nullptr,
                              code->co_nlocalsplus);

  PyObject **fastlocals_old = frame->localsplus;
  PyObject **fastlocals_new = shadow->f_frame->localsplus;

  // localsplus are XINCREF'd by default eval frame, so all values must be
  // something (XINCREF allows for null).
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
    Py_DECREF(func);
    throw py::value_error("Couldn't create localsplus name dict.");
  }

  for (Py_ssize_t i = 0; i < code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(code->co_localsplusnames, i);
    PyObject *idx = PyLong_FromSsize_t(i);
    if (name == nullptr || idx == nullptr ||
        PyDict_SetItem(name_to_idx, name, idx) != 0) {
      Py_DECREF(name_to_idx);
      Py_DECREF(func);
      throw py::value_error("Couldn't get name or get idx or set name to "
                            "idx in name_to_idx dict.");
    }
  }

  for (Py_ssize_t i = 0; i < frame->f_code->co_nlocalsplus; i++) {
    PyObject *name = PyTuple_GET_ITEM(frame->f_code->co_localsplusnames, i);
    PyObject *idx = PyDict_GetItem(name_to_idx, name);
    Py_ssize_t new_i = PyLong_AsSsize_t(idx);
    if (name == nullptr || idx == nullptr ||
        (new_i == (Py_ssize_t)-1 && PyErr_Occurred() != nullptr)) {
      Py_DECREF(name_to_idx);
      Py_DECREF(func);
      throw py::value_error("Couldn't get name or get idx or new_i.");
    }
    Py_XINCREF(fastlocals_old[i]);
    fastlocals_new[new_i] = fastlocals_old[i];
  }

  Py_DECREF(name_to_idx);

  DEBUG_TRACE("evaling using default %s", name(frame));
  enableEvalFrame(true);
  PyObject *result =
      _PyEval_EvalFrameDefault(tstate, shadow->f_frame, throw_flag);

  Py_DECREF(func);

  return result;
}

static PyObject *evalFrameTrampoline(PyThreadState *tstate,
                                     _PyInterpreterFrame *frame, int exc) {
  std::string name_ = name(frame);
  auto filename = PyUnicode_AsUTF8(frame->f_code->co_filename);
  DEBUG_TRACE("begin trampoline %s %s %i %i", name_.data(), filename,
              frame->f_code->co_firstlineno, _PyInterpreterFrame_LASTI(frame));
  if (exc) {
    DEBUG_TRACE("throw %s", name(frame));
    return _PyEval_EvalFrameDefault(tstate, frame, exc);
  }
  auto thp = PyInterpreterFrame(frame);
  // this prevents recursion/reentrancy - ie the callback stackframe brings you
  // back here
  enableEvalFrame(false);
  PyObject *result = nullptr;
  if (!rewriteCodeCb.is_none() && !skip(filename) && !skip(name_)) {
    try {
      auto maybeCode = rewriteCodeCb(thp);
      DEBUG_TRACE0("done calling rewriteCodeCb");
      if (!maybeCode.is_none()) {
        DEBUG_TRACE0("maybeCode is not None");
        if (!evalCustomCodeCb.is_none()) {
          DEBUG_TRACE0("evalCustomCodeCb is not None");
          auto res = evalCustomCodeCb(maybeCode, thp);
          result = res.release().ptr();
        } else {
          DEBUG_TRACE0("calling evalCustomCode");
          result = evalCustomCode(tstate, frame,
                                  (PyCodeObject *)(maybeCode.ptr()), exc);
        }
      }
    } catch (py::error_already_set &e) {
      // https://github.com/pybind/pybind11/issues/1498
      e.restore();
      return nullptr;
    }
  }
  enableEvalFrame(true);
  if (!result) {
    DEBUG_TRACE0("result is None");
    result = _PyEval_EvalFrameDefault(tstate, frame, exc);
  } else {
    DEBUG_TRACE0("result is not None");
    DEBUG_NULL_CHECK(result);
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
                  py::return_value_policy::reference)
              .def_property_readonly(
                  "localsplusnames",
                  [](const PyInterpreterFrame &self) {
                    return py::reinterpret_borrow<py::object>(
                        self.frame->f_code->co_localsplusnames);
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
  m.def("add_to_skiplist", [](const py::object &, const std::string &skip) {
    skipList.push_back(skip);
  });
  m.def("add_to_skiplist",
        [](const py::object &, const std::vector<std::string> &skips) {
          skipList.insert(skipList.end(), skips.begin(), skips.end());
        });
}