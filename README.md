

```
cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug 
```

```
cmake -B build/release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/release 
```

Track Epoll calls:
```
strace -e epoll_create1,epoll_ctl,epoll_wait ./build/debug/server

```
