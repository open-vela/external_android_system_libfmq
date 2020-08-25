/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <aidl/android/hardware/common/MQDescriptor.h>
#include <cutils/native_handle.h>
#include <fmq/AidlMQDescriptorShim.h>
#include <fmq/MessageQueueBase.h>
#include <utils/Log.h>

namespace android {

using aidl::android::hardware::common::MQDescriptor;
using android::details::AidlMQDescriptorShim;
using android::hardware::MQFlavor;

typedef uint64_t RingBufferPosition;

template <typename T, MQFlavor flavor>
struct AidlMessageQueue final : public MessageQueueBase<AidlMQDescriptorShim, T, flavor> {
    typedef AidlMQDescriptorShim<T, flavor> Descriptor;
    /**
     * This constructor uses the external descriptor used with AIDL interfaces.
     * It will create an FMQ based on the descriptor that was obtained from
     * another FMQ instance for communication.
     *
     * @param desc Descriptor from another FMQ that contains all of the
     * information required to create a new instance of that queue.
     * @param resetPointers Boolean indicating whether the read/write pointers
     * should be reset or not.
     */
    AidlMessageQueue(const MQDescriptor& desc, bool resetPointers = true);
    ~AidlMessageQueue() = default;

    /**
     * This constructor uses Ashmem shared memory to create an FMQ
     * that can contain a maximum of 'numElementsInQueue' elements of type T.
     *
     * @param numElementsInQueue Capacity of the AidlMessageQueue in terms of T.
     * @param configureEventFlagWord Boolean that specifies if memory should
     * also be allocated and mapped for an EventFlag word.
     */
    AidlMessageQueue(size_t numElementsInQueue, bool configureEventFlagWord = false);
    MQDescriptor dupeDesc();

  private:
    AidlMessageQueue(const AidlMessageQueue& other) = delete;
    AidlMessageQueue& operator=(const AidlMessageQueue& other) = delete;
    AidlMessageQueue() = delete;
};

template <typename T, MQFlavor flavor>
AidlMessageQueue<T, flavor>::AidlMessageQueue(const MQDescriptor& desc, bool resetPointers)
    : MessageQueueBase<AidlMQDescriptorShim, T, flavor>(Descriptor(desc), resetPointers) {}

template <typename T, MQFlavor flavor>
AidlMessageQueue<T, flavor>::AidlMessageQueue(size_t numElementsInQueue,
                                              bool configureEventFlagWord)
    : MessageQueueBase<AidlMQDescriptorShim, T, flavor>(numElementsInQueue,
                                                        configureEventFlagWord) {}

template <typename T, MQFlavor flavor>
MQDescriptor AidlMessageQueue<T, flavor>::dupeDesc() {
    auto* shim = MessageQueueBase<AidlMQDescriptorShim, T, flavor>::getDesc();
    if (shim) {
        std::vector<aidl::android::hardware::common::GrantorDescriptor> grantors;
        for (const auto& grantor : shim->grantors()) {
            grantors.push_back(aidl::android::hardware::common::GrantorDescriptor{
                    .offset = static_cast<int32_t>(grantor.offset),
                    .extent = static_cast<int64_t>(grantor.extent)});
        }
        return MQDescriptor{
                .quantum = static_cast<int32_t>(shim->getQuantum()),
                .grantors = grantors,
                .flags = static_cast<int32_t>(shim->getFlags()),
                .fileDescriptor = ndk::ScopedFileDescriptor(dup(shim->handle()->data[0])),
        };
    } else {
        return MQDescriptor();
    }
}

}  // namespace android
