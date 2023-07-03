import ast
import inspect
from textwrap import dedent
from types import CodeType
from typing import Any

import pyframe_eval


def smoke_test_callback(frame):
    print("frame func", frame.f_func)
    # print(frame.f_globals)
    # print(frame.f_builtins)

    # fails with TypeError: Unable to convert function return value to a Python type! The signature was
    # 	(arg0: pyframe_eval._PyInterpreterFrame) -> object
    # print(frame.f_locals)

    print("frame code obj", frame.f_code)

    # fails with TypeError: Unable to convert function return value to a Python type! The signature was
    # 	(arg0: pyframe_eval._PyInterpreterFrame) -> object
    # print(frame.frame_obj)
    # previous frame is null...
    # print(frame.previous)
    return


pyframe_eval.set_eval_frame(None, smoke_test_callback)


def bob():
    x = 1
    y = 2
    z = x + y
    return z


def bar():
    w = bob()
    x = 1
    y = 2
    z = x + y + w
    return z


def foo():
    w = bar()
    x = 1
    y = 2
    z = x + y + w
    print(z)


foo()

pyframe_eval.set_eval_frame(None, None)


class ScaleConstants(ast.NodeTransformer):
    def __init__(self, s):
        self.s = s

    def visit_Constant(self, node: ast.Constant) -> Any:
        node.value *= self.s
        return node


def rewrite_callback(frame):
    fun = frame.f_func
    tree = ast.parse(dedent(inspect.getsource(fun)))
    assert isinstance(
        tree.body[0], ast.FunctionDef
    ), f"unexpected ast node {tree.body[0]}"
    tree = ScaleConstants(s=10).visit(tree)
    tree = ast.fix_missing_locations(tree)
    tree = ast.increment_lineno(tree, fun.__code__.co_firstlineno - 1)
    module_code_o = compile(tree, fun.__code__.co_filename, "exec")
    f_code_o = next(
        c
        for c in module_code_o.co_consts
        if type(c) is CodeType and c.co_name == fun.__name__
    )
    return f_code_o


pyframe_eval.set_eval_frame(None, rewrite_callback)

foo()
