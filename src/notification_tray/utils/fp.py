from typing import Callable, TypeVar

A = TypeVar("A")
B = TypeVar("B")
C = TypeVar("C")


def compose(f: Callable[[A], B], g: Callable[[B], C]) -> Callable[[A], C]:
    def composed_function(x: A) -> C:
        return g(f(x))

    return composed_function
