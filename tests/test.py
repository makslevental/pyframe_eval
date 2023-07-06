import ast
import inspect
import io
from contextlib import redirect_stdout
from textwrap import dedent
from types import CodeType
from typing import Any

import pyframe_eval

k = 5


def bob():
    x = 1 + k
    y = 2
    z = x + y
    return z


class Foo:
    def bar(self, a):
        w = bob() + a
        x = 1
        y = 2
        z = x + y + w
        return z

    def __call__(self, a):
        w = self.bar(a) + a
        x = 1
        y = 2
        z = x + y + w
        return z


foo = Foo()


def smoke_test_callback(frame):
    print(
        frame.f_func.__name__,
        frame.f_code.co_filename,
        frame.f_code.co_firstlineno,
    )
    return frame.f_code


f = io.StringIO()
with redirect_stdout(f), pyframe_eval.Dynamo(smoke_test_callback):
    r = foo(1)
    assert r == 16

assert f.getvalue() == dedent(
    f"""\
{Foo.__call__.__name__} {Foo.__call__.__code__.co_filename} {Foo.__call__.__code__.co_firstlineno}
{Foo.bar.__name__} {Foo.bar.__code__.co_filename} {Foo.bar.__code__.co_firstlineno}
{bob.__name__} {bob.__code__.co_filename} {bob.__code__.co_firstlineno}
{pyframe_eval.Dynamo.__exit__.__name__} {pyframe_eval.Dynamo.__exit__.__code__.co_filename} {pyframe_eval.Dynamo.__exit__.__code__.co_firstlineno}
"""
)


class ScaleConstants(ast.NodeTransformer):
    def __init__(self, s):
        self.s = s

    def visit_Constant(self, node: ast.Constant) -> Any:
        if node.value:
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


with pyframe_eval.Dynamo(rewrite_callback):
    r = foo(2)
    assert r == 99
