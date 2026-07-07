# ClusterFuzzLite / OSS-Fuzz build image for metl.
#
# base-builder ships Clang + the sanitizer/fuzzing toolchain and exports the
# usual OSS-Fuzz build environment ($CC, $CXX, $CFLAGS, $CXXFLAGS,
# $LIB_FUZZING_ENGINE, $SRC, $OUT). metl is header-only, so we just copy the
# repo in and hand off to build.sh, which compiles each harness in fuzz/.
FROM gcr.io/oss-fuzz-base/base-builder

COPY . $SRC/metl
WORKDIR $SRC/metl
COPY .clusterfuzzlite/build.sh $SRC/build.sh
