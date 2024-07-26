import logging
from typing import List, Optional

_default_handlers : List[logging.Handler] =[]
_default_level=logging.DEBUG

"""
standard logging set-up to be used throughout
"""
def setup_logger(logger : logging.Logger, additional_handlers : List[logging.Handler] =[]):

    fmt=logging.Formatter(
        fmt='[%(asctime)s] [%(module)s] %(message)s',
        datefmt='%Y-%m-%d_%H-%M-%S.%f'
    ) 

    logger.setLevel(_default_level)

    for h in _default_handlers + additional_handlers:
        h.setFormatter(fmt)
        logger.addHandler(h)

def get_logger(name : str):
    L=logging.getLogger(name)
    setup_logger(L)
    return L

def add_default_handlers(handlers : List[logging.Handler]):
    global _default_handlers
    _default_handlers.extend(handlers)
