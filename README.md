Welcome to the libuv_tcp wiki!
-------
# <i class="icon-hdd"></i>Introduction

libuv_tcp is an C++ class that include tcp server and client which package using libuv.

# <i class="icon-hdd"></i>Build the demo

## <i class="icon-hdd"></i>Build for Windows using Visual Studio

```
cd libuv_tcp 
mkdir -p build_win64
cd build_win64
cmake -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

## <i class="icon-hdd"></i>Build for Linux

```
cd libuv_tcp 
mkdir -p build_linux
cd build_linux
cmake ..
make -j
```

# <i class="icon-hdd"></i>Usage

Refer to [**test_tcpserver.cpp**] [1] and [**test_tcpclient.cpp**] [1]

# <i class="icon-hdd"></i>Author:  Phata

Blog: http://www.cnblogs.com/wqvbjhc/

E-mail: feitemj@gmail.com

QQ: 1930952842

Github: https://github.com/wqvbjhc



 [1]: https://github.com/wqvbjhc/libuv_tcp/blob/master/test_tcpserver.cpp/ "TCPServer Sample"
 
 [2]: https://github.com/wqvbjhc/libuv_tcp/blob/master/test_tcpclient.cpp "TCPClient Sample"
