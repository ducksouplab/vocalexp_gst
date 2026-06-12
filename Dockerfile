FROM ducksouplab/ducksoup:ducksoup_plugins_gst1.28.0

USER root

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgtest-dev \
    librubberband-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install ONNX Runtime C library
RUN wget https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-linux-x64-1.19.2.tgz && \
    tar -xzf onnxruntime-linux-x64-1.19.2.tgz && \
    cp -r onnxruntime-linux-x64-1.19.2/include/* /usr/local/include/ && \
    cp -r onnxruntime-linux-x64-1.19.2/lib/* /usr/local/lib/ && \
    rm -rf onnxruntime-linux-x64-1.19.2*

# Fix GStreamer path mismatch in the base image
RUN mkdir -p /gstreamer && ln -sf /opt/gstreamer /gstreamer/install

# Set the working directory for the plugin
WORKDIR /opt/vocalexp_gst

# Copy the source code into the image
COPY . .

# Download SWIFT ONNX model
RUN mkdir -p models && \
    wget -O models/swift_f0.onnx https://github.com/lars76/swift-f0/raw/main/swift_f0/model.onnx

# Build the vocalexp_gst plugin
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

# (Optional) Run unit tests
# Note: Some tests might need update for new dependencies
RUN ctest --test-dir build --output-on-failure

# Set GST_PLUGIN_PATH and LD_LIBRARY_PATH
ENV GST_PLUGIN_PATH=/opt/vocalexp_gst/build:${GST_PLUGIN_PATH}
ENV LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}

# Set the final working directory
WORKDIR /workspace
