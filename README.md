# remote-clip

### build
```
gcc server.c -lpthread -o server
gcc client.c -lX11 -lpthread -o client
```

ubuntu 提示没有x11, 可执行:
```
sudo apt-get install libx11-dev
```


### 参考
https://github.com/exebook/x11clipboard.git
https://github.com/astrand/xclip.git
