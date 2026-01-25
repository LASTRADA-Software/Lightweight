FROM yaraslaut/clang-p2996:latest

COPY . /mnt/src
WORKDIR /mnt
RUN sudo apt update
RUN sudo apt install -y git cmake ninja-build unixodbc-dev sqlite3 libsqlite3-dev libsqliteodbc uuid-dev pkg-config zip unzip clang zlib1g-dev
RUN git clone https://github.com/microsoft/vcpkg.git
WORKDIR /mnt/vcpkg
RUN sh bootstrap-vcpkg.sh
WORKDIR /mnt/src

RUN echo "v1.2.34" >> version.txt
RUN CXX="clang++" CC="clang" CXXFLAGS="-freflection-latest -stdlib=libc++ " CFLAGS="-stdlib=libc++" LDFLAGS="-stdlib=libc++" \
    cmake -S . -B build -G Ninja  \
    -D LIGHTWEIGHT_CXX26_REFLECTION=ON \
    -D CMAKE_TOOLCHAIN_FILE=/mnt/vcpkg/scripts/buildsystems/vcpkg.cmake
RUN cmake --build build --target LightweightTest
RUN ./build/src/tests/LightweightTest
