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

#define LOG_TAG "BufferQueueProducerBF"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#define EGL_EGLEXT_PROTOTYPES

#include <gui/BufferItem.h>
#include <gui/BufferQueueCoreBF.h>
#include <gui/BufferQueueProducerBF.h>
#include <gui/IConsumerListener.h>
#include <gui/IGraphicBufferAlloc.h>
#include <gui/IProducerListener.h>

#include <utils/Log.h>
#include <utils/Trace.h>

namespace android {

BufferQueueProducerBF::BufferQueueProducerBF(const sp<BufferQueueCoreBF>& core) :
    mCore(core),
    mSlots(core->mSlots),
    mConsumerName(),
    mStickyTransform(0),
    mLastQueueBufferFence(Fence::NO_FENCE),
    mCallbackMutex(),
    mNextCallbackTicket(0),
    mCurrentCallbackTicket(0),
    mCallbackCondition(),
    mStaticGraphicBuffer(NULL) {}

BufferQueueProducerBF::~BufferQueueProducerBF() {}

status_t BufferQueueProducerBF::requestBuffer(int slot, sp<GraphicBuffer>* buf) {
    ATRACE_CALL();
    BQ_LOGD("requestBuffer: slot %d,[only slot 0 supported]", slot);
    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("requestBuffer: BufferQueue has been abandoned");
        return NO_INIT;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        BQ_LOGE("requestBuffer: slot index %d out of range [0, %d)",
                slot, BufferQueueDefs::NUM_BUFFER_SLOTS);
        return BAD_VALUE;
    }

    mSlots[0].mRequestBufferCalled = true;
    *buf = mSlots[0].mGraphicBuffer; //get Graphic buffer 
    BQ_LOGD("requestBuffer: slot %d, get graphic buffer %p[%p] Successed!",slot,buf->get(),(*buf)->handle);
    return NO_ERROR;
}

status_t BufferQueueProducerBF::setBufferCount(int bufferCount) {
    ATRACE_CALL();
    BQ_LOGV("setBufferCount: count = %d", bufferCount);
    if (bufferCount > 1 ){
    	BQ_LOGV("setBufferCount could not beyond 1");
    } 
    sp<IConsumerListener> listener;
    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);
        mCore->waitWhileAllocatingLocked();

        if (mCore->mIsAbandoned) {
            BQ_LOGE("setBufferCount: BufferQueue has been abandoned");
            return NO_INIT;
        }

        if (bufferCount > BufferQueueDefs::NUM_BUFFER_SLOTS) {
            BQ_LOGE("setBufferCount: bufferCount %d too large (max %d)",
                    bufferCount, BufferQueueDefs::NUM_BUFFER_SLOTS);
            return BAD_VALUE;
        }

        // There must be no dequeued buffers when changing the buffer count.
        for (int s = 0; s < BufferQueueDefs::NUM_BUFFER_SLOTS; ++s) {
            if (mSlots[s].mBufferState == BufferSlot::DEQUEUED) {
                BQ_LOGE("setBufferCount: buffer owned by producer");
                return BAD_VALUE;
            }
        }

        if (bufferCount == 0) {
            mCore->mOverrideMaxBufferCount = 0;
            mCore->mDequeueCondition.broadcast();
            return NO_ERROR;
        }


        mCore->freeAllBuffersLocked();
        mCore->mOverrideMaxBufferCount = 1;
        mCore->mDequeueCondition.broadcast();
        listener = mCore->mConsumerListener;
    } // Autolock scope

    // Call back without lock held
    if (listener != NULL) {
        listener->onBuffersReleased();
    }

    return NO_ERROR;
}


status_t BufferQueueProducerBF::dequeueBuffer(int *outSlot,
        sp<android::Fence> *outFence, bool async,
        uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    static int callCounter=0;
    ATRACE_CALL();
    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);
        mConsumerName = mCore->mConsumerName;
    } // Autolock scope
    /*if (callCounter > 0){ 
    	BQ_LOGD("dequeueBuffer can only be called once \n");
	return BAD_VALUE;
    }*/
    BQ_LOGD("dequeueBuffer: async=%s w=%u h=%u format=%#x, usage=%#x",
            async ? "true" : "false", width, height, format, usage);

    if ((width && !height) || (!width && height)) {
        BQ_LOGE("dequeueBuffer: invalid size: w=%u h=%u", width, height);
        return BAD_VALUE;
    }

    status_t returnFlags = NO_ERROR;
    EGLSyncKHR eglFence = EGL_NO_SYNC_KHR;
    bool attachedByConsumer = false;

    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);
        mCore->waitWhileAllocatingLocked();

        if (format == 0) {
            format = mCore->mDefaultBufferFormat;
        }

        // Enable the usage bits the consumer requested
        usage |= mCore->mConsumerUsageBits;


        *outSlot = 0;

        attachedByConsumer = mSlots[0].mAttachedByConsumer;

        const bool useDefaultSize = !width && !height;
        if (useDefaultSize) {
            width = mCore->mDefaultWidth;
            height = mCore->mDefaultHeight;
	    BQ_LOGD("dequeueBuffer:use default size %d x%d",width,height);
        }

	mSlots[0].mAcquireCalled = false;
	mSlots[0].mGraphicBuffer = mStaticGraphicBuffer;
	mSlots[0].mRequestBufferCalled = false;
	mSlots[0].mEglDisplay = EGL_NO_DISPLAY;


        eglFence = mSlots[0].mEglFence;
        mSlots[0].mEglFence = EGL_NO_SYNC_KHR;
        mSlots[0].mFence = Fence::NO_FENCE;
	*outFence = mSlots[0].mFence;
    } // Autolock scope

        status_t error;
        BQ_LOGD("dequeueBuffer: allocating a new buffer for slot %d", *outSlot);
	if(mStaticGraphicBuffer == NULL) {
		sp<GraphicBuffer> graphicBuffer(mCore->mAllocator->createGraphicBuffer(
					width, height, format, usage|0x80000000, &error));
		if (graphicBuffer == NULL) {
			BQ_LOGE("dequeueBuffer: createGraphicBuffer failed");
			return error;
		}

		{ // Autolock scope
			Mutex::Autolock lock(mCore->mMutex);

			if (mCore->mIsAbandoned) {
				BQ_LOGE("dequeueBuffer: BufferQueue has been abandoned");
				return NO_INIT;
			}

			mSlots[0].mFrameNumber = UINT32_MAX;
			mSlots[0].mGraphicBuffer = graphicBuffer;
			mStaticGraphicBuffer=graphicBuffer;
		} // Autolock scope
	}


    BQ_LOGD("dequeueBuffer:[SUCCESS] returning slot=%d/%" PRIu64 " buf=%p[%p] flags=%#x",
            *outSlot,
            mSlots[*outSlot].mFrameNumber,
            mSlots[*outSlot].mGraphicBuffer.get(), mSlots[*outSlot].mGraphicBuffer->handle, returnFlags);
    callCounter++;

    return returnFlags;
}

status_t BufferQueueProducerBF::detachBuffer(int slot) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(slot);
    BQ_LOGV("detachBuffer(P): slot %d", slot);
    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("detachBuffer(P): BufferQueue has been abandoned");
        return NO_INIT;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        BQ_LOGE("detachBuffer(P): slot index %d out of range [0, %d)",
                slot, BufferQueueDefs::NUM_BUFFER_SLOTS);
        return BAD_VALUE;
    } else if (mSlots[slot].mBufferState != BufferSlot::DEQUEUED) {
        BQ_LOGE("detachBuffer(P): slot %d is not owned by the producer "
                "(state = %d)", slot, mSlots[slot].mBufferState);
        return BAD_VALUE;
    } else if (!mSlots[slot].mRequestBufferCalled) {
        BQ_LOGE("detachBuffer(P): buffer in slot %d has not been requested",
                slot);
        return BAD_VALUE;
    }

    mCore->freeBufferLocked(slot);
    mCore->mDequeueCondition.broadcast();

    return NO_ERROR;
}

status_t BufferQueueProducerBF::detachNextBuffer(sp<GraphicBuffer>* outBuffer,
        sp<Fence>* outFence) {
    ATRACE_CALL();

    BQ_LOGE("detachNextBuffer: not supported");
    return BAD_VALUE;    

}

status_t BufferQueueProducerBF::attachBuffer(int* outSlot,
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
    mCore->waitWhileAllocatingLocked();

    status_t returnFlags = NO_ERROR;


    *outSlot = 0;
    ATRACE_BUFFER_INDEX(*outSlot);
    BQ_LOGV("attachBuffer(P): returning slot %d flags=%#x",
            *outSlot, returnFlags);

    mSlots[*outSlot].mGraphicBuffer = buffer;
    mSlots[*outSlot].mBufferState = BufferSlot::DEQUEUED;
    mSlots[*outSlot].mEglFence = EGL_NO_SYNC_KHR;
    mSlots[*outSlot].mFence = Fence::NO_FENCE;
    mSlots[*outSlot].mRequestBufferCalled = true;

    return returnFlags;
}

status_t BufferQueueProducerBF::queueBuffer(int slot,
        const QueueBufferInput &input, QueueBufferOutput *output) {
    ATRACE_CALL();
    ATRACE_BUFFER_INDEX(slot);

    int64_t timestamp;
    bool isAutoTimestamp;
    Rect crop;
#ifdef QCOM_BSP
    Rect dirtyRect;
#endif
    int scalingMode;
    uint32_t transform;
    uint32_t stickyTransform;
    bool async;
    sp<Fence> fence;

    input.deflate(&timestamp, &isAutoTimestamp, &crop,
#ifdef QCOM_BSP
            &dirtyRect,
#endif
            &scalingMode, &transform,
            &async, &fence, &stickyTransform);

    fence=Fence::NO_FENCE;
    switch (scalingMode) {
        case NATIVE_WINDOW_SCALING_MODE_FREEZE:
        case NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW:
        case NATIVE_WINDOW_SCALING_MODE_SCALE_CROP:
        case NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP:
            break;
        default:
            BQ_LOGE("queueBuffer: unknown scaling mode %d", scalingMode);
            return BAD_VALUE;
    }

    sp<IConsumerListener> frameAvailableListener;
    sp<IConsumerListener> frameReplacedListener;
    int callbackTicket = 0;
    BufferItem item;
    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);

        if (mCore->mIsAbandoned) {
            BQ_LOGE("queueBuffer: BufferQueue has been abandoned");
            return NO_INIT;
        }


        BQ_LOGD("queueBuffer: slot=%d/%" PRIu64 " time=%" PRIu64
                " crop=[%d,%d,%d,%d] transform=%#x scale=%s",
                slot, mCore->mFrameCounter + 1, timestamp,
                crop.left, crop.top, crop.right, crop.bottom,
                transform, BufferItem::scalingModeName(scalingMode));


        mSlots[0].mFence = fence;
//        mSlots[0].mBufferState = BufferSlot::QUEUED; no state needed
        ++mCore->mFrameCounter;
        mSlots[0].mFrameNumber = mCore->mFrameCounter;

        item.mAcquireCalled = mSlots[0].mAcquireCalled;
        item.mGraphicBuffer = mSlots[0].mGraphicBuffer;
        item.mCrop = crop;
#ifdef QCOM_BSP
        item.mDirtyRect = dirtyRect;
#endif
        item.mTransform = transform & ~NATIVE_WINDOW_TRANSFORM_INVERSE_DISPLAY;
        item.mTransformToDisplayInverse =
                bool(transform & NATIVE_WINDOW_TRANSFORM_INVERSE_DISPLAY);
        item.mScalingMode = scalingMode;
        item.mTimestamp = timestamp;
        item.mIsAutoTimestamp = isAutoTimestamp;
        item.mFrameNumber = mCore->mFrameCounter;
        item.mSlot = slot;
        item.mFence = fence;
        item.mIsDroppable = mCore->mDequeueBufferCannotBlock || async;

        mStickyTransform = stickyTransform;
	
	BQ_LOGD("prepare item ok");
	mCore->mQueue.clear();
	mCore->mQueue.push_back(item);
	frameAvailableListener = mCore->mConsumerListener;
	frameReplacedListener = mCore->mConsumerListener;
        mCore->mBufferHasBeenQueued = true;
        mCore->mDequeueCondition.broadcast();
	BQ_LOGD("mcore enqueue completed");
        output->inflate(mCore->mDefaultWidth, mCore->mDefaultHeight,
                mCore->mTransformHint, mCore->mQueue.size());

        ATRACE_INT(mCore->mConsumerName.string(), mCore->mQueue.size());

        // Take a ticket for the callback functions
        callbackTicket = mNextCallbackTicket++;
    } // Autolock scope


    // Don't send the GraphicBuffer through the callback, and don't send
    // the slot number, since the consumer shouldn't need it
    BQ_LOGD("Graphic buffer clear.");
    //item.mGraphicBuffer.clear();
    //item.mSlot = BufferItem::INVALID_BUFFER_SLOT;

    // Call back without the main BufferQueue lock held, but with the callback
    // lock held so we can ensure that callbacks occur in order

    if (frameAvailableListener != NULL) {
	frameAvailableListener->onFrameAvailable(item);
    } else {
	BQ_LOGE("BFProducer: frameavlable listner is null");
    }
    /*if (frameAvailableListener != NULL) {
	    frameAvailableListener->onFrameAvailable(item);
    } else if (frameReplacedListener != NULL) {
	    frameReplacedListener->onFrameReplaced(item);
    }*/

    BQ_LOGD("QueueBuffer: Notify layer .");

    return NO_ERROR;
}

void BufferQueueProducerBF::cancelBuffer(int slot, const sp<Fence>& fence) {
    ATRACE_CALL();
    BQ_LOGV("cancelBuffer: slot %d,only support slot 0", slot);
    Mutex::Autolock lock(mCore->mMutex);

    if (mCore->mIsAbandoned) {
        BQ_LOGE("cancelBuffer: BufferQueue has been abandoned");
        return;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        BQ_LOGE("cancelBuffer: slot index %d out of range [0, %d)",
                slot, BufferQueueDefs::NUM_BUFFER_SLOTS);
        return;
    } else if (fence == NULL) {
        BQ_LOGE("cancelBuffer: fence is NULL");
        return;
    }

    //mSlots[0].mBufferState = BufferSlot::FREE;
    mSlots[0].mFrameNumber = 0;
    mSlots[0].mFence = fence;
    mCore->mDequeueCondition.broadcast();
}

int BufferQueueProducerBF::query(int what, int *outValue) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCore->mMutex);

    if (outValue == NULL) {
        BQ_LOGE("query: outValue was NULL");
        return BAD_VALUE;
    }

    if (mCore->mIsAbandoned) {
        BQ_LOGE("query: BufferQueue has been abandoned");
        return NO_INIT;
    }

    int value;
    switch (what) {
        case NATIVE_WINDOW_WIDTH:
            value = mCore->mDefaultWidth;
            break;
        case NATIVE_WINDOW_HEIGHT:
            value = mCore->mDefaultHeight;
            break;
        case NATIVE_WINDOW_FORMAT:
            value = mCore->mDefaultBufferFormat;
            break;
        case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
            value = 1;
            break;
        case NATIVE_WINDOW_STICKY_TRANSFORM:
            value = static_cast<int>(mStickyTransform);
            break;
        case NATIVE_WINDOW_CONSUMER_RUNNING_BEHIND:
            value = 1;
            break;
        case NATIVE_WINDOW_CONSUMER_USAGE_BITS:
            value = mCore->mConsumerUsageBits;
            break;
        default:
            return BAD_VALUE;
    }

    BQ_LOGV("query: %d? %d", what, value);
    *outValue = value;
    return NO_ERROR;
}

status_t BufferQueueProducerBF::connect(const sp<IProducerListener>& listener,
        int api, bool producerControlledByApp, QueueBufferOutput *output) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCore->mMutex);
    mConsumerName = mCore->mConsumerName;
    BQ_LOGD("connect(P): api=%d producerControlledByApp=%s", api,
            producerControlledByApp ? "true" : "false");

    if (mCore->mIsAbandoned) {
        BQ_LOGE("connect(P): BufferQueue has been abandoned");
        return NO_INIT;
    }

    if (mCore->mConsumerListener == NULL) {
        BQ_LOGE("connect(P): BufferQueue has no consumer");
        return NO_INIT;
    }

    if (output == NULL) {
        BQ_LOGE("connect(P): output was NULL");
        return BAD_VALUE;
    }

    if (mCore->mConnectedApi != BufferQueueCoreBF::NO_CONNECTED_API) {
        BQ_LOGE("connect(P): already connected (cur=%d req=%d)",
                mCore->mConnectedApi, api);
        return BAD_VALUE;
    }

    int status = NO_ERROR;
    switch (api) {
        case NATIVE_WINDOW_API_EGL:
        case NATIVE_WINDOW_API_CPU:
        case NATIVE_WINDOW_API_MEDIA:
        case NATIVE_WINDOW_API_CAMERA:
            mCore->mConnectedApi = api;
            output->inflate(mCore->mDefaultWidth, mCore->mDefaultHeight,
                    mCore->mTransformHint, mCore->mQueue.size());

            // Set up a death notification so that we can disconnect
            // automatically if the remote producer dies
            if (listener != NULL &&
                    listener->asBinder()->remoteBinder() != NULL) {
                status = listener->asBinder()->linkToDeath(
                        static_cast<IBinder::DeathRecipient*>(this));
                if (status != NO_ERROR) {
                    BQ_LOGE("connect(P): linkToDeath failed: %s (%d)",
                            strerror(-status), status);
                }
            }
            mCore->mConnectedProducerListener = listener;
            break;
        default:
            BQ_LOGE("connect(P): unknown API %d", api);
            status = BAD_VALUE;
            break;
    }

    mCore->mBufferHasBeenQueued = false;
    mCore->mDequeueBufferCannotBlock =
            mCore->mConsumerControlledByApp && producerControlledByApp;

    return status;
}

status_t BufferQueueProducerBF::disconnect(int api) {
    ATRACE_CALL();
    BQ_LOGD("disconnect(P): api %d", api);
    
    int status = NO_ERROR;
    sp<IConsumerListener> listener;
    { // Autolock scope
        Mutex::Autolock lock(mCore->mMutex);
        mCore->waitWhileAllocatingLocked();

        if (mCore->mIsAbandoned) {
            // It's not really an error to disconnect after the surface has
            // been abandoned; it should just be a no-op.
            return NO_ERROR;
        }

        switch (api) {
            case NATIVE_WINDOW_API_EGL:
            case NATIVE_WINDOW_API_CPU:
            case NATIVE_WINDOW_API_MEDIA:
            case NATIVE_WINDOW_API_CAMERA:
                if (mCore->mConnectedApi == api) {
                    mCore->freeAllBuffersLocked();

                    // Remove our death notification callback if we have one
                    if (mCore->mConnectedProducerListener != NULL) {
                        sp<IBinder> token =
                                mCore->mConnectedProducerListener->asBinder();
                        // This can fail if we're here because of the death
                        // notification, but we just ignore it
                        token->unlinkToDeath(
                                static_cast<IBinder::DeathRecipient*>(this));
                    }
                    mCore->mConnectedProducerListener = NULL;
                    mCore->mConnectedApi = BufferQueueCoreBF::NO_CONNECTED_API;
                    mCore->mSidebandStream.clear();
                    mCore->mDequeueCondition.broadcast();
                    listener = mCore->mConsumerListener;
                } else {
                    BQ_LOGE("disconnect(P): connected to another API "
                            "(cur=%d req=%d)", mCore->mConnectedApi, api);
                    status = BAD_VALUE;
                }
                break;
            default:
                BQ_LOGE("disconnect(P): unknown API %d", api);
                status = BAD_VALUE;
                break;
        }
    } // Autolock scope

    // Call back without lock held
    if (listener != NULL) {
        listener->onBuffersReleased();
    }

    return status;
}

status_t BufferQueueProducerBF::setSidebandStream(const sp<NativeHandle>& stream) {
    sp<IConsumerListener> listener;
    { // Autolock scope
        Mutex::Autolock _l(mCore->mMutex);
        mCore->mSidebandStream = stream;
        listener = mCore->mConsumerListener;
    } // Autolock scope

    if (listener != NULL) {
        listener->onSidebandStreamChanged();
    }
    return NO_ERROR;
}

#ifdef QCOM_BSP_LEGACY
status_t BufferQueueProducerBF::setBuffersSize(int size) {
    BQ_LOGV("setBuffersSize: size=%d", size);
    Mutex::Autolock _l(mCore->mMutex);
    mCore->mAllocator->setGraphicBufferSize(size);
    return NO_ERROR;
}
#endif

void BufferQueueProducerBF::allocateBuffers(bool async, uint32_t width,
        uint32_t height, uint32_t format, uint32_t usage) {
    ATRACE_CALL();
    BQ_LOGV("allocateBuffers request");
    Mutex::Autolock lock(mCore->mMutex);
    

    if (mSlots[0].mGraphicBuffer == NULL){
	uint32_t allocWidth = 0;
        uint32_t allocHeight = 0;
        uint32_t allocFormat = 0;
        uint32_t allocUsage = 0;
	 allocWidth = width > 0 ? width : mCore->mDefaultWidth;
         allocHeight = height > 0 ? height : mCore->mDefaultHeight;
         allocFormat = format != 0 ? format : mCore->mDefaultBufferFormat;
         allocUsage = usage | mCore->mConsumerUsageBits;		
   	status_t result; 
	sp<GraphicBuffer> graphicBuffer(mCore->mAllocator->createGraphicBuffer(
                    allocWidth, allocHeight, allocFormat, allocUsage, &result));
	 mSlots[0].mGraphicBuffer =graphicBuffer;
	 
    }
    mCore->mIsAllocating = false;

}

void BufferQueueProducerBF::binderDied(const wp<android::IBinder>& /* who */) {
    // If we're here, it means that a producer we were connected to died.
    // We're guaranteed that we are still connected to it because we remove
    // this callback upon disconnect. It's therefore safe to read mConnectedApi
    // without synchronization here.
    BQ_LOGD("Producer died");
    int api = mCore->mConnectedApi;
    disconnect(api);
}

} // namespace android
