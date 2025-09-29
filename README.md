# UiTest_Agent_VNC

## Libvncserver Build
```shell
apt-get update && apt-get install -y build-essential autoconf libtool pkg-config libssl-dev make cmake

mkdir -p /tmp/libvncserver_build_ohos
git clone -b LibVNCServer-0.9.15 https://github.com/LibVNC/libvncserver.git
cd libvncserver
git submodule update --init

# Build libjpeg
cd deps
git clone -b 3.1.2 https://github.com/libjpeg-turbo/libjpeg-turbo.git
cd libjpeg-turbo
$OHOS_SDK/native/build-tools/cmake/bin/cmake -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
      -DOHOS_ARCH=x86_64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="/tmp/libvncserver_build_ohos/libjpeg" \
      .
make "-j$(nproc)" install
cd ../../

# Build libpng
cd deps
git clone -b v1.6.50 https://github.com/pnggroup/libpng.git
cd libpng
$OHOS_SDK/native/build-tools/cmake/bin/cmake -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
      -DOHOS_ARCH=x86_64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-I$OHOS_SDK/native/sysroot/usr/include/x86_64-linux-ohos" \
      -DCMAKE_INSTALL_PREFIX="/tmp/libvncserver_build_ohos/libpng" \
      .
make "-j$(nproc)" install
cd ../../

# Build Libvncserver
mkdir -p "build"
pushd "build"
$OHOS_SDK/native/build-tools/cmake/bin/cmake -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
      -DOHOS_ARCH=x86_64 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/tmp/libvncserver_build_ohos/libvncserver \
      -DJPEG_INCLUDE_DIR="/tmp/libvncserver_build_ohos/libjpeg/include" \
      -DJPEG_LIBRARY="/tmp/libvncserver_build_ohos/libjpeg/lib/libjpeg.a" \
      -DPNG_PNG_INCLUDE_DIR="/tmp/libvncserver_build_ohos/libpng/include" \
      -DPNG_LIBRARY="/tmp/libvncserver_build_ohos/libpng/libpng.a" \
      -DWITH_OPENSSL=OFF \
      -DWITH_GCRYPT=OFF \
      -DBUILD_SHARED_LIBS=OFF \
      -DWITH_TESTS=OFF \
      -DWITH_EXAMPLES=OFF \
      ..
make "-j$(nproc)" install
popd

zip -r libvncserver_build_ohos.zip /tmp/libvncserver_build_ohos
```

## Project Build
```shell
mkdir -p "build"
pushd "build"
$OHOS_SDK/native/build-tools/cmake/bin/cmake -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
        -G "Ninja" \
        -D OHOS_STL=c++_shared \
        -D OHOS_ARCH=x86_64 \
        -D OHOS_PLATFORM=OHOS \
        -D CMAKE_INSTALL_PREFIX="" \
        -D CMAKE_MAKE_PROGRAM=$OHOS_SDK/native/build-tools/cmake/bin/ninja \
        ..
cmake --build .
popd
```

## Usage
```shell
hdc tconn 172.16.0.156:5555
hdc file send libagent.so /data/local/tmp/agent.so

hdc shell "(pidof uitest | xargs -r kill -9) ; /system/bin/uitest start-daemon singleness"
hdc shell "hilog | grep UiTestKit"

hdc fport tcp:5900 tcp:5900
```