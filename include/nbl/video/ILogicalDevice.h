#ifndef __NBL_I_GPU_LOGICAL_DEVICE_H_INCLUDED__
#define __NBL_I_GPU_LOGICAL_DEVICE_H_INCLUDED__

#include "nbl/core/IReferenceCounted.h"
#include "nbl/video/IGPUQueue.h"
#include "nbl/video/IGPUSemaphore.h"
#include "nbl/video/IDescriptorPool.h"
#include "nbl/video/IGPUDescriptorSet.h"
#include "nbl/video/IGPUCommandPool.h"
#include "nbl/video/IGPUFramebuffer.h"
#include "nbl/video/IGPUGraphicsPipeline.h"
#include "nbl/video/ISwapchain.h"
#include "nbl/asset/ICPUShader.h"
#include "nbl/asset/utils/ISPIRVOptimizer.h"
#include "nbl/video/IGPUShader.h"
#include "nbl/video/IGPUPipelineCache.h"
#include "nbl/video/EApiType.h"

namespace nbl {
namespace video
{

class ILogicalDevice : public core::IReferenceCounted
{
public:
    struct SQueueCreationParams
    {
        IGPUQueue::E_CREATE_FLAGS flags;
        uint32_t familyIndex;
        uint32_t count;
        const float* priorities;
    };
    struct SCreationParams
    {
        uint32_t queueParamsCount;
        const SQueueCreationParams* queueCreateInfos;
        // ???:
        //uint32_t enabledExtensionCount;
        //const char* const* ppEnabledExtensionNames;
        //const VkPhysicalDeviceFeatures* pEnabledFeatures;
    };

    struct SDescriptorSetCreationParams
    {
        IDescriptorPool* descriptorPool;
        uint32_t descriptorSetCount;
        IGPUDescriptorSetLayout** pSetLayouts;
    };

    struct SBindBufferMemoryInfo
    {
        IGPUBuffer* buffer;
        IDriverMemoryAllocation* memory;
        size_t offset;
    };
    struct SBindImageMemoryInfo
    {
        IGPUImage* image;
        IDriverMemoryAllocation* memory;
        size_t offset;
    };

    ILogicalDevice(E_API_TYPE api_type, const SCreationParams& params, core::smart_refctd_ptr<io::IFileSystem>&& fs, core::smart_refctd_ptr<asset::IGLSLCompiler>&& glslc) : m_apiType(api_type), m_fs(std::move(fs)), m_GLSLCompiler(std::move(glslc))
    {
        uint32_t qcnt = 0u;
        uint32_t greatestFamNum = 0u;
        for (uint32_t i = 0u; i < params.queueParamsCount; ++i)
        {
            greatestFamNum = std::max(greatestFamNum, params.queueCreateInfos[i].familyIndex);
            qcnt += params.queueCreateInfos[i].count;
        }

        m_queues = core::make_refctd_dynamic_array<queues_array_t>(qcnt);
        m_offsets = core::make_refctd_dynamic_array<q_offsets_array_t>(greatestFamNum + 1u, 0u);

        for (const auto& qci : core::SRange<const SQueueCreationParams>(params.queueCreateInfos, params.queueCreateInfos + params.queueParamsCount))
        {
            if (qci.familyIndex == greatestFamNum)
                continue;

            (*m_offsets)[qci.familyIndex + 1u] = qci.count;
        }
        // compute prefix sum
        for (uint32_t i = 1u; i < m_offsets->size(); ++i)
        {
            (*m_offsets)[i] += (*m_offsets)[i - 1u];
        }
    }

    E_API_TYPE getAPIType() const { return m_apiType; }

    IGPUQueue* getQueue(uint32_t _familyIx, uint32_t _ix)
    {
        const uint32_t offset = (*m_offsets)[_familyIx];

        return (*m_queues)[offset+_ix].get();
    }

    virtual core::smart_refctd_ptr<IGPUSemaphore> createSemaphore() = 0;

    virtual core::smart_refctd_ptr<IGPUEvent> createEvent() = 0;
    virtual IGPUEvent::E_STATUS getEventStatus(const IGPUEvent* _event) = 0;
    virtual IGPUEvent::E_STATUS resetEvent(IGPUEvent* _event) = 0;
    virtual IGPUEvent::E_STATUS setEvent(IGPUEvent* _event) = 0;

    virtual core::smart_refctd_ptr<IGPUFence> createFence(IGPUFence::E_CREATE_FLAGS _flags) = 0;
    virtual IGPUFence::E_STATUS getFenceStatus(IGPUFence* _fence) = 0;
    virtual void resetFences(uint32_t _count, IGPUFence** _fences) = 0;
    virtual IGPUFence::E_STATUS waitForFences(uint32_t _count, IGPUFence** _fences, bool _waitAll, uint64_t _timeout) = 0;

    bool createCommandBuffers(IGPUCommandPool* _cmdPool, IGPUCommandBuffer::E_LEVEL _level, uint32_t _count, core::smart_refctd_ptr<IGPUCommandBuffer>* _outCmdBufs)
    {
        if (!_cmdPool->wasCreatedBy(this))
            return false;
        return createCommandBuffers_impl(_cmdPool, _level, _count, _outCmdBufs);
    }
    bool freeCommandBuffers(IGPUCommandBuffer** _cmdbufs, uint32_t _count)
    {
        for (uint32_t i = 0u; i < _count; ++i)
            if (!_cmdbufs[i]->wasCreatedBy(this))
                return false;
        return freeCommandBuffers_impl(_cmdbufs, _count);
    }

    virtual core::smart_refctd_ptr<IGPUCommandPool> createCommandPool(uint32_t _familyIx, IGPUCommandPool::E_CREATE_FLAGS flags) = 0;
    virtual core::smart_refctd_ptr<IDescriptorPool> createDescriptorPool(IDescriptorPool::E_CREATE_FLAGS flags, uint32_t maxSets, uint32_t poolSizeCount, const IDescriptorPool::SDescriptorPoolSize* poolSizes) = 0;

    core::smart_refctd_ptr<IGPUFramebuffer> createGPUFramebuffer(IGPUFramebuffer::SCreationParams&& params)
    {
        if (!params.renderpass->wasCreatedBy(this))
            return nullptr;
        if (!IGPUFramebuffer::validate(params))
            return nullptr;
        return createGPUFramebuffer_impl(std::move(params));
    }

    virtual core::smart_refctd_ptr<IGPURenderpass> createGPURenderpass(const IGPURenderpass::SCreationParams& params) = 0;

    virtual void regenerateMipLevels(IGPUImageView* imageview) = 0;


    static inline IDriverMemoryBacked::SDriverMemoryRequirements getDeviceLocalGPUMemoryReqs()
    {
        IDriverMemoryBacked::SDriverMemoryRequirements reqs;
        reqs.vulkanReqs.alignment = 0;
        reqs.vulkanReqs.memoryTypeBits = 0xffffffffu;
        reqs.memoryHeapLocation = IDriverMemoryAllocation::ESMT_DEVICE_LOCAL;
        reqs.mappingCapability = IDriverMemoryAllocation::EMCF_CANNOT_MAP;
        reqs.prefersDedicatedAllocation = true;
        reqs.requiresDedicatedAllocation = true;
        return reqs;
    }
    static inline IDriverMemoryBacked::SDriverMemoryRequirements getSpilloverGPUMemoryReqs()
    {
        IDriverMemoryBacked::SDriverMemoryRequirements reqs;
        reqs.vulkanReqs.alignment = 0;
        reqs.vulkanReqs.memoryTypeBits = 0xffffffffu;
        reqs.memoryHeapLocation = IDriverMemoryAllocation::ESMT_NOT_DEVICE_LOCAL;
        reqs.mappingCapability = IDriverMemoryAllocation::EMCF_CANNOT_MAP;
        reqs.prefersDedicatedAllocation = true;
        reqs.requiresDedicatedAllocation = true;
        return reqs;
    }
    static inline IDriverMemoryBacked::SDriverMemoryRequirements getUpStreamingMemoryReqs()
    {
        IDriverMemoryBacked::SDriverMemoryRequirements reqs;
        reqs.vulkanReqs.alignment = 0;
        reqs.vulkanReqs.memoryTypeBits = 0xffffffffu;
        reqs.memoryHeapLocation = IDriverMemoryAllocation::ESMT_DEVICE_LOCAL;
        reqs.mappingCapability = IDriverMemoryAllocation::EMCF_CAN_MAP_FOR_WRITE;
        reqs.prefersDedicatedAllocation = true;
        reqs.requiresDedicatedAllocation = true;
        return reqs;
    }
    static inline IDriverMemoryBacked::SDriverMemoryRequirements getDownStreamingMemoryReqs()
    {
        IDriverMemoryBacked::SDriverMemoryRequirements reqs;
        reqs.vulkanReqs.alignment = 0;
        reqs.vulkanReqs.memoryTypeBits = 0xffffffffu;
        reqs.memoryHeapLocation = IDriverMemoryAllocation::ESMT_NOT_DEVICE_LOCAL;
        reqs.mappingCapability = IDriverMemoryAllocation::EMCF_CAN_MAP_FOR_READ | IDriverMemoryAllocation::EMCF_CACHED;
        reqs.prefersDedicatedAllocation = true;
        reqs.requiresDedicatedAllocation = true;
        return reqs;
    }
    static inline IDriverMemoryBacked::SDriverMemoryRequirements getCPUSideGPUVisibleGPUMemoryReqs()
    {
        IDriverMemoryBacked::SDriverMemoryRequirements reqs;
        reqs.vulkanReqs.alignment = 0;
        reqs.vulkanReqs.memoryTypeBits = 0xffffffffu;
        reqs.memoryHeapLocation = IDriverMemoryAllocation::ESMT_NOT_DEVICE_LOCAL;
        reqs.mappingCapability = IDriverMemoryAllocation::EMCF_CAN_MAP_FOR_READ | IDriverMemoryAllocation::EMCF_CAN_MAP_FOR_WRITE | IDriverMemoryAllocation::EMCF_COHERENT | IDriverMemoryAllocation::EMCF_CACHED;
        reqs.prefersDedicatedAllocation = true;
        reqs.requiresDedicatedAllocation = true;
        return reqs;
    }

    //! Best for Mesh data, UBOs, SSBOs, etc.
    virtual core::smart_refctd_ptr<IDriverMemoryAllocation> allocateDeviceLocalMemory(const IDriverMemoryBacked::SDriverMemoryRequirements& additionalReqs) { return nullptr; }

    //! If cannot or don't want to use device local memory, then this memory can be used
    /** If the above fails (only possible on vulkan) or we have perfomance hitches due to video memory oversubscription.*/
    virtual core::smart_refctd_ptr<IDriverMemoryAllocation> allocateSpilloverMemory(const IDriverMemoryBacked::SDriverMemoryRequirements& additionalReqs) { return nullptr; }

    //! Best for staging uploads to the GPU, such as resource streaming, and data to update the above memory with
    virtual core::smart_refctd_ptr<IDriverMemoryAllocation> allocateUpStreamingMemory(const IDriverMemoryBacked::SDriverMemoryRequirements& additionalReqs) { return nullptr; }

    //! Best for staging downloads from the GPU, such as query results, Z-Buffer, video frames for recording, etc.
    virtual core::smart_refctd_ptr<IDriverMemoryAllocation> allocateDownStreamingMemory(const IDriverMemoryBacked::SDriverMemoryRequirements& additionalReqs) { return nullptr; }

    //! Should be just as fast to play around with on the CPU as regular malloc'ed memory, but slowest to access with GPU
    virtual core::smart_refctd_ptr<IDriverMemoryAllocation> allocateCPUSideGPUVisibleMemory(const IDriverMemoryBacked::SDriverMemoryRequirements& additionalReqs) { return nullptr; }


    //! For memory allocations without the video::IDriverMemoryAllocation::EMCF_COHERENT mapping capability flag you need to call this for the CPU writes to become GPU visible
    void flushMappedMemoryRanges(uint32_t memoryRangeCount, const video::IDriverMemoryAllocation::MappedMemoryRange* pMemoryRanges)
    {
        core::SRange<const video::IDriverMemoryAllocation::MappedMemoryRange> ranges{ pMemoryRanges, pMemoryRanges + memoryRangeCount };
        return flushMappedMemoryRanges(ranges);
    }

    //! Utility wrapper for the pointer based func
    virtual void flushMappedMemoryRanges(core::SRange<const video::IDriverMemoryAllocation::MappedMemoryRange> ranges) = 0;

    //! For memory allocations without the video::IDriverMemoryAllocation::EMCF_COHERENT mapping capability flag you need to call this for the GPU writes to become CPU visible (slow on OpenGL)
    void invalidateMappedMemoryRanges(uint32_t memoryRangeCount, const video::IDriverMemoryAllocation::MappedMemoryRange* pMemoryRanges)
    {
        core::SRange<const video::IDriverMemoryAllocation::MappedMemoryRange> ranges{ pMemoryRanges, pMemoryRanges + memoryRangeCount };
        return invalidateMappedMemoryRanges(ranges);
    }

    //! Utility wrapper for the pointer based func
    virtual void invalidateMappedMemoryRanges(core::SRange<const video::IDriverMemoryAllocation::MappedMemoryRange> ranges) = 0;

    virtual core::smart_refctd_ptr<IGPUBuffer> createGPUBuffer(const IDriverMemoryBacked::SDriverMemoryRequirements& initialMreqs, const bool canModifySubData = false) { return nullptr; }

    //! Binds memory allocation to provide the backing for the resource.
    /** Available only on Vulkan, in OpenGL all resources create their own memory implicitly,
    so pooling or aliasing memory for different resources is not possible.
    There is no unbind, so once memory is bound it remains bound until you destroy the resource object.
    Actually all resource classes in OpenGL implement both IDriverMemoryBacked and IDriverMemoryAllocation,
    so effectively the memory is pre-bound at the time of creation.
    \return true on success, always false under OpenGL.*/
    virtual bool bindBufferMemory(uint32_t bindInfoCount, const SBindBufferMemoryInfo* pBindInfos) { return false; }

    //! Creates the buffer, allocates memory dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUBuffer> createDeviceLocalGPUBufferOnDedMem(size_t size)
    {
        auto reqs = getDeviceLocalGPUMemoryReqs();
        reqs.vulkanReqs.size = size;
        return this->createGPUBufferOnDedMem(reqs, false);
    }

    //! Creates the buffer, allocates memory dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUBuffer> createSpilloverGPUBufferOnDedMem(size_t size)
    {
        auto reqs = getSpilloverGPUMemoryReqs();
        reqs.vulkanReqs.size = size;
        return this->createGPUBufferOnDedMem(reqs, false);
    }

    //! Creates the buffer, allocates memory dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUBuffer> createUpStreamingGPUBufferOnDedMem(size_t size)
    {
        auto reqs = getUpStreamingMemoryReqs();
        reqs.vulkanReqs.size = size;
        return this->createGPUBufferOnDedMem(reqs, false);
    }

    //! Creates the buffer, allocates memory dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUBuffer> createDownStreamingGPUBufferOnDedMem(size_t size)
    {
        auto reqs = getDownStreamingMemoryReqs();
        reqs.vulkanReqs.size = size;
        return this->createGPUBufferOnDedMem(reqs, false);
    }

    //! Creates the buffer, allocates memory dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUBuffer> createCPUSideGPUVisibleGPUBufferOnDedMem(size_t size)
    {
        auto reqs = getCPUSideGPUVisibleGPUMemoryReqs();
        reqs.vulkanReqs.size = size;
        return this->createGPUBufferOnDedMem(reqs, false);
    }

    //! Low level function used to implement the above, use with caution
    virtual core::smart_refctd_ptr<IGPUBuffer> createGPUBufferOnDedMem(const IDriverMemoryBacked::SDriverMemoryRequirements& initialMreqs, const bool canModifySubData = false) { return nullptr; }

    virtual core::smart_refctd_ptr<IGPUShader> createGPUShader(core::smart_refctd_ptr<asset::ICPUShader>&& cpushader) = 0;

    core::smart_refctd_ptr<IGPUSpecializedShader> createGPUSpecializedShader(const IGPUShader* _unspecialized, const asset::ISpecializedShader::SInfo& _specInfo, const asset::ISPIRVOptimizer* _spvopt = nullptr)
    {
        if (!_unspecialized->wasCreatedBy(this))
            return nullptr;
        return createGPUSpecializedShader_impl(_unspecialized, _specInfo, _spvopt);
    }

    //! Create a BufferView, to a shader; a fake 1D texture with no interpolation (@see ICPUBufferView)
    core::smart_refctd_ptr<IGPUBufferView> createGPUBufferView(IGPUBuffer* _underlying, asset::E_FORMAT _fmt, size_t _offset = 0ull, size_t _size = IGPUBufferView::whole_buffer)
    {
        if (!_underlying->wasCreatedBy(this))
            return nullptr;
        return createGPUBufferView_impl(_underlying, _fmt, _offset, _size);
    }


    //! Creates an Image (@see ICPUImage)
    virtual core::smart_refctd_ptr<IGPUImage> createGPUImage(asset::IImage::SCreationParams&& params) { return nullptr; }

    //! The counterpart of @see bindBufferMemory for images
    virtual bool bindImageMemory(uint32_t bindInfoCount, const SBindImageMemoryInfo* pBindInfos) { return false; }

    //! Creates the Image, allocates dedicated memory and binds it at once.
    inline core::smart_refctd_ptr<IGPUImage> createDeviceLocalGPUImageOnDedMem(IGPUImage::SCreationParams&& params)
    {
        auto reqs = getDeviceLocalGPUMemoryReqs();
        return this->createGPUImageOnDedMem(std::move(params), reqs);
    }

    //!
    virtual core::smart_refctd_ptr<IGPUImage> createGPUImageOnDedMem(IGPUImage::SCreationParams&& params, const IDriverMemoryBacked::SDriverMemoryRequirements& initialMreqs) = 0;

    //!
    inline core::smart_refctd_ptr<IGPUImage> createFilledDeviceLocalGPUImageOnDedMem(IGPUImage::SCreationParams&& params, IGPUBuffer* srcBuffer, uint32_t regionCount, const IGPUImage::SBufferCopy* pRegions)
    {
        auto retval = createDeviceLocalGPUImageOnDedMem(std::move(params));
        // TODO, copyBufferToImage() is a command, so this whole function sholdnt be in ILogicalDevice probably
        //this->copyBufferToImage(srcBuffer, retval.get(), regionCount, pRegions);
        return retval;
    }
    inline core::smart_refctd_ptr<IGPUImage> createFilledDeviceLocalGPUImageOnDedMem(IGPUImage::SCreationParams&& params, IGPUImage* srcImage, uint32_t regionCount, const IGPUImage::SImageCopy* pRegions)
    {
        auto retval = createDeviceLocalGPUImageOnDedMem(std::move(params));
        // TODO, copyImage() is a command, so this whole function sholdnt be in ILogicalDevice probably
        //this->copyImage(srcImage, retval.get(), regionCount, pRegions);
        return retval;
    }


    //! Create an ImageView that can actually be used by shaders (@see ICPUImageView)
    core::smart_refctd_ptr<IGPUImageView> createGPUImageView(IGPUImageView::SCreationParams&& params)
    {
        if (!params.image->wasCreatedBy(this))
            return nullptr;
        return createGPUImageView_impl(std::move(params));
    }

    core::smart_refctd_ptr<IGPUDescriptorSet> createGPUDescriptorSet(IDescriptorPool* pool, core::smart_refctd_ptr<const IGPUDescriptorSetLayout>&& layout)
    {
        if (!pool->wasCreatedBy(this))
            return nullptr;
        if (!layout->wasCreatedBy(this))
            return nullptr;
        return createGPUDescriptorSet_impl(pool, std::move(layout));
    }

    void createGPUDescriptorSets(IDescriptorPool* pool, uint32_t count, const IGPUDescriptorSetLayout** _layouts, core::smart_refctd_ptr<IGPUDescriptorSet>* output)
    {
        core::SRange<const IGPUDescriptorSetLayout*> layouts{ _layouts, _layouts + count };
        createGPUDescriptorSets(pool, layouts, output);
    }
    void createGPUDescriptorSets(IDescriptorPool* pool, core::SRange<const IGPUDescriptorSetLayout*> layouts, core::smart_refctd_ptr<IGPUDescriptorSet>* output)
    {
        uint32_t i = 0u;
        for (const IGPUDescriptorSetLayout* layout_ : layouts)
        {
            auto layout = core::smart_refctd_ptr<const IGPUDescriptorSetLayout>(layout_);
            output[i++] = createGPUDescriptorSet(pool, std::move(layout));
        }
    }

    //! Fill out the descriptor sets with descriptors
    virtual void updateDescriptorSets(uint32_t descriptorWriteCount, const IGPUDescriptorSet::SWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const IGPUDescriptorSet::SCopyDescriptorSet* pDescriptorCopies) = 0;

    //! Create a sampler object to use with images
    virtual core::smart_refctd_ptr<IGPUSampler> createGPUSampler(const IGPUSampler::SParams& _params) = 0;

    //! Create a pipeline cache object
    virtual core::smart_refctd_ptr<IGPUPipelineCache> createGPUPipelineCache() { return nullptr; }

    //! Create a descriptor set layout (@see ICPUDescriptorSetLayout)
    core::smart_refctd_ptr<IGPUDescriptorSetLayout> createGPUDescriptorSetLayout(const IGPUDescriptorSetLayout::SBinding* _begin, const IGPUDescriptorSetLayout::SBinding* _end)
    {
        for (auto b = _begin; b != _end; ++b)
        {
            if (b->type == asset::EDT_COMBINED_IMAGE_SAMPLER && b->samplers)
            {
                auto* samplers = b->samplers;
                for (uint32_t i = 0u; i < b->count; ++i)
                    if (!samplers[i]->wasCreatedBy(this))
                        return nullptr;
            }
        }
        return createGPUDescriptorSetLayout_impl(_begin, _end);
    }

    //! Create a pipeline layout (@see ICPUPipelineLayout)
    core::smart_refctd_ptr<IGPUPipelineLayout> createGPUPipelineLayout(
        const asset::SPushConstantRange* const _pcRangesBegin = nullptr, const asset::SPushConstantRange* const _pcRangesEnd = nullptr,
        core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout0 = nullptr, core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout1 = nullptr,
        core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout2 = nullptr, core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout3 = nullptr
    )
    {
        if (_layout0 && !_layout0->wasCreatedBy(this))
            return nullptr;
        if (_layout1 && !_layout1->wasCreatedBy(this))
            return nullptr;
        if (_layout2 && !_layout2->wasCreatedBy(this))
            return nullptr;
        if (_layout3 && !_layout3->wasCreatedBy(this))
            return nullptr;
        return createGPUPipelineLayout_impl(_pcRangesBegin, _pcRangesEnd, std::move(_layout0), std::move(_layout1), std::move(_layout2), std::move(_layout3));
    }

    core::smart_refctd_ptr<IGPUComputePipeline> createGPUComputePipeline(
        IGPUPipelineCache* _pipelineCache,
        core::smart_refctd_ptr<IGPUPipelineLayout>&& _layout,
        core::smart_refctd_ptr<IGPUSpecializedShader>&& _shader
    ) {
        if (_pipelineCache && !_pipelineCache->wasCreatedBy(this))
            return nullptr;
        if (!_layout->wasCreatedBy(this))
            return nullptr;
        if (!_shader->wasCreatedBy(this))
            return nullptr;
        return createGPUComputePipeline_impl(_pipelineCache, std::move(_layout), std::move(_shader));
    }

    bool createGPUComputePipelines(
        IGPUPipelineCache* pipelineCache,
        core::SRange<const IGPUComputePipeline::SCreationParams> createInfos,
        core::smart_refctd_ptr<IGPUComputePipeline>* output
    ) {
        if (pipelineCache && !pipelineCache->wasCreatedBy(this))
            return false;
        for (const auto& ci : createInfos)
            if (!ci.layout->wasCreatedBy(this) || !ci.shader->wasCreatedBy(this))
                return false;
        return createGPUComputePipelines_impl(pipelineCache, createInfos, output);
    }

    bool createGPUComputePipelines(
        IGPUPipelineCache* pipelineCache,
        uint32_t count,
        const IGPUComputePipeline::SCreationParams* createInfos,
        core::smart_refctd_ptr<IGPUComputePipeline>* output
    ) 
    {
        auto ci = core::SRange<const IGPUComputePipeline::SCreationParams>{createInfos, createInfos+count};
        return createGPUComputePipelines(pipelineCache, ci, output);
    }

    core::smart_refctd_ptr<IGPURenderpassIndependentPipeline> createGPURenderpassIndependentPipeline(
        IGPUPipelineCache* _pipelineCache,
        core::smart_refctd_ptr<IGPUPipelineLayout>&& _layout,
        IGPUSpecializedShader** _shaders, IGPUSpecializedShader** _shadersEnd,
        const asset::SVertexInputParams& _vertexInputParams,
        const asset::SBlendParams& _blendParams,
        const asset::SPrimitiveAssemblyParams& _primAsmParams,
        const asset::SRasterizationParams& _rasterParams
    ) {
        if (_pipelineCache && !_pipelineCache->wasCreatedBy(this))
            return nullptr;
        if (!_layout->wasCreatedBy(this))
            return nullptr;
        for (auto s = _shaders; s != _shadersEnd; ++s)
        {
            if (!(*s)->wasCreatedBy(this))
                return nullptr;
        }
        return createGPURenderpassIndependentPipeline_impl(_pipelineCache, std::move(_layout), _shaders, _shadersEnd, _vertexInputParams, _blendParams, _primAsmParams, _rasterParams);
    }

    bool createGPURenderpassIndependentPipelines(
        IGPUPipelineCache* pipelineCache,
        core::SRange<const IGPURenderpassIndependentPipeline::SCreationParams> createInfos,
        core::smart_refctd_ptr<IGPURenderpassIndependentPipeline>* output
    ) {
        if (pipelineCache && !pipelineCache->wasCreatedBy(this))
            return false;
        for (const IGPURenderpassIndependentPipeline::SCreationParams& ci : createInfos)
        {
            if (!ci.layout->wasCreatedBy(this))
                return false;
            for (auto& s : ci.shaders)
                if (s && !s->wasCreatedBy(this))
                    return false;
        }
        return createGPURenderpassIndependentPipelines_impl(pipelineCache, createInfos, output);
    }

    bool createGPURenderpassIndependentPipelines(
        IGPUPipelineCache* pipelineCache,
        uint32_t count,
        const IGPURenderpassIndependentPipeline::SCreationParams* createInfos,
        core::smart_refctd_ptr<IGPURenderpassIndependentPipeline>* output
    )
    {
        auto ci = core::SRange<const IGPURenderpassIndependentPipeline::SCreationParams>{createInfos, createInfos+count};
        return createGPURenderpassIndependentPipelines(pipelineCache, ci, output);
    }

    core::smart_refctd_ptr<IGPUGraphicsPipeline> createGPUGraphicsPipeline(IGPUPipelineCache* pipelineCache, IGPUGraphicsPipeline::SCreationParams&& params)
    {
        if (pipelineCache && !pipelineCache->wasCreatedBy(this))
            return nullptr;
        if (!params.renderpass->wasCreatedBy(this))
            return nullptr;
        if (!params.renderpassIndependent->wasCreatedBy(this))
            return nullptr;
        if (!IGPUGraphicsPipeline::validate(params))
            return nullptr;
        return createGPUGraphicsPipeline_impl(pipelineCache, std::move(params));
    }

    bool createGPUGraphicsPipelines(IGPUPipelineCache* pipelineCache, core::SRange<const IGPUGraphicsPipeline::SCreationParams> params, core::smart_refctd_ptr<IGPUGraphicsPipeline>* output)
    {
        if (pipelineCache && !pipelineCache->wasCreatedBy(this))
            return false;
        for (const auto& ci : params)
        {
            if (!ci.renderpass->wasCreatedBy(this))
                return false;
            if (!ci.renderpassIndependent->wasCreatedBy(this))
                return false;
            if (!IGPUGraphicsPipeline::validate(ci))
                return false;
        }
        return createGPUGraphicsPipelines_impl(pipelineCache, params, output);
    }

    bool createGPUGraphicsPipelines(IGPUPipelineCache* pipelineCache, uint32_t count, const IGPUGraphicsPipeline::SCreationParams* params, core::smart_refctd_ptr<IGPUGraphicsPipeline>* output)
    {
        auto ci = core::SRange<const IGPUGraphicsPipeline::SCreationParams>{ params, params + count };
        return createGPUGraphicsPipelines(pipelineCache, ci, output);
    }

    virtual core::smart_refctd_ptr<ISwapchain> createSwapchain(ISwapchain::SCreationParams&& params) = 0;

    virtual void waitIdle() = 0;

    // Not implemented stuff:
    //vkCreateGraphicsPipelines //no graphics pipelines yet (just renderpass independent)
    //vkGetBufferMemoryRequirements
    //vkGetDescriptorSetLayoutSupport
    //vkMapMemory
    //vkUnmapMemory
    //vkTrimCommandPool
    //vkGetPipelineCacheData //as pipeline cache method??
    //vkMergePipelineCaches //as pipeline cache method
    //vkCreateQueryPool //????
    //vkCreateShaderModule //????

protected:
    virtual bool createCommandBuffers_impl(IGPUCommandPool* _cmdPool, IGPUCommandBuffer::E_LEVEL _level, uint32_t _count, core::smart_refctd_ptr<IGPUCommandBuffer>* _outCmdBufs) = 0;
    virtual bool freeCommandBuffers_impl(IGPUCommandBuffer** _cmdbufs, uint32_t _count) = 0;
    virtual core::smart_refctd_ptr<IGPUFramebuffer> createGPUFramebuffer_impl(IGPUFramebuffer::SCreationParams&& params) = 0;
    virtual core::smart_refctd_ptr<IGPUSpecializedShader> createGPUSpecializedShader_impl(const IGPUShader* _unspecialized, const asset::ISpecializedShader::SInfo& _specInfo, const asset::ISPIRVOptimizer* _spvopt) = 0;
    virtual core::smart_refctd_ptr<IGPUBufferView> createGPUBufferView_impl(IGPUBuffer* _underlying, asset::E_FORMAT _fmt, size_t _offset = 0ull, size_t _size = IGPUBufferView::whole_buffer) = 0;
    virtual core::smart_refctd_ptr<IGPUImageView> createGPUImageView_impl(IGPUImageView::SCreationParams&& params) = 0;
    virtual core::smart_refctd_ptr<IGPUDescriptorSet> createGPUDescriptorSet_impl(IDescriptorPool* pool, core::smart_refctd_ptr<const IGPUDescriptorSetLayout>&& layout) = 0;
    virtual core::smart_refctd_ptr<IGPUDescriptorSetLayout> createGPUDescriptorSetLayout_impl(const IGPUDescriptorSetLayout::SBinding* _begin, const IGPUDescriptorSetLayout::SBinding* _end) = 0;
    virtual core::smart_refctd_ptr<IGPUPipelineLayout> createGPUPipelineLayout_impl(
        const asset::SPushConstantRange* const _pcRangesBegin = nullptr, const asset::SPushConstantRange* const _pcRangesEnd = nullptr,
        core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout0 = nullptr, core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout1 = nullptr,
        core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout2 = nullptr, core::smart_refctd_ptr<IGPUDescriptorSetLayout>&& _layout3 = nullptr
    ) = 0;
    virtual core::smart_refctd_ptr<IGPUComputePipeline> createGPUComputePipeline_impl(
        IGPUPipelineCache* _pipelineCache,
        core::smart_refctd_ptr<IGPUPipelineLayout>&& _layout,
        core::smart_refctd_ptr<IGPUSpecializedShader>&& _shader
    ) = 0;
    virtual bool createGPUComputePipelines_impl(
        IGPUPipelineCache* pipelineCache,
        core::SRange<const IGPUComputePipeline::SCreationParams> createInfos,
        core::smart_refctd_ptr<IGPUComputePipeline>* output
    ) = 0;
    virtual core::smart_refctd_ptr<IGPURenderpassIndependentPipeline> createGPURenderpassIndependentPipeline_impl(
        IGPUPipelineCache* _pipelineCache,
        core::smart_refctd_ptr<IGPUPipelineLayout>&& _layout,
        IGPUSpecializedShader** _shaders, IGPUSpecializedShader** _shadersEnd,
        const asset::SVertexInputParams& _vertexInputParams,
        const asset::SBlendParams& _blendParams,
        const asset::SPrimitiveAssemblyParams& _primAsmParams,
        const asset::SRasterizationParams& _rasterParams
    ) = 0;
    virtual bool createGPURenderpassIndependentPipelines_impl(
        IGPUPipelineCache* pipelineCache,
        core::SRange<const IGPURenderpassIndependentPipeline::SCreationParams> createInfos,
        core::smart_refctd_ptr<IGPURenderpassIndependentPipeline>* output
    ) = 0;
    virtual core::smart_refctd_ptr<IGPUGraphicsPipeline> createGPUGraphicsPipeline_impl(IGPUPipelineCache* pipelineCache, IGPUGraphicsPipeline::SCreationParams&& params) = 0;
    virtual bool createGPUGraphicsPipelines_impl(IGPUPipelineCache* pipelineCache, core::SRange<const IGPUGraphicsPipeline::SCreationParams> params, core::smart_refctd_ptr<IGPUGraphicsPipeline>* output) = 0;

    core::smart_refctd_ptr<io::IFileSystem> m_fs;
    core::smart_refctd_ptr<asset::IGLSLCompiler> m_GLSLCompiler;

    using queues_array_t = core::smart_refctd_dynamic_array<core::smart_refctd_ptr<IGPUQueue>>;
    queues_array_t m_queues;
    using q_offsets_array_t = core::smart_refctd_dynamic_array<uint32_t>;
    q_offsets_array_t m_offsets;

private:
    const E_API_TYPE m_apiType;
};

}
}


#endif