// SPIR-V → MSL (Metal Shading Language) 변환 도구. docs/06-RENDERING.md 6장, ADR-0035.
// 빌드 때 tools/shader/sbx_shader_gen.py 가 단계마다 부른다 (SBX_SHADER_MSL=ON). 리소스의 Metal 번호는 생성기가 정해
// --bind 로 넘긴다 — 이 도구는 번호를 정하지 않고, 넘겨받지 못한 리소스가 셰이더에 있으면 실패한다 (번호가 어긋난 채로
// 내장되지 않게).
//
//   sbx_msl --in <a.spv> --out <a.metal> --stage vs|ps|cs --entry VSMain [--push-buffer 0]
//           [--bind <group>:<binding>:buffer|texture|sampler:<index>]...
//   stdout: {"entry":"<MSL 진입점 이름>"}
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "spirv_msl.hpp"

namespace {

struct Bind {
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::string kind;
    std::uint32_t index = 0;
};

struct Args {
    std::string in;
    std::string out;
    std::string stage;
    std::string entry;
    std::uint32_t pushBuffer = 0;
    std::vector<Bind> binds;
};

std::uint32_t parseU32(const std::string& s, const char* what) {
    std::size_t used = 0;
    const unsigned long v = std::stoul(s, &used);
    if (used != s.size() || v > 0xFFFFu) {
        throw std::runtime_error(std::string("잘못된 숫자 (") + what + "): " + s);
    }
    return static_cast<std::uint32_t>(v);
}

Bind parseBind(const std::string& spec) {
    // group:binding:kind:index
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == ':') {
            parts.push_back(spec.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 4 || (parts[2] != "buffer" && parts[2] != "texture" && parts[2] != "sampler")) {
        throw std::runtime_error("--bind 형식: <group>:<binding>:buffer|texture|sampler:<index> ('" + spec + "')");
    }
    return {parseU32(parts[0], "group"), parseU32(parts[1], "binding"), parts[2], parseU32(parts[3], "index")};
}

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (i + 1 >= argc) {
            throw std::runtime_error("값이 없는 인자: " + k);
        }
        const std::string v = argv[++i];
        if (k == "--in") {
            a.in = v;
        } else if (k == "--out") {
            a.out = v;
        } else if (k == "--stage") {
            a.stage = v;
        } else if (k == "--entry") {
            a.entry = v;
        } else if (k == "--push-buffer") {
            a.pushBuffer = parseU32(v, "push-buffer");
        } else if (k == "--bind") {
            a.binds.push_back(parseBind(v));
        } else {
            throw std::runtime_error("모르는 인자: " + k);
        }
    }
    if (a.in.empty() || a.out.empty() || a.entry.empty() || (a.stage != "vs" && a.stage != "ps" && a.stage != "cs")) {
        throw std::runtime_error("--in --out --stage vs|ps|cs --entry 가 필요하다");
    }
    return a;
}

std::vector<std::uint32_t> readSpirv(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("읽을 수 없다: " + path);
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 20 || bytes.size() % 4 != 0) {
        throw std::runtime_error("SPIR-V 가 아니다 (크기): " + path);
    }
    std::vector<std::uint32_t> words(bytes.size() / 4);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto b = [&](std::size_t k) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i * 4 + k]));
        };
        words[i] = b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
    }
    if (words[0] != 0x07230203u) {
        throw std::runtime_error("SPIR-V 가 아니다 (magic): " + path);
    }
    return words;
}

int run(const Args& a) {
    using namespace spirv_cross;
    const spv::ExecutionModel model = a.stage == "vs"   ? spv::ExecutionModelVertex
                                      : a.stage == "ps" ? spv::ExecutionModelFragment
                                                        : spv::ExecutionModelGLCompute;
    CompilerMSL msl(readSpirv(a.in));
    msl.set_entry_point(a.entry, model);

    CompilerMSL::Options opt = msl.get_msl_options();
    opt.platform = CompilerMSL::Options::macOS; // Apple Silicon Mac (macOS 13+) — 06 2장
    opt.set_msl_version(3, 0);
    opt.enable_decoration_binding = false; // 번호는 아래 표로만
    opt.argument_buffers = false;          // 직접 번호 (ADR-0035) — 인자 버퍼는 [계획]
    msl.set_msl_options(opt);

    MSLResourceBinding push;
    push.stage = model;
    push.desc_set = kPushConstDescSet;
    push.binding = kPushConstBinding;
    push.msl_buffer = a.pushBuffer;
    msl.add_msl_resource_binding(push);
    for (const Bind& b : a.binds) {
        MSLResourceBinding rb;
        rb.stage = model;
        rb.desc_set = b.group;
        rb.binding = b.binding;
        if (b.kind == "buffer") {
            rb.msl_buffer = b.index;
        } else if (b.kind == "texture") {
            rb.msl_texture = b.index;
        } else {
            rb.msl_sampler = b.index;
        }
        msl.add_msl_resource_binding(rb);
    }

    const std::string source = msl.compile();

    // 셰이더가 실제로 쓰는 리소스가 모두 표에 있었는지 (없으면 SPIRV-Cross 가 제 마음대로 번호를 붙인다)
    const ShaderResources res = msl.get_shader_resources();
    std::string missing;
    const auto check = [&](const SmallVector<Resource>& list) {
        for (const Resource& r : list) {
            const std::uint32_t set = msl.get_decoration(r.id, spv::DecorationDescriptorSet);
            const std::uint32_t binding = msl.get_decoration(r.id, spv::DecorationBinding);
            if (!msl.is_msl_resource_binding_used(model, set, binding)) {
                missing += " " + r.name + "(" + std::to_string(set) + ":" + std::to_string(binding) + ")";
            }
        }
    };
    check(res.uniform_buffers);
    check(res.storage_buffers);
    check(res.separate_images);
    check(res.separate_samplers);
    check(res.storage_images);
    check(res.sampled_images);
    if (!missing.empty()) {
        throw std::runtime_error("Metal 번호를 받지 못한 리소스:" + missing);
    }
    for (const Resource& r : res.push_constant_buffers) {
        if (!msl.is_msl_resource_binding_used(model, kPushConstDescSet, kPushConstBinding)) {
            throw std::runtime_error("push constant '" + r.name + "' 이 버퍼 번호를 받지 못했다");
        }
    }

    std::ofstream out(a.out, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("쓸 수 없다: " + a.out);
    }
    out << source;
    if (!out) {
        throw std::runtime_error("쓰기 실패: " + a.out);
    }
    const std::string entry = msl.get_cleansed_entry_point_name(a.entry, model);
    std::printf("{\"entry\":\"%s\"}\n", entry.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parseArgs(argc, argv));
    } catch (const spirv_cross::CompilerError& e) {
        std::fprintf(stderr, "sbx_msl: SPIRV-Cross: %s\n", e.what());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sbx_msl: %s\n", e.what());
    }
    return 1;
}
