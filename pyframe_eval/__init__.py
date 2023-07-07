import functools
import inspect
import types

from ._pyframe_eval import (
    add_to_skiplist,
    enable_eval_frame,
    set_eval_custom_code_callback,
    set_rewrite_code_callback,
)


def copy_func(f, new_code):
    """Based on http://stackoverflow.com/a/6528148/190597 (Glenn Maynard)"""
    g = types.FunctionType(
        new_code,
        f.__globals__,
        name=f.__name__,
        argdefs=f.__defaults__,
        closure=f.__closure__,
    )
    g = functools.update_wrapper(g, f)
    g.__kwdefaults__ = f.__kwdefaults__
    g.__dict__.update(f.__dict__)
    return g


def eval_custom_code(new_code, frame):
    new_code = new_code.replace(co_name=new_code.co_name + "__updated")
    updated_f = copy_func(frame.f_func, new_code)
    arg_info = inspect.getargvalues(frame)
    localsplus = dict(zip(frame.localsplusnames, frame.localsplus))
    posargs = [localsplus[a] for a in arg_info.args]
    varargs = localsplus[arg_info.varargs] if arg_info.varargs is not None else []
    kwargs = localsplus[arg_info.keywords] if arg_info.keywords is not None else {}
    enable_eval_frame(None, True)
    return updated_f(*posargs, *varargs, **kwargs)


class Dynamo:
    def __init__(self, callback, skips=None):
        if skips is None:
            skips = []
        self.callback = callback
        if len(skips):
            add_to_skiplist(None, skips)

    def __enter__(self):
        set_rewrite_code_callback(None, self.callback)
        set_eval_custom_code_callback(None, eval_custom_code)
        enable_eval_frame(None, True)

    def __exit__(self, exc_type, exc_val, exc_tb):
        set_rewrite_code_callback(None, None)
        set_eval_custom_code_callback(None, None)
        enable_eval_frame(None, False)
