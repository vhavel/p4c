FROM p4lang/behavioral-model:latest
LABEL maintainer="P4 Developers <p4-dev@lists.p4.org>"

# Default to using 2 make jobs, which is a good default for CI. If you're
# building locally or you know there are more cores available, you may want to
# override this.
ARG MAKEFLAGS=-j2

# Select the type of image we're building. Use `build` for a normal build, which
# is optimized for image size. Use `test` if this image will be used for
# testing; in this case, the source code and build-only dependencies will not be
# removed from the image.
ARG IMAGE_TYPE=build
# Whether to do a unified build.
ARG ENABLE_UNIFIED_COMPILATION=ON
# Whether to enable translation validation
ARG VALIDATION=OFF
# This creates a release build that includes link time optimization and links
# all libraries statically.
ARG BUILD_STATIC_RELEASE=OFF
# Toggle usage of the GNU Multiple Precision Arithmetic Library.
ARG ENABLE_GMP=ON
# No questions asked during package installation.
ARG DEBIAN_FRONTEND=noninteractive
# Whether to install dependencies required to run PTF-ebpf tests
ARG INSTALL_PTF_EBPF_DEPENDENCIES=OFF
# List of kernel versions to install supporting packages for PTF-ebpf tests
ARG KERNEL_VERSIONS
# Whether to build the P4Tools back end and platform.
ARG ENABLE_TEST_TOOLS=OFF
# Whether to treat warnings as errors.
ARG ENABLE_WERROR=ON

# Delegate the build to tools/ci-build.
COPY . /p4c/
RUN chmod u+x /p4c/tools/ci-build.sh && /p4c/tools/ci-build.sh
# Set the workdir after building p4c.
WORKDIR /p4c/
