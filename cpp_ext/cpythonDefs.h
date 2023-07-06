#pragma once

#include <Python.h>

// Functions that need to be copied from the CPython source
// should go in cpython_defs.c. Copying is required when, e.g.,
// we need to call internal CPython functions that are not exposed.

#include <internal/pycore_frame.h>

int _PyFrame_FastToLocalsWithError(_PyInterpreterFrame *frame);

PyFunctionObject *_PyFunction_CopyWithNewCode(PyFunctionObject *o,
                                              PyCodeObject *code);

void _PyFrame_Clear(_PyInterpreterFrame *frame);
