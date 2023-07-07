import ast
import inspect
import io
from contextlib import redirect_stdout
from textwrap import dedent
from types import CodeType
from typing import Any

import pyframe_eval


def bob():
    k = 5

    def bob1():
        x = 1 + k
        y = 2
        z = x + y
        return z

    return bob1()


class Foo:
    def bar(self, a):
        w = bob() + a
        x = 1
        y = 2
        z = x + y + w
        return z

    def __call__(self, a, *args, **kwargs):
        w = self.bar(a) + a
        x = 1
        y = 2
        z = x + y + w
        return z


foo = Foo()


def smoke_test_callback(frame):
    print(inspect.getargvalues(frame))
    print(
        frame.f_func.__name__,
        frame.f_code.co_filename,
        frame.f_code.co_firstlineno,
        frame.localsplusnames,
        frame.localsplus,
    )
    return frame.f_code


with pyframe_eval.Dynamo(smoke_test_callback, skips=["__exit__"]):
    print(foo(1, 2, 3, b=4))

# f = io.StringIO()
with pyframe_eval.Dynamo(smoke_test_callback, skips=["__exit__"]):
    r = foo(1)
    assert r == 16

# assert f.getvalue() == dedent(
#     f"""\
# {Foo.__call__.__name__} {Foo.__call__.__code__.co_filename} {Foo.__call__.__code__.co_firstlineno}
# {Foo.bar.__name__} {Foo.bar.__code__.co_filename} {Foo.bar.__code__.co_firstlineno}
# {bob.__name__} {bob.__code__.co_filename} {bob.__code__.co_firstlineno}
# """
# )


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


# with pyframe_eval.Dynamo(rewrite_callback):
#     r = foo(2)
#     assert r == 99


def hasposonly(a, b, c):
    print("posonly", a, b, c)


hasposonly(1, 2, 3)

with pyframe_eval.Dynamo(rewrite_callback):
    hasposonly(1, 2, 3)


def hasposonlyandvar(a, b, c, *args):
    print("posandvar", a, b, c, *args)


hasposonlyandvar(1, 2, 3, 4, 5, 6)

with pyframe_eval.Dynamo(rewrite_callback):
    hasposonlyandvar(1, 2, 3, 4, 5, 6)


def hasposonlyandkwargs(a, b, c, **kwargs):
    print("posandkwargs", a, b, c, kwargs)


hasposonlyandkwargs(1, 2, 3, d=4, e=5, f=6)

with pyframe_eval.Dynamo(rewrite_callback):
    hasposonlyandkwargs(1, 2, 3, d=4, e=5, f=6)


def hasposonlyandvarandkwargs(a, b, c, *args, **kwargs):
    print("posandvarandkwargs", a, b, c, args, kwargs)


hasposonlyandvarandkwargs(1, 2, 3, 4, 5, 6, d=7, e=8, f=9)

with pyframe_eval.Dynamo(rewrite_callback):
    hasposonlyandvarandkwargs(1, 2, 3, 4, 5, 6, d=7, e=8, f=9)


def hasvarargs(*args):
    print("varargs", args)


hasvarargs(1, 2, 3)

with pyframe_eval.Dynamo(rewrite_callback):
    hasvarargs(1, 2, 3)


def haskwargs(**kwargs):
    print("kwargs", kwargs)


haskwargs(a=1, b=2, c=3)
with pyframe_eval.Dynamo(rewrite_callback):
    haskwargs(a=1, b=2, c=3)


def hasboth(*args, **kwargs):
    print("both", args, kwargs)


hasboth(1, 2, 3, a=4, b=5, c=6)
with pyframe_eval.Dynamo(rewrite_callback):
    hasboth(1, 2, 3, a=4, b=5, c=6)
