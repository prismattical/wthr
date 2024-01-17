# Weather server

## Description

A socket server written in C. It sends a weather forecast at 00:00 every day to all clients.

## Build

Dependencies: json-c, curl, check

```
cmake -S . -B build
cmake --build build
```
