# Windows x64 Release Build

The r2 release provides the tested single EXE, the tagged melonDS source,
and a separate dependency source archive. Download the archive from the
[r2 release](https://github.com/ATEMVEGETA/melonDS-Connectivity/releases/tag/v1.1-connectivity-r2).
It contains the preserved dependency source tarballs, pinned vcpkg recipes with their
patches, installed package version/feature inventory, and SHA-256 checksums.
Dependency license notices are in [licenses](licenses).

The binary was compiled with MSYS2 UCRT64 GCC 16.1.0 and statically linked
Qt 6.8.3. The manifest and the `x64-mingw-static-release` overlay triplet in
this repository specify the library build. The vcpkg baseline is
`dd3097e305afa53f7b4312371f62058d2e665320`.

Install Git, GCC, CMake and Ninja in an MSYS2 UCRT64 environment. In a terminal
where those tools are on PATH, check out the release tag and prepare vcpkg:

```bash
git clone https://github.com/ATEMVEGETA/melonDS-Connectivity.git
cd melonDS-Connectivity
git checkout v1.1-connectivity-r2
git clone https://github.com/Microsoft/vcpkg.git ../vcpkg-connectivity
git -C ../vcpkg-connectivity checkout dd3097e305afa53f7b4312371f62058d2e665320
../vcpkg-connectivity/bootstrap-vcpkg.bat -disableMetrics
```

For a dependency source cache, extract the dependency archive and set
`VCPKG_DOWNLOADS` to the absolute path to its `DependencySources` directory.
The vcpkg manifest can also fetch its pinned inputs normally. The archive is
not an offline toolchain installer. The vcpkg recipes are also included in
`DependencyRecipes` for inspection.

Configure with the release flags and build:

```bash
cmake -S . -B build-connectivity -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -DUSE_QT6=ON \
  -DPORTABLE=ON -DUSE_VCPKG=ON -DVCPKG_ROOT="$(cygpath -am ../vcpkg-connectivity)" \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static-release \
  -DVCPKG_HOST_TRIPLET=x64-mingw-static-release \
  -DVCPKG_APPLOCAL_DEPS=OFF -DENABLE_DEBUG_DEPS=OFF \
  -DENABLE_LTO=OFF -DENABLE_LTO_RELEASE=OFF \
  '-DMELONDS_VERSION_SUFFIX= - Connectivity r2 (unofficial)' \
  -DMELONDS_EMBED_BUILD_INFO=ON -DMELONDS_GIT_BRANCH=connectivity-1.1 \
  -DMELONDS_GIT_HASH=b86390e4428bf38ce4c1ce0e9ca446d6d25955e8 \
  -DMELONDS_BUILD_PROVIDER=Connectivity
cmake --build build-connectivity --target melonDS
```

The release used a previously built static vcpkg cache. A fresh build from
these inputs is not claimed to be byte-identical or tested offline.
The SHA-256 published with the release identifies the distributed binary.
