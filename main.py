import inspect
from contextlib import contextmanager
from typing import Protocol, Optional, Union

print(inspect.currentframe().f_code.co_name)

import pyframe_eval


def callback(*args, **kwargs):
    print(args, kwargs)


@contextmanager
def cm():
    pyframe_eval.set_eval_frame(None, callback)
    yield
    pyframe_eval.set_eval_frame(None)


pyframe_eval.set_eval_frame(None, callback)


def foo():
    x = 1
    y = 2
    z = x + y


foo()
