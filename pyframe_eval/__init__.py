from types import FunctionType
from ._pyframe_eval import (
    set_rewrite_code_callback,
    enable_eval_frame,
    set_eval_custom_code_callback,
)


def rebuild_func(new_code, frame):
    new_code = new_code.replace(co_name=new_code.co_name + "__updated")
    updated_f = FunctionType(
        code=new_code,
        globals={
            **frame.f_func.__globals__,
            **{
                fr: frame.f_func.__closure__[i].cell_contents
                for i, fr in enumerate(frame.f_func.__code__.co_freevars)
            },
        },
        name=frame.f_func.__name__,
        argdefs=frame.f_func.__defaults__,
    )
    updated_f.__qualname__ = frame.f_func.__qualname__
    updated_f.__annotations__ = frame.f_func.__annotations__

    return updated_f


def eval_custom_code(new_code, frame):
    updated_f = rebuild_func(new_code, frame)
    args = frame.localsplus[: new_code.co_argcount]
    enable_eval_frame(None, True)
    return updated_f(*args)


class Dynamo:
    def __init__(self, callback):
        self.callback = callback

    def __enter__(self):
        set_rewrite_code_callback(None, self.callback)
        set_eval_custom_code_callback(None, eval_custom_code)
        enable_eval_frame(None, True)

    def __exit__(self, exc_type, exc_val, exc_tb):
        set_rewrite_code_callback(None, None)
        set_eval_custom_code_callback(None, None)
        enable_eval_frame(None, False)
