# ClusterFuzzLite builder. Clang, libc++, and libFuzzer come from base-builder.
# Digest-pinned so Scorecard Pinned-Dependencies accepts the image; Dependabot
# updates the digest with the docker ecosystem.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:b867812795e2dd4516002f889a9c9f5ac4c549589b9f6b38f3b7a4c6627efaa9

# No extra packages. build.sh compiles parser and tokenizer only.
COPY . $SRC/nesso
WORKDIR $SRC/nesso
COPY .clusterfuzzlite/build.sh $SRC/build.sh
