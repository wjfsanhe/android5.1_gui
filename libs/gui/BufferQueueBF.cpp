/*
 * Copyright (C) 2012 The Android Open Source Project
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

#define LOG_TAG "BufferQueueBF"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS
//#define LOG_NDEBUG 0

#include <gui/BufferQueueBF.h>
#include <gui/BufferQueueConsumerBF.h>
#include <gui/BufferQueueCoreBF.h>
#include <gui/BufferQueueProducerBF.h>

namespace android {

BufferQueueBF::ProxyConsumerListener::ProxyConsumerListener(
        const wp<ConsumerListener>& consumerListener):
        mConsumerListener(consumerListener) {}

BufferQueueBF::ProxyConsumerListener::~ProxyConsumerListener() {}

void BufferQueueBF::ProxyConsumerListener::onFrameAvailable(
        const android::BufferItem& item) {
    sp<ConsumerListener> listener(mConsumerListener.promote());
    
    ALOGD("proxy onframe enter");
    if (listener != NULL) {
	ALOGD("proxy call listener");
        listener->onFrameAvailable(item);
    }
}

void BufferQueueBF::ProxyConsumerListener::onBuffersReleased() {
    sp<ConsumerListener> listener(mConsumerListener.promote());
    if (listener != NULL) {
        listener->onBuffersReleased();
    }
}

void BufferQueueBF::ProxyConsumerListener::onSidebandStreamChanged() {
    sp<ConsumerListener> listener(mConsumerListener.promote());
    if (listener != NULL) {
        listener->onSidebandStreamChanged();
    }
}
void BufferQueueBF::createBufferQueue(sp<IGraphicBufferProducer>* outProducer,
        sp<IGraphicBufferConsumer>* outConsumer,
        const sp<IGraphicBufferAlloc>& allocator) {
    LOG_ALWAYS_FATAL_IF(outProducer == NULL,
            "BufferQueueBF: outProducer must not be NULL");
    LOG_ALWAYS_FATAL_IF(outConsumer == NULL,
            "BufferQueueBF: outConsumer must not be NULL");

    sp<BufferQueueCoreBF> core(new BufferQueueCoreBF(allocator));
    LOG_ALWAYS_FATAL_IF(core == NULL,
            "BufferQueueBF: failed to create BufferQueueCore");

    sp<IGraphicBufferProducer> producer(new BufferQueueProducerBF(core));
    LOG_ALWAYS_FATAL_IF(producer == NULL,
            "BufferQueueBF: failed to create BufferQueueProducer");

    sp<IGraphicBufferConsumer> consumer(new BufferQueueConsumerBF(core));
    LOG_ALWAYS_FATAL_IF(consumer == NULL,
            "BufferQueueBF: failed to create BufferQueueConsumer");

    *outProducer = producer;
    *outConsumer = consumer;

    ALOGD("BufferQueueBF create relative Buffer Producer&Consumer");
}

}; // namespace android
