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

#include <aidl/android/hardware/common/MQDescriptor.h>
#include <cutils/native_handle.h>
#include <fmq/MQDescriptorBase.h>

using aidl::android::hardware::common::GrantorDescriptor;
using aidl::android::hardware::common::MQDescriptor;
using android::hardware::MQFlavor;

namespace android {
namespace details {

template <typename T, MQFlavor flavor>
struct AidlMQDescriptorShim {
    // Takes ownership of handle
    AidlMQDescriptorShim(const MQDescriptor& desc);

    // Takes ownership of handle
    AidlMQDescriptorShim(size_t bufferSize, native_handle_t* nHandle, size_t messageSize,
                         bool configureEventFlag = false);

    explicit AidlMQDescriptorShim(const AidlMQDescriptorShim& other)
        : AidlMQDescriptorShim(0, nullptr, 0) {
        *this = other;
    }
    AidlMQDescriptorShim& operator=(const AidlMQDescriptorShim& other);

    ~AidlMQDescriptorShim();

    size_t getSize() const;

    size_t getQuantum() const;

    int32_t getFlags() const;

    bool isHandleValid() const { return mHandle != nullptr; }
    size_t countGrantors() const { return mGrantors.size(); }

    inline const std::vector<GrantorDescriptor>& grantors() const { return mGrantors; }

    inline const ::native_handle_t* handle() const { return mHandle; }

    inline ::native_handle_t* handle() { return mHandle; }

    static const size_t kOffsetOfGrantors;
    static const size_t kOffsetOfHandle;

  private:
    std::vector<GrantorDescriptor> mGrantors;
    native_handle_t* mHandle = nullptr;
    int32_t mQuantum = 0;
    int32_t mFlags = 0;
};

template <typename T, MQFlavor flavor>
AidlMQDescriptorShim<T, flavor>::AidlMQDescriptorShim(const MQDescriptor& desc)
    : mQuantum(desc.quantum), mFlags(desc.flags) {
    mHandle = native_handle_create(1 /* num fds */, 0 /* num ints */);
    mHandle->data[0] = dup(desc.fileDescriptor.get());

    mGrantors.resize(desc.grantors.size());
    for (size_t i = 0; i < desc.grantors.size(); ++i) {
        mGrantors[i] = desc.grantors[i];
    }
}

template <typename T, MQFlavor flavor>
AidlMQDescriptorShim<T, flavor>& AidlMQDescriptorShim<T, flavor>::operator=(
        const AidlMQDescriptorShim& other) {
    mGrantors = other.mGrantors;
    if (mHandle != nullptr) {
        native_handle_close(mHandle);
        native_handle_delete(mHandle);
        mHandle = nullptr;
    }
    mQuantum = other.mQuantum;
    mFlags = other.mFlags;

    if (other.mHandle != nullptr) {
        mHandle = native_handle_create(other.mHandle->numFds, other.mHandle->numInts);

        for (int i = 0; i < other.mHandle->numFds; ++i) {
            mHandle->data[i] = dup(other.mHandle->data[i]);
        }

        memcpy(&mHandle->data[other.mHandle->numFds], &other.mHandle->data[other.mHandle->numFds],
               static_cast<size_t>(other.mHandle->numInts) * sizeof(int));
    }

    return *this;
}

template <typename T, MQFlavor flavor>
AidlMQDescriptorShim<T, flavor>::AidlMQDescriptorShim(size_t bufferSize, native_handle_t* nHandle,
                                                      size_t messageSize, bool configureEventFlag) {
    mHandle = nHandle;
    mQuantum = messageSize;
    mFlags = flavor;
    /*
     * If configureEventFlag is true, allocate an additional spot in mGrantor
     * for containing the fd and offset for mmapping the EventFlag word.
     */
    mGrantors.resize(configureEventFlag ? hardware::details::kMinGrantorCountForEvFlagSupport
                                        : hardware::details::kMinGrantorCount);

    size_t memSize[] = {
            sizeof(hardware::details::RingBufferPosition), /* memory to be allocated for read
                                                            * pointer counter
                                                            */
            sizeof(hardware::details::RingBufferPosition), /* memory to be allocated for write
                                                     pointer counter */
            bufferSize,                   /* memory to be allocated for data buffer */
            sizeof(std::atomic<uint32_t>) /* memory to be allocated for EventFlag word */
    };

    /*
     * Create a default grantor descriptor for read, write pointers and
     * the data buffer. fdIndex parameter is set to 0 by default and
     * the offset for each grantor is contiguous.
     */
    for (size_t grantorPos = 0, offset = 0; grantorPos < mGrantors.size();
         offset += memSize[grantorPos++]) {
        mGrantors[grantorPos] = {
                0 /* grantor flags */, 0 /* fdIndex */,
                static_cast<int32_t>(hardware::details::alignToWordBoundary(offset)),
                static_cast<int64_t>(memSize[grantorPos])};
    }
}

template <typename T, MQFlavor flavor>
AidlMQDescriptorShim<T, flavor>::~AidlMQDescriptorShim() {
    if (mHandle != nullptr) {
        native_handle_close(mHandle);
        native_handle_delete(mHandle);
    }
}

template <typename T, MQFlavor flavor>
size_t AidlMQDescriptorShim<T, flavor>::getSize() const {
    return mGrantors[hardware::details::DATAPTRPOS].extent;
}

template <typename T, MQFlavor flavor>
size_t AidlMQDescriptorShim<T, flavor>::getQuantum() const {
    return mQuantum;
}

template <typename T, MQFlavor flavor>
int32_t AidlMQDescriptorShim<T, flavor>::getFlags() const {
    return mFlags;
}

template <typename T, MQFlavor flavor>
std::string toString(const AidlMQDescriptorShim<T, flavor>& q) {
    std::string os;
    if (flavor & hardware::kSynchronizedReadWrite) {
        os += "fmq_sync";
    }
    if (flavor & hardware::kUnsynchronizedWrite) {
        os += "fmq_unsync";
    }
    os += " {" + toString(q.grantors().size()) + " grantor(s), " +
          "size = " + toString(q.getSize()) + ", .handle = " + toString(q.handle()) +
          ", .quantum = " + toString(q.getQuantum()) + "}";
    return os;
}

}  // namespace details
}  // namespace android
