#include <onnxruntime_c_api.h>
#include <iostream>
#include <vector>

int main() {
    const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    OrtEnv* env;
    ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "test", &env);
    OrtSessionOptions* opts;
    ort->CreateSessionOptions(&opts);
    OrtSession* session;
    const char* model_path = "/opt/vocalexp_gst/models/swift_f0.onnx";
    OrtStatus* status = ort->CreateSession(env, model_path, opts, &session);
    if (status != nullptr) {
        std::cerr << "Error: " << ort->GetErrorMessage(status) << std::endl;
        return 1;
    }

    size_t num_input_nodes;
    ort->SessionGetInputCount(session, &num_input_nodes);
    std::cout << "Number of inputs: " << num_input_nodes << std::endl;
    OrtAllocator* allocator;
    ort->GetAllocatorWithDefaultOptions(&allocator);

    for (size_t i = 0; i < num_input_nodes; i++) {
        char* name;
        ort->SessionGetInputName(session, i, allocator, &name);
        std::cout << "Input " << i << " name: " << name << std::endl;
        ort->AllocatorFree(allocator, name);
    }

    size_t num_output_nodes;
    ort->SessionGetOutputCount(session, &num_output_nodes);
    std::cout << "Number of outputs: " << num_output_nodes << std::endl;
    for (size_t i = 0; i < num_output_nodes; i++) {
        char* name;
        ort->SessionGetOutputName(session, i, allocator, &name);
        std::cout << "Output " << i << " name: " << name << std::endl;
        ort->AllocatorFree(allocator, name);
    }

    ort->ReleaseSession(session);
    ort->ReleaseSessionOptions(opts);
    ort->ReleaseEnv(env);
    return 0;
}
