# A `dynamo` of our very own

- [What is this](#what-is-this)
- [How do you use it](#how-do-you-use-it)
- [Build/Install](#build-install)
- [Meme that metaphorically illustrates](#meme-that-metaphorically-illustrates)
- [Footnotes](#footnotes)

## What is this

This is a minimal implementation of just-in-time stackframe evaluation for CPython, i.e., similar to the [dynamo functionality](https://pytorch.org/docs/stable/dynamo/index.html) that's available in PyTorch 2.0.

It uses the same CPython features[^1] as PyTorch but includes none of the NN stuff (nor any of the interpreter stuff).
Importantly, it only does the bare minimum[^2] in C/C++ and implements the rest in python so that it's fully extensible without recompile or C/C++.

## How do you use it

However you want! There's a small [context manager](https://github.com/makslevental/pyframe_eval/blob/95ff92db445bd4e5b25ae7e00ca75c4c5357ed4b/pyframe_eval/__init__.py#L36) that takes a callback that will run for each stackframe.
For example, you can use it to just-in-time rewrite ASTs:

```python
class ScaleConstants(ast.NodeTransformer):
    def visit_Constant(self, node):
        node.value *= 10
        return node

def rewrite_callback(frame) -> CodeType:
    fun = frame.f_func
    tree = ast.parse(inspect.getsource(fun))
    new_tree = ScaleConstants().visit(tree)
    return compile(new_tree, fun.__code__.co_filename, "exec")

def bob():
    return 1 + 2

assert bob() == 3

with pyframe_eval.Dynamo(rewrite_callback):
    assert bob() == 30
```

See [test.py](tests%2Ftest.py) for the full example.

## Build/Install

```shell
pip install . -v
```

There are also wheels on the [release page](https://github.com/makslevental/pyframe_eval/releases).

## Meme that metaphorically illustrates

<p align="center">
  <img width="500" src="https://github.com/makslevental/nelli/assets/5657668/083438e2-cc4b-46c8-8887-d0cf7c1623d7  " alt="">
</p>    

## Footnotes

[^1]: [PEP 523 – Adding a frame evaluation API to CPython](https://peps.python.org/pep-0523/).
[^2]: Basically just a connection to `_PyInterpreterState_SetEvalFrameFunc`.

