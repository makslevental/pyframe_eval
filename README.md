# A `dynamo` of our very own

* [Explanation](#explanation)
* [Build/Install](#build-install)
* [Demo](#demo)
* [Meme that metaphorically illustrates](#meme-that-metaphorically-illustrates)

## Explanation

This repo is a dirty implementation of the the same [dynamo functionality](https://pytorch.org/docs/stable/dynamo/index.html) that's available in PyTorch 2.0, i.e., custom stackframe evaluation/jitting.

It uses the same exact CPython features[^1] as PyTorch but includes none of the NN stuff (nor any guarantees).
Importantly, it only does the bare minimum[^2] in C/C++ and implements the rest in python so that it's fully extensible without recompile or C/C++.

## Build/Install

```shell
pip install . -v
```

There are also wheels on the [release page](https://github.com/makslevental/pyframe_eval/releases).

## Demo

See [test.py](tests%2Ftest.py).

[^1]: [PEP 523 – Adding a frame evaluation API to CPython](https://peps.python.org/pep-0523/).
[^2]: Basically just a connection to `_PyInterpreterState_SetEvalFrameFunc`.

## Meme that metaphorically illustrates

<p align="center">
  <img width="500" src="https://github.com/makslevental/nelli/assets/5657668/083438e2-cc4b-46c8-8887-d0cf7c1623d7  " alt="">
</p>    

