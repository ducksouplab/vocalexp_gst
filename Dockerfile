FROM ducksouplab/ducksoup:ducksoup_plugins_gst1.28.0

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies for building GStreamer plugins
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# Fix GStreamer path mismatch in the base image
RUN mkdir -p /gstreamer && ln -s /opt/gstreamer /gstreamer/install

# Set the working directory for the plugin
WORKDIR /opt/vocalexp_gst

# Copy the source code into the image
COPY . .

# Build the vocalexp_gst plugin
# We build in Release mode and use all available cores for compilation
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

# (Optional) Run unit tests to ensure the DSP and GStreamer element are functional
RUN ctest --test-dir build --output-on-failure

# Set GST_PLUGIN_PATH so GStreamer can find the vocalexp element
# This allows 'gst-inspect-1.0 vocalexp' to work immediately
ENV GST_PLUGIN_PATH=/opt/vocalexp_gst/build:${GST_PLUGIN_PATH}

# Set the final working directory
WORKDIR /workspace
