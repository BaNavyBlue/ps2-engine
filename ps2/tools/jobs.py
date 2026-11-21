"""(build 114) Runs in parallel, their results in order.

`ordered(fn, tasks, jobs)` calls fn(task) for every task in `jobs` worker
processes and yields the results in the tasks' order, whatever order they
finish in, so a tool's output is the same at any --jobs. The workers are
spawned, not forked, on every OS (Windows can only spawn): each starts
fresh, with none of the parent's module state -- a trial's set-up stage
(walk_trials --setup) leaves nothing behind for its runs -- so a worker
runs a task as any other worker would. fn must be a module-level function
(a spawned worker finds it by name).

Each worker knows its own index, 0 to jobs - 1 (`worker()`), for a probe
session of its own: two runs at once must never share route_out/<name>/,
the session's log, stderr capture and saves.
"""
import concurrent.futures
import multiprocessing
import os

_INDEX = 0


def _init(indices):
    global _INDEX
    _INDEX = indices.get()


def worker():
    """This worker's index, 0 .. jobs - 1 (0 outside a pool)."""
    return _INDEX


def ordered(fn, tasks, jobs):
    """fn(task) for every task, in up to `jobs` processes; the results in
    the tasks' order. An exception in a task is raised where its result
    would have come."""
    tasks = list(tasks)
    if not tasks:
        return
    jobs = max(1, min(jobs, len(tasks)))
    ctx = multiprocessing.get_context("spawn")
    indices = ctx.Queue()
    for i in range(jobs):
        indices.put(i)
    # Every worker hashes strings with one seed, playthrough's (0) unless
    # the caller set one: a few of the harness's choices take the first of
    # equals from a set of names, and a run must come out the same in
    # whichever worker plays it. Workers are spawned as tasks arrive, so
    # the seed stays in the environment until the pool is done.
    had = os.environ.get("PYTHONHASHSEED")
    os.environ["PYTHONHASHSEED"] = had if had is not None else "0"
    pool = concurrent.futures.ProcessPoolExecutor(
        max_workers=jobs, mp_context=ctx, initializer=_init,
        initargs=(indices,))
    try:
        yield from pool.map(fn, tasks)
    finally:
        # A task's exception, or the caller stopping early: the tasks not
        # yet started are dropped, the running ones finish.
        pool.shutdown(wait=True, cancel_futures=True)
        if had is None:
            del os.environ["PYTHONHASHSEED"]
