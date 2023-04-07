#include "ElemwiseHelper.h"
#include "Utils/SymbolHelper.h"
#include "compiler/Common/Logger.h"

using namespace megcc;
using namespace KernelGen;
using namespace GeneralIntrinsic;

namespace {
template <BcastType BT>
std::string BinaryCode();

template <>
std::string BinaryCode<VEC_VEC>() {
    std::string body = R"(
        Layout layout = outputs[0]->layout;
        size_t SIMD_WIDTH = ${simd_width};
        size_t nr_elem = 1;
        for (int i = 0; i < layout.nr_dim; ++i) {
                nr_elem *= layout.dims[i];
        }
        ${kernel_init()}
        const ${dtype_specifier} * src0 = ${source0};
        const ${dtype_specifier} * src1 = ${source1};
        ${dtype_specifier} * dst = ${dst};
        size_t index = 0;
        for(; index + 2*SIMD_WIDTH-1 < nr_elem; index += 2*SIMD_WIDTH) {
            ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
            ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + SIMD_WIDTH);
            ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
            ${simd_dtype_specifier} vsrc1_1 = ${load_vec}(src1 + SIMD_WIDTH);
            ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_1)}
            src0 += 2*SIMD_WIDTH;
            src1 += 2*SIMD_WIDTH;
            dst += 2*SIMD_WIDTH;
        }
        for(; index +  SIMD_WIDTH-1< nr_elem; index += SIMD_WIDTH) {
            ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0);
            ${simd_dtype_specifier} vsrc1_0 =  ${load_vec}(src1);
            ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
            src0 += SIMD_WIDTH;
            src1 += SIMD_WIDTH;
            dst += SIMD_WIDTH;
        }
        for(; index < nr_elem; index++) {
            ${kernel_naive_unroll(1, dst, src0, src1)}
            src0 += 1;
            src1 += 1;
            dst += 1;
        })";
    return body;
}

template <>
std::string BinaryCode<VEC_SCALAR>() {
    std::string body = R"(
        Layout layout = outputs[0]->layout;
        size_t SIMD_WIDTH = ${simd_width};
        size_t nr_elem = 1;
        for (int i = 0; i < layout.nr_dim; ++i) {
                nr_elem *= layout.dims[i];
        }
        ${kernel_init()}
        const ${dtype_specifier} * src0 = ${source0};
        const ${dtype_specifier} * src1 = ${source1};
        ${dtype_specifier} * dst = ${dst};
        ${simd_dtype_specifier} vsrc1_0 = ${broad_cast}(src1[0]);
        size_t index = 0;
        for(; index + 2*SIMD_WIDTH-1 < nr_elem; index += 2*SIMD_WIDTH) {
            ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
            ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + SIMD_WIDTH);
            ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_0)}
            src0 +=  2*SIMD_WIDTH;
            dst +=  2*SIMD_WIDTH;
        }
        for(; index + SIMD_WIDTH-1 < nr_elem; index += SIMD_WIDTH) {
            ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
            ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
            src0 += SIMD_WIDTH;
            dst += SIMD_WIDTH;
        }
        for(; index < nr_elem; index++) {
            ${kernel_naive_unroll(1, dst, src0, src1)}
            src0 += 1;
            dst += 1;
        })";
    return body;
}

template <>
std::string BinaryCode<VEC_BCAST111C>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        size_t channel = 1;
        for(size_t i = 0; i < dst_layout.nr_dim - 1; ++i)
            channel *= dst_layout.dims[i];
        size_t channel_stride = dst_layout.dims[dst_layout.nr_dim - 1];
        ${kernel_init()}
        const ${dtype_specifier} * src0 = ${source0};
        const ${dtype_specifier} * src1 = ${source1};
        ${dtype_specifier} * dst = ${dst};
        for(size_t c=0; c<channel; c++){
            const ${dtype_specifier}* src0_ptr = src0 + c * channel_stride;
            const ${dtype_specifier}* src1_ptr = src1;
            size_t index = 0;
            for(; index + 2*SIMD_WIDTH-1 < channel_stride; index += 2*SIMD_WIDTH){
                ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0_ptr + index);
                ${simd_dtype_specifier} vsrc0_1 =  ${load_vec}(src0_ptr + index + SIMD_WIDTH);
                ${simd_dtype_specifier} vsrc1_0 =  ${load_vec}(src1_ptr + index);
                ${simd_dtype_specifier} vsrc1_1 =  ${load_vec}(src1_ptr + index + SIMD_WIDTH);
                ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_1)}
                dst += 2*SIMD_WIDTH;
            }
            for(; index + SIMD_WIDTH-1 < channel_stride; index += SIMD_WIDTH){
                ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0_ptr + index);
                ${simd_dtype_specifier} vsrc1_0 =  ${load_vec}(src1_ptr + index);
                ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                dst += SIMD_WIDTH;
            }
            for(; index < channel_stride; index++) {
                ${kernel_naive_unroll(1, dst, src0_ptr + index, src1_ptr + index)}
                ++dst;
            }
        }
        )";
    return body;
}

template <>
std::string BinaryCode<VEC_BCAST101>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        size_t batch = dst_layout.dims[0];
        size_t channel = dst_layout.dims[1];
        size_t nr_elem_per_channel = dst_layout.dims[2] * dst_layout.dims[3];
        ${kernel_init()}
        const ${dtype_specifier} * src0 = ${source0};
        const ${dtype_specifier} * src1 = ${source1};
        ${dtype_specifier} * dst = ${dst};
        for(size_t b=0; b<batch; b++){
            for(size_t c=0; c<channel; c++){
                ${simd_dtype_specifier} vsrc1_0 = ${broad_cast}(src1[c]);
                size_t index = 0;
                for(; index + 2*SIMD_WIDTH-1 < nr_elem_per_channel; index += 2*SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0);
                    ${simd_dtype_specifier} vsrc0_1 =  ${load_vec}(src0 + SIMD_WIDTH);
                    ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_0)}
                    src0 += 2*SIMD_WIDTH;
                    dst += 2*SIMD_WIDTH;
                }
                for(; index + SIMD_WIDTH-1 < nr_elem_per_channel; index += SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0);
                    ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                    src0 += SIMD_WIDTH;
                    dst += SIMD_WIDTH;
                }
                for(; index < nr_elem_per_channel; index++) {
                    ${kernel_naive_unroll(1, dst, src0, src1+c)}
                    src0 += 1;
                    dst += 1;
                }
            }
        }
        )";
    return body;
}

template <>
std::string BinaryCode<VEC_BCAST101xX>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        size_t batch = dst_layout.dims[0];
        size_t channel = dst_layout.dims[1];
        size_t nr_elem_per_channel = dst_layout.dims[2] * dst_layout.dims[3] * dst_layout.dims[4];
        ${kernel_init()}
        const ${dtype_specifier} * src0 = ${source0};
        const ${dtype_specifier} * src1 = ${source1};
        ${dtype_specifier} * dst = ${dst};
        for(size_t b=0; b<batch; b++){
            for(size_t c=0; c<channel; c++){
                ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1 + c * SIMD_WIDTH);
                size_t index = 0;
                for(; index + 2*SIMD_WIDTH-1 < nr_elem_per_channel; index += 2*SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0);
                    ${simd_dtype_specifier} vsrc0_1 =  ${load_vec}(src0 + SIMD_WIDTH);
                    ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_0)}
                    src0 += 2*SIMD_WIDTH;
                    dst += 2*SIMD_WIDTH;
                }
                for(; index + SIMD_WIDTH-1 < nr_elem_per_channel; index += SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 =  ${load_vec}(src0);
                    ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                    src0 += SIMD_WIDTH;
                    dst += SIMD_WIDTH;
                }
            }
        })";
    return body;
}

template <>
std::string BinaryCode<VEC_BV>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        Layout src_layout_0 = inputs[0]->layout;
        Layout src_layout_1 = inputs[1]->layout;
        Layout small_layout = ${reverse} == 1? src_layout_0:src_layout_1;
        //! b for broadcast, e for elemwise
        size_t batch = 1;
        size_t nr_elem_per_channel = 1;
        int be_idx = 0;
        for (int i = 0; i < dst_layout.nr_dim; ++i){
            if(small_layout.dims[i] == 1){
                batch *= dst_layout.dims[i];
            }else{
                nr_elem_per_channel *= dst_layout.dims[i];
            }
        }
        ${kernel_init()}
        const ${dtype_specifier} * src0_base = ${source0};
        const ${dtype_specifier} * src1_base = ${source1};
        ${dtype_specifier} * dst = ${dst};
        for(size_t b=0; b<batch; b++){
            const ${dtype_specifier} * src0 = src0_base + b * nr_elem_per_channel;
            const ${dtype_specifier} * src1 = src1_base;
            size_t index = 0;
            for(; index + 2*SIMD_WIDTH-1 < nr_elem_per_channel; index += 2*SIMD_WIDTH) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + SIMD_WIDTH);
                ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                ${simd_dtype_specifier} vsrc1_1 = ${load_vec}(src1 + SIMD_WIDTH);
                ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_1)}
                src0 += 2*SIMD_WIDTH;
                src1 += 2*SIMD_WIDTH;
                dst += 2*SIMD_WIDTH;
            }
            for(; index + SIMD_WIDTH-1 < nr_elem_per_channel; index += SIMD_WIDTH) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                src0 += SIMD_WIDTH;
                src1 += SIMD_WIDTH;
                dst += SIMD_WIDTH;
            }
            for(; index < nr_elem_per_channel; index++) {
                ${kernel_naive_unroll(1, dst, src0, src1)}
                src0 += 1;
                src1 += 1;
                dst += 1;
            }
        }
        )";
    return body;
}

template <>
std::string BinaryCode<VEC_BCAST110>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        Layout src_layout_0 = inputs[0]->layout;
        Layout src_layout_1 = inputs[1]->layout;
        size_t channel = 1;
        size_t nr_elem_per_channel = dst_layout.dims[dst_layout.nr_dim - 1];
        for (int i = 0; i < dst_layout.nr_dim - 1; ++i){
            channel *= dst_layout.dims[i];
        }
        ${kernel_init()}
        const ${dtype_specifier} * src0_base = ${source0};
        const ${dtype_specifier} * src1_base = ${source1};
        ${dtype_specifier} * dst = ${dst};
        for(size_t c=0; c < channel; c++){
            const ${dtype_specifier} * src0 = src0_base + c * nr_elem_per_channel;
            const ${dtype_specifier} * src1 = src1_base + c;
            ${simd_dtype_specifier} vsrc1 = ${broad_cast}(*src1);
            size_t index = 0;
            for(; index + SIMD_WIDTH * 2 - 1 < nr_elem_per_channel; index += SIMD_WIDTH * 2) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + 4);
                ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1, vsrc0_1, vsrc1)}
                src0 += SIMD_WIDTH * 2;
                dst += SIMD_WIDTH * 2;
            }
            for(; index + SIMD_WIDTH - 1 < nr_elem_per_channel; index += SIMD_WIDTH) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1)}
                src0 += SIMD_WIDTH;
                dst += SIMD_WIDTH;
            }
            for(; index < nr_elem_per_channel; index++) {
                ${kernel_naive_unroll(1, dst, src0, src1)}
                src0 += 1;
                dst += 1;
            }
        }
        )";
    return body;
}

template <>
std::string BinaryCode<NAIVE>() {
    std::string body = R"(
        Layout dst_layout = outputs[0]->layout;
        size_t nr_elem = 1;
        for (int i = 0; i < dst_layout.nr_dim; ++i) {
            nr_elem *= dst_layout.dims[i];
        }
        Layout src_layout_0 = inputs[0]->layout;
        Layout src_layout_1 = inputs[1]->layout;
        ${kernel_init()}
        
        broadcast_layout(&src_layout_0, dst_layout);
        broadcast_layout(&src_layout_1, dst_layout);
        NoconIter src0_iter = init_iter(src_layout_0);
        NoconIter src1_iter = init_iter(src_layout_1);
        const ${dtype_specifier} * src0 = inputs[0]->ptr;
        const ${dtype_specifier} * src1 = inputs[1]->ptr;
        ${dtype_specifier} * dst = outputs[0]->ptr;
        
        for(size_t index = 0; index < nr_elem; index++) {
            ${kernel_naive_unroll(1, dst, src0 + src0_iter.offset, src1 + src1_iter.offset)}
            inc_iter(src_layout_0, &src0_iter);
            inc_iter(src_layout_1, &src1_iter);
            dst++;
        }
        
    )";
    return body;
}

template <>
std::string BinaryCode<DYNAMIC_TYPE>() {
    std::string body = R"(
        size_t SIMD_WIDTH = ${simd_width};
        Layout dst_layout = outputs[0]->layout;
        size_t nr_elem = 1;
        for (int i = 0; i < dst_layout.nr_dim; ++i) {
            nr_elem *= dst_layout.dims[i];
        }
        Layout src_layout_0 = inputs[0]->layout;
        Layout src_layout_1 = inputs[1]->layout;
        size_t nr_elem_in0 = 1;
        for (int i = 0; i < src_layout_0.nr_dim; ++i) {
            nr_elem_in0 *= src_layout_0.dims[i];
        }
        size_t nr_elem_in1 = 1;
        for (int i = 0; i < src_layout_1.nr_dim; ++i) {
            nr_elem_in1 *= src_layout_1.dims[i];
        }
        ${kernel_init()}
        if (nr_elem == nr_elem_in0 && nr_elem_in0 == nr_elem_in1){
            const ${dtype_specifier} * src0 = inputs[0]->ptr;
            const ${dtype_specifier} * src1 = inputs[1]->ptr;
            ${dtype_specifier} * dst = outputs[0]->ptr;
            size_t index = 0;
            for(; index + 2*SIMD_WIDTH-1 < nr_elem; index += 2*SIMD_WIDTH) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + SIMD_WIDTH);
                ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                ${simd_dtype_specifier} vsrc1_1 = ${load_vec}(src1 + SIMD_WIDTH);
                ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_1)}
                src0 += 2*SIMD_WIDTH;
                src1 += 2*SIMD_WIDTH;
                dst += 2*SIMD_WIDTH;
            }
            for(; index + SIMD_WIDTH-1 < nr_elem; index += SIMD_WIDTH) {
                ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                src0 += SIMD_WIDTH;
                src1 += SIMD_WIDTH;
                dst += SIMD_WIDTH;
            }
            for(; index < nr_elem; index++) {
                ${kernel_naive_unroll(1, dst, src0, src1)}
                src0 += 1;
                src1 += 1;
                dst += 1;
            }
        }else if(nr_elem_in0 == 1 || nr_elem_in1 == 1){
            if(nr_elem_in0 > nr_elem_in1){
                const ${dtype_specifier} * src0 = inputs[0]->ptr;
                const ${dtype_specifier} * src1 = inputs[1]->ptr;
                ${dtype_specifier} * dst = outputs[0]->ptr;
                ${simd_dtype_specifier} vsrc1_0 = ${broad_cast}(src1[0]);
                size_t index = 0;
                for(; index + 2*SIMD_WIDTH-1 < nr_elem; index += 2*SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                    ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0 + SIMD_WIDTH);
                    ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_0)}
                    src0 += 2*SIMD_WIDTH;
                    dst += 2*SIMD_WIDTH;
                }
                for(; index + SIMD_WIDTH-1 < nr_elem; index += SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0);
                    ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                    src0 += SIMD_WIDTH;
                    dst += SIMD_WIDTH;
                }
                for(; index < nr_elem; index++) {
                    ${kernel_naive_unroll(1, dst, src0, src1)}
                    src0 += 1;
                    dst += 1;
                }
            }else{
                const ${dtype_specifier} * src0 = inputs[0]->ptr;
                const ${dtype_specifier} * src1 = inputs[1]->ptr;
                ${dtype_specifier} * dst = outputs[0]->ptr;
                ${simd_dtype_specifier} vsrc0_0 = ${broad_cast}(src0[0]);
                size_t index = 0;
                for(; index + 2*SIMD_WIDTH-1 < nr_elem; index += 2*SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                    ${simd_dtype_specifier} vsrc1_1 = ${load_vec}(src1 + SIMD_WIDTH);
                    ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_0, vsrc1_1)}
                    src1 += 2*SIMD_WIDTH;
                    dst += 2*SIMD_WIDTH;
                }
                for(; index + SIMD_WIDTH-1 < nr_elem; index += SIMD_WIDTH) {
                    ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1);
                    ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                    src1 += SIMD_WIDTH;
                    dst += SIMD_WIDTH;
                }
                for(; index < nr_elem; index++) {
                    ${kernel_naive_unroll(1, dst, src0, src1)}
                    src1 += 1;
                    dst += 1;
                }
            }
        }else if((nr_elem_in0 == src_layout_0.dims[src_layout_0.nr_dim - 1] || 
                nr_elem_in1 == src_layout_1.dims[src_layout_1.nr_dim - 1]) &&
                src_layout_0.dims[src_layout_0.nr_dim - 1] == src_layout_1.dims[src_layout_1.nr_dim - 1]){
            const ${dtype_specifier}* src0 = inputs[0]->ptr;
            const ${dtype_specifier}* src1 = inputs[1]->ptr;
            size_t channel = 1;
            for(size_t i = 0; i < dst_layout.nr_dim - 1; ++i)
                channel *= dst_layout.dims[i];
            size_t channel_stride = dst_layout.dims[dst_layout.nr_dim - 1];
            ${dtype_specifier} * dst = outputs[0]->ptr;
            for(size_t c=0; c<channel; c++){
                const ${dtype_specifier}* src0_ptr = src0 + c * channel_stride;
                const ${dtype_specifier}* src1_ptr = src1;
                if(nr_elem_in0 == src_layout_0.dims[src_layout_0.nr_dim - 1]){
                    src0_ptr = src0;
                    src1_ptr = src1 + c * channel_stride;
                }
                size_t index = 0;
                for(; index + 2*SIMD_WIDTH-1 < channel_stride; index += 2*SIMD_WIDTH){
                    ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0_ptr + index);
                    ${simd_dtype_specifier} vsrc0_1 = ${load_vec}(src0_ptr + index + SIMD_WIDTH);
                    ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1_ptr + index);
                    ${simd_dtype_specifier} vsrc1_1 = ${load_vec}(src1_ptr + index + SIMD_WIDTH);
                    ${kernel_simd_unroll(2, dst, vsrc0_0, vsrc1_0, vsrc0_1, vsrc1_1)}
                    dst += 2*SIMD_WIDTH;
                }
                for(; index + SIMD_WIDTH-1 < channel_stride; index += SIMD_WIDTH){
                    ${simd_dtype_specifier} vsrc0_0 = ${load_vec}(src0_ptr + index);
                    ${simd_dtype_specifier} vsrc1_0 = ${load_vec}(src1_ptr + index);
                    ${kernel_simd_unroll(1, dst, vsrc0_0, vsrc1_0)}
                    dst += SIMD_WIDTH;
                }
                for(; index < channel_stride; index++) {
                    ${kernel_naive_unroll(1, dst, src0_ptr + index, src1_ptr + index)}
                    ++dst;
                }
            }
        }else{
            broadcast_layout(&src_layout_0, dst_layout);
            broadcast_layout(&src_layout_1, dst_layout);
            NoconIter src0_iter = init_iter(src_layout_0);
            NoconIter src1_iter = init_iter(src_layout_1);
            const ${dtype_specifier} * src0 = inputs[0]->ptr;
            const ${dtype_specifier} * src1 = inputs[1]->ptr;
            ${dtype_specifier} * dst = outputs[0]->ptr;
            
            for(size_t index = 0; index < nr_elem; index++) {
                ${kernel_naive_unroll(1, dst, src0 + src0_iter.offset, src1 + src1_iter.offset)}
                inc_iter(src_layout_0, &src0_iter);
                inc_iter(src_layout_1, &src1_iter);
                dst++;
            }
        }
        
    )";
    return body;
}

}  // namespace

std::string ElemwiseGenBinaryAdd::GenKernelSimdInit(std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinaryAdd::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                   << ", GiAddFloat32(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                   << ", GiAddFloat16(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryAdd::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
               << "] + (" << strs[str_id + 1] << ")[" << i << "];";
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinarySub::GenKernelSimdInit(std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinarySub::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (!m_should_reverse) {
            if (m_comp_type == Utils::DtypeEnum::float32) {
                writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                       << ", GiSubtractFloat32(" << strs[str_id] << ","
                       << strs[str_id + 1] << "));";
            } else if (m_comp_type == Utils::DtypeEnum::float16) {
                writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                       << ", GiSubtractFloat16(" << strs[str_id] << ","
                       << strs[str_id + 1] << "));";
            }

        } else {
            if (m_comp_type == Utils::DtypeEnum::float32) {
                writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                       << ", GiSubtractFloat32(" << strs[str_id + 1] << ","
                       << strs[str_id] << "));";
            } else if (m_comp_type == Utils::DtypeEnum::float16) {
                writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                       << ", GiSubtractFloat16(" << strs[str_id + 1] << ","
                       << strs[str_id] << "));";
            }
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinarySub::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (!m_should_reverse) {
            writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
                   << "] - (" << strs[str_id + 1] << ")[" << i << "];";
        } else {
            writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id + 1] << ")["
                   << i << "] - (" << strs[str_id] << ")[" << i << "];";
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMul::GenKernelSimdInit(std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinaryMul::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                   << ", GiMultiplyFloat32(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                   << ", GiMultiplyFloat16(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        }

        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMul::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
               << "] * (" << strs[str_id + 1] << ")[" << i << "];";
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryTrueDiv::GenKernelSimdInit(
        std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinaryTrueDiv::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (!m_should_reverse) {
            if (m_comp_type == Utils::DtypeEnum::float32) {
                writer << "\n GiStoreFloat32((" << dst << "+4*" << i
                       << "), GiDivideFloat32(" << strs[str_id] << " , "
                       << strs[str_id + 1] << "));";
            } else if (m_comp_type == Utils::DtypeEnum::float16) {
                writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                       << ", GiDivideFloat16(" << strs[str_id] << ","
                       << strs[str_id + 1] << "));";
            }

        } else {
            if (m_comp_type == Utils::DtypeEnum::float32) {
                writer << "\n GiStoreFloat32((" << dst << "+4*" << i
                       << "), GiDivideFloat32(" << strs[str_id + 1] << " , "
                       << strs[str_id] << "));";
            } else if (m_comp_type == Utils::DtypeEnum::float16) {
                writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                       << ", GiDivideFloat16(" << strs[str_id + 1] << ","
                       << strs[str_id] << "));";
            }
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryTrueDiv::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (!m_should_reverse) {
            writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
                   << "] / (" << strs[str_id + 1] << ")[" << i << "];";
        } else {
            writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id + 1] << ")["
                   << i << "] / (" << strs[str_id] << ")[" << i << "];";
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryFuseAddRelu::GenKernelSimdInit(
        std::vector<std::string>) const {
    std::stringstream writer;
    if (m_comp_type == Utils::DtypeEnum::float32) {
        writer << "\nGI_FLOAT32_t vzero = GiBroadcastFloat32(0.f);";
    } else if (m_comp_type == Utils::DtypeEnum::float16) {
        writer << "\nGI_FLOAT16_t vzero = GiBroadcastFloat16(0.0);";
    }

    return writer.str();
}

std::string ElemwiseGenBinaryFuseAddRelu::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n GI_FLOAT32_t tmp" << i << " = GiAddFloat32(" << strs[str_id]
                   << "," << strs[str_id + 1] << ");";
            writer << "\n GiStoreFloat32((" << dst << " +4*" << i
                   << "), GiMaximumFloat32(tmp" << i << ", vzero));";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n GI_FLOAT16_t tmp" << i << " = GiAddFloat16(" << strs[str_id]
                   << "," << strs[str_id + 1] << ");";
            writer << "\n GiStoreFloat16((" << dst << " +8*" << i
                   << "), GiMaximumFloat16(tmp" << i << ", vzero));";
        }

        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryFuseAddRelu::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n"
                   << "float tmp = (" << strs[str_id] << ")[" << i << "] + ("
                   << strs[str_id + 1] << ")[" << i << "];";
            writer << "\n"
                   << dst << "[" << i << "] = "
                   << " tmp > 0 ? tmp : 0.0f;";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n"
                   << "gi_float16_t tmp = (" << strs[str_id] << ")[" << i << "] + ("
                   << strs[str_id + 1] << ")[" << i << "];";
            writer << "\n"
                   << dst << "[" << i << "] = "
                   << " tmp > 0 ? tmp : 0.0;";
        }

        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMax::GenKernelSimdInit(std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinaryMax::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                   << ", GiMaximumFloat32(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                   << ", GiMaximumFloat16(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        }

        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMax::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
               << "] > (" << strs[str_id + 1] << ")[" << i << "] ?(" << strs[str_id]
               << ")[" << i << "]:(" << strs[str_id + 1] << ")[" << i << "] ;";
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMin::GenKernelSimdInit(std::vector<std::string>) const {
    return "";
}

std::string ElemwiseGenBinaryMin::GenKernelSimdUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        if (m_comp_type == Utils::DtypeEnum::float32) {
            writer << "\n GiStoreFloat32((" << dst << ") + 4 * " << i
                   << ", GiMinimumFloat32(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        } else if (m_comp_type == Utils::DtypeEnum::float16) {
            writer << "\n GiStoreFloat16((" << dst << ") + 8 * " << i
                   << ", GiMinimumFloat16(" << strs[str_id] << "," << strs[str_id + 1]
                   << "));";
        }
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinaryMin::GenKernelNaiveUnroll(
        std::vector<std::string> strs) const {
    int unroll = std::stoi(strs[0]);
    auto dst = strs[1];
    std::stringstream writer;
    int str_id = 2;
    for (int i = 0; i < unroll; i++) {
        writer << "\n(" << dst << ")[" << i << "] = (" << strs[str_id] << ")[" << i
               << "] < (" << strs[str_id + 1] << ")[" << i << "] ?(" << strs[str_id]
               << ")[" << i << "]:(" << strs[str_id + 1] << ")[" << i << "] ;";
        str_id += 2;
    }
    return writer.str();
}

std::string ElemwiseGenBinary::GenCodeBody(std::vector<std::string> strs) const {
    auto input0 = strs[0];
    auto input1 = strs[1];
    auto output = strs[2];
    std::string body;
    switch (m_bcast_type) {
        case VEC_VEC:
            body = BinaryCode<VEC_VEC>();
            break;
        case VEC_BCAST101:
        case BCAST101_VEC:
            body = BinaryCode<VEC_BCAST101>();
            break;
        case VEC_BCAST101xX:
        case BCAST101xX_VEC:
            body = BinaryCode<VEC_BCAST101xX>();
            break;
        case VEC_SCALAR:
        case SCALAR_VEC:
            body = BinaryCode<VEC_SCALAR>();
            break;
        case BCAST111C_VEC:
        case VEC_BCAST111C:
            body = BinaryCode<VEC_BCAST111C>();
            break;
        case VEC_BV:
        case BV_VEC:
            body = BinaryCode<VEC_BV>();
            break;
        case NAIVE:
            body = BinaryCode<NAIVE>();
            break;
        case DYNAMIC_TYPE:
            body = BinaryCode<DYNAMIC_TYPE>();
            break;
        case VEC_BCAST110:
        case BCAST110_VEC:
            body = BinaryCode<VEC_BCAST110>();
            break;
        default:
            CC_ABORT << "unsupported broadcast type in elemwise\n";
    }

    auto kernel_init = [this](std::vector<std::string> strs) {
        return GenKernelSimdInit(strs);
    };
    auto kernel_simd_unroll = [this](std::vector<std::string> strs) {
        return GenKernelSimdUnroll(strs);
    };
    auto kernel_naive_unroll = [this](std::vector<std::string> strs) {
        return GenKernelNaiveUnroll(strs);
    };
    if (m_should_reverse) {
        input0 = strs[1];
        input1 = strs[0];
    }
    int reverse_flag = m_should_reverse ? 1 : 0;
    std::stringstream ss;
    auto dtype = Utils::cvt_dtype_specifier(m_comp_type);
    auto simd_width = Utils::get_dtype_simd_length(m_comp_type);
    auto simd_dtype = Utils::get_dtype_gi_simd_type(m_comp_type);
    std::string gi_type_str = Utils::get_dtype_gi_type_str(m_comp_type);
    auto simd_load = "GiLoad" + gi_type_str;
    auto simd_broad = "GiBroadcast" + gi_type_str;

    ss << StringTemplate::StringTemplateArgs()
                    .add("source0", input0)
                    .add("source1", input1)
                    .add("reverse", reverse_flag)
                    .add("dst", output)
                    .add("kernel_init", kernel_init)
                    .add("kernel_simd_unroll", kernel_simd_unroll)
                    .add("kernel_naive_unroll", kernel_naive_unroll)
                    .add("dtype_specifier", dtype)
                    .add("simd_dtype_specifier", simd_dtype)
                    .add("simd_width", (uint32_t)simd_width)
                    .add("load_vec", simd_load)
                    .add("broad_cast", simd_broad)

                    .render(body);

    return ss.str();
}

BcastType ElemwiseGenBinary::GetBcastType(
        const CCOperand& operand0, const CCOperand& operand1) {
    return GetBinaryBcastType(operand0, operand1);
}

bool ElemwiseGenBinary::WhetherShouldReverse(
        const CCOperand& operand0, const CCOperand& operand1) {
    auto shape0 = operand0.shape;
    auto shape1 = operand1.shape;
    size_t nr_elem0 = 1;
    size_t nr_elem1 = 1;
    for (size_t i = 0; i < shape0.size(); i++) {
        nr_elem0 *= shape0[i];
    }
    for (size_t i = 0; i < shape1.size(); i++) {
        nr_elem1 *= shape1[i];
    }
    if (Utils::is_shape_dynamic(shape0) || Utils::is_shape_dynamic(shape1)) {
        return false;
    }
    if (nr_elem0 < nr_elem1) {
        return true;
    } else {
        return false;
    }
}

// vim: syntax=cpp.doxygen
