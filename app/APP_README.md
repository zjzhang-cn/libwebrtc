
## env
### .boto
```sh
cat ~/.boto <<EOF 
[Boto]
proxy = 127.0.0.1
proxy_port = 8081
EOF
```
### PATH
```sh
cat env.sh <<EOF 
export http_proxy="http://127.0.0.1:8081"; 
export HTTP_PROXY="http://127.0.0.1:8081";
export https_proxy="http://127.0.0.1:8081"; 
export HTTPS_PROXY="http://127.0.0.1:8081"
export NO_AUTH_BOTO_CONFIG="/home/gehc/.boto"
export PATH="$HOME/webrtc/build_tools/depot_tools:$PATH"
END
```
### LDPATH
```sh
export LD_LIBRARY_PATH=$HOME/webrtc/webrtc_src_m97/src/out-debug/
export LD_LIBRARY_PATH=$HOME/webrtc/webrtc_src_m97/src/out-release/
```
### 编译DEBUG
```sh
gn gen out-debug --args="is_debug=true target_os=\"linux\" target_cpu=\"x64\" is_component_build=false is_clang=true rtc_use_h264=true rtc_include_tests=false"
ninja -C out-debug libwebrtc
```
### 编译Release
```sh
gn gen out-release --args="is_debug=false target_os=\"linux\" target_cpu=\"x64\" is_component_build=false is_clang=true rtc_use_h264=true rtc_include_tests=false"
ninja -C out-release libwebrtc

gn args out-release --list=rtc_use_h264
```
```sh
ninja -C out-debug libwebrtc && ninja -C out-debug libwebrtc
```

``