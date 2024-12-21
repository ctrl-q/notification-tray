import functools
import logging
from typing import Callable, ParamSpec, TypeVar

T = TypeVar("T")
P = ParamSpec("P")


def log_input_and_output(level: int):
    def decorator(func: Callable[P, T]) -> Callable[P, T]:
        @functools.wraps(func)
        def wrapper(*args: P.args, **kwargs: P.kwargs) -> T:
            logger = logging.getLogger(func.__module__)
            arg_str = ", ".join([f"{a!r}" for a in args])
            kwarg_str = ", ".join([f"{k}={v!r}" for k, v in kwargs.items()])
            full_arg_str = ", ".join(filter(None, [arg_str, kwarg_str]))
            logger.log(level, f"Calling {func.__name__}({full_arg_str})")
            output = func(*args, **kwargs)
            logger.log(level, f"Output of {func.__name__}({full_arg_str}): {output!r}")
            return output

        return wrapper

    return decorator
