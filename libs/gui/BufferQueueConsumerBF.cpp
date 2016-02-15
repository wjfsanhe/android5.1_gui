/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>

#define LOG_TAG "BufferQueueConsumerBF"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#include <gui/BufferItem.h>
#include <gui/BufferQueueConsumerBF.h>
#include <gui/BufferQueueCoreBF.h>
#include <gui/IConsumerListener.h>
#include <gui/IProducerListener.h>
#include <cutils/properties.h>

namespace android {

BufferQueueConsumerBF::BufferQueueConsumerBF(const sp<BufferQueueCoreBF>& core) :
    mCore(core),
    mSlots(core->mSlots),
    mConsumerName() {}

BufferQueueConsumerBF::~BufferQueueConsumerBF() {}

status_t BufferQueueConsumerBF::acquireBuffer(BufferItem* outBuffer,
        nsecs_t expectedPresent) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCore->mMutex);



    int slot = 0;
    *outBuffer = mCore->mQueue[0];

    BQ_LOGV("acquireBuffer: acquiring { slot=%d/%" PRIu64 " buffer=%p }",
		    slot, outBuffer->mFrameNumber, outBuffer->mGraphicBuffer->handle);
    // If the front buffer is still being tracked, update its slot state
    mSlots[slot].mAcquireCalled = true;
    mSlots[slot].mNeedsCleanupOnRelease = false;
    mSlots[slot].mBufferState = BufferSlot::ACQUIRED;
    mSlots[slot].mFence = Fence::NO_FENCE;

    // If the buffer has previously been acquired by the consumer, set
    // mGraphicBuffer to NULL to avoid unnecessarily remapping this buffer
    // on the consumer side
    //outBuffer->mGraphicBuffer = NULL;
    // We might have freed a slot while dropping old buffers, or the producer
    // may be blocked waiting for the number of buffers in the queue to
    // decrease.
    mCore->mDequeueCondition.broadcast();


    return NO_ERROR;
}

status_t BufferQueueConsumerBF::detachBuffer(int slot) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(slot);
    BQ_LOGV("detachBuffer(C): slot %d", slot);
    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("detachBuffer(C): BufferQueue has been abandoned");
        return NO_INIT;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        BQ_LOGE("detachBuffer(C): slot index %d out of range [0, %d)",
                slot, BufferQueueDefs::NUM_BUFFER_SLOTS);
        return BAD_VALUE;
    } else if (mSlots[slot].mBufferState != BufferSlot::ACQUIRED) {
        BQ_LOGE("detachBuffer(C): slot %d is not owned by the consumer "
                "(state = %d)", slot, mSlots[slot].mBufferState);
        return BAD_VALUE;
    }

    mCore->freeBufferLocked(slot);
    mCore->mDequeueCondition.broadcast();

    return NO_ERROR;
}

status_t BufferQueueConsumerBF::attachBuffer(int* outSlot,
        const sp<android::GraphicBuffer>& buffer) {
    ATRACE_CALL();

    if (outSlot == NULL) {
        BQ_LOGE("attachBuffer(P): outSlot must not be NULL");
        return BAD_VALUE;
    } else if (buffer == NULL) {
        BQ_LOGE("attachBuffer(P): cannot attach NULL buffer");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mCore->mMutex);

    // Make sure we don't have too many acquired buffers and find a free slot
    // to put the buffer into (the oldest if there are multiple).
    *outSlot = 0;
    ATRACE_BUFFER_INDEX(*outSlot);
    BQ_LOGV("attachBuffer(C): returning slot %d", *outSlot);

    mSlots[*outSlot].mGraphicBuffer = buffer;
    mSlots[*outSlot].mBufferState = BufferSlot::ACQUIRED;
    mSlots[*outSlot].mAttachedByConsumer = true;
    mSlots[*outSlot].mNeedsCleanupOnRelease = false;
    mSlots[*outSlot].mFence = Fence::NO_FENCE;
    mSlots[*outSlot].mFrameNumber = 0;

    // mAcquireCalled tells BufferQueue that it doesn't need to send a valid
    // GraphicBuffer pointer on the next acquireBuffer call, which decreases
    // Binder traffic by not un/flattening the GraphicBuffer. However, it
    // requires that the consumer maintain a cached copy of the slot <--> buffer
    // mappings, which is why the consumer doesn't need the valid pointer on
    // acquire.
    //
    // The StreamSplitter is one of the primary users of the attach/detach
    // logic, and while it is running, all buffers it acquires are immediately
    // detached, and all buffers it eventually releases are ones that were
    // attached (as opposed to having been obtained from acquireBuffer), so it
    // doesn't make sense to maintain the slot/buffer mappings, which would
    // become invalid for every buffer during detach/attach. By setting this to
    // false, the valid GraphicBuffer pointer will always be sent with acquire
    // for attached buffers.
    mSlots[*outSlot].mAcquireCalled = false;

    return NO_ERROR;
}

status_t BufferQueueConsumerBF::releaseBuffer(int slot, uint64_t frameNumber,
        const sp<Fence>& releaseFence, EGLDisplay eglDisplay,
        EGLSyncKHR eglFence) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(slot);

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS ||
            releaseFence == NULL) {
        BQ_LOGE("releaseBuffer: slot %d out of range or fence %p NULL", slot,
                releaseFence.get());
        return BAD_VALUE;
    }
    if (slot > 0){
        BQ_LOGE("releaseBuffer: only support slot 0");
	return BAD_VALUE;
    }
    sp<IProducerListener> listener;
    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);

        // If the frame number has changed because the buffer has been reallocated,
        // we can ignore this releaseBuffer for the old buffer
        if (frameNumber != mSlots[slot].mFrameNumber) {
            return STALE_BUFFER_SLOT;
        }

        // Make sure this buffer hasn't been queued while acquired by the consumer

        if (mSlots[slot].mBufferState == BufferSlot::ACQUIRED) {
            mSlots[slot].mEglDisplay = eglDisplay;
            mSlots[slot].mEglFence = eglFence;
            mSlots[slot].mFence = releaseFence;
            mSlots[slot].mBufferState = BufferSlot::FREE;
            listener = mCore->mConnectedProducerListener;
            BQ_LOGV("releaseBuffer: releasing slot %d", slot);
        } else if (mSlots[slot].mNeedsCleanupOnRelease) {
            BQ_LOGV("releaseBuffer: releasing a stale buffer slot %d "
                    "(state = %d)", slot, mSlots[slot].mBufferState);
            mSlots[slot].mNeedsCleanupOnRelease = false;
            return STALE_BUFFER_SLOT;
        } else {
            BQ_LOGE("releaseBuffer: attempted to release buffer slot %d "
                    "but its state was %d", slot, mSlots[slot].mBufferState);
            return BAD_VALUE;
        }

        mCore->mDequeueCondition.broadcast();
    } // Autolock scope

    // Call back without lock held
    if (listener != NULL) {
        listener->onBufferReleased();
    }

    return NO_ERROR;
}

status_t BufferQueueConsumerBF::connect(
        const sp<IConsumerListener>& consumerListener, bool controlledByApp) {
    ATRACE_CALL();

    if (consumerListener == NULL) {
        BQ_LOGE("connect(C): consumerListener may not be NULL");
        return BAD_VALUE;
    }

    BQ_LOGV("connect(C): controlledByApp=%s",
            controlledByApp ? "true" : "false");

    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("connect(C): BufferQueue has been abandoned");
        return NO_INIT;
    }

    mCore->mConsumerListener = consumerListener;
    mCore->mConsumerControlledByApp = controlledByApp;

    return NO_ERROR;
}

status_t BufferQueueConsumerBF::disconnect() {
    ATRACE_CALL();

    BQ_LOGD("disconnect(C)");

    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mConsumerListener == NULL) {
        BQ_LOGE("disconnect(C): no consumer is connected");
        return BAD_VALUE;
    }

    mCore->mIsAbandoned = true;
    mCore->mConsumerListener = NULL;
    mCore->mQueue.clear();
    mCore->freeAllBuffersLocked();
    mCore->mDequeueCondition.broadcast();
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::getReleasedBuffers(uint64_t *outSlotMask) {
    ATRACE_CALL();

    if (outSlotMask == NULL) {
        BQ_LOGE("getReleasedBuffers: outSlotMask may not be NULL");
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("getReleasedBuffers: BufferQueue has been abandoned");
        return NO_INIT;
    }
    BQ_LOGD("getReleasedBuffers: mIsAbandoned is false");
    uint64_t mask = 1; //release buffer 0
    // Remove from the mask queued buffers for which acquire has been called,
    // since the consumer will not receive their buffer addresses and so must
    // retain their cached information

    BQ_LOGD("getReleasedBuffers: returning mask %#" PRIx64, mask);
    *outSlotMask = mask;
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::setDefaultBufferSize(uint32_t width,
        uint32_t height) {
    ATRACE_CALL();

    if (width == 0 || height == 0) {
        BQ_LOGV("setDefaultBufferSize: dimensions cannot be 0 (width=%u "
                "height=%u)", width, height);
        return BAD_VALUE;
    }

    BQ_LOGV("setDefaultBufferSize: width=%u height=%u", width, height);

    Mutex::Autolock lock(mCore->mMutex);
    mCore->mDefaultWidth = width;
    mCore->mDefaultHeight = height;
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::setDefaultMaxBufferCount(int bufferCount) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCore->mMutex);
    return mCore->setDefaultMaxBufferCountLocked(bufferCount);
}

status_t BufferQueueConsumerBF::disableAsyncBuffer() {
    ATRACE_CALL();

    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mConsumerListener != NULL) {
        BQ_LOGE("disableAsyncBuffer: consumer already connected");
        return INVALID_OPERATION;
    }

    BQ_LOGV("disableAsyncBuffer");
    mCore->mUseAsyncBuffer = false;
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::setMaxAcquiredBufferCount(
        int maxAcquiredBuffers) {
    ATRACE_CALL();

    if (maxAcquiredBuffers < 1 ||
            maxAcquiredBuffers > BufferQueueCoreBF::MAX_MAX_ACQUIRED_BUFFERS) {
        BQ_LOGE("setMaxAcquiredBufferCount: invalid count %d",
                maxAcquiredBuffers);
        return BAD_VALUE;
    }

    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mConnectedApi != BufferQueueCoreBF::NO_CONNECTED_API) {
        BQ_LOGE("setMaxAcquiredBufferCount: producer is already connected");
        return INVALID_OPERATION;
    }

    BQ_LOGV("setMaxAcquiredBufferCount: %d", maxAcquiredBuffers);
    mCore->mMaxAcquiredBufferCount = 1;
    return NO_ERROR;
}

void BufferQueueConsumerBF::setConsumerName(const String8& name) {
    ATRACE_CALL();
    BQ_LOGV("setConsumerName: '%s'", name.string());
    Mutex::Autolock lock(mCore->mMutex);
    mCore->mConsumerName = name;
    mConsumerName = name;
}

status_t BufferQueueConsumerBF::setDefaultBufferFormat(uint32_t defaultFormat) {
    ATRACE_CALL();
    BQ_LOGV("setDefaultBufferFormat: %u", defaultFormat);
    Mutex::Autolock lock(mCore->mMutex);
    mCore->mDefaultBufferFormat = defaultFormat;
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::setConsumerUsageBits(uint32_t usage) {
    ATRACE_CALL();
    BQ_LOGV("setConsumerUsageBits: %#x", usage);
    Mutex::Autolock lock(mCore->mMutex);
    mCore->mConsumerUsageBits = usage;
    return NO_ERROR;
}

status_t BufferQueueConsumerBF::setTransformHint(uint32_t hint) {
    ATRACE_CALL();
    BQ_LOGV("setTransformHint: %#x", hint);
    Mutex::Autolock lock(mCore->mMutex);
    mCore->mTransformHint = hint;
    return NO_ERROR;
}

sp<NativeHandle> BufferQueueConsumerBF::getSidebandStream() const {
    return mCore->mSidebandStream;
}

void BufferQueueConsumerBF::dump(String8& result, const char* prefix) const {
    mCore->dump(result, prefix);
}

} // namespace android
