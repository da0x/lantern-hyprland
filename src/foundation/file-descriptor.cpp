// SPDX-License-Identifier: GPL-3.0-or-later
#include "foundation/file-descriptor.hpp"

#include <utility>

#include <unistd.h>

namespace foundation {

    FileDescriptor::FileDescriptor(int descriptor) : descriptor{descriptor} {}

    FileDescriptor::~FileDescriptor() {
        if (valid())
            ::close(descriptor);
    }

    FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept : descriptor{std::exchange(other.descriptor, -1)} {}

    FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (valid())
                ::close(descriptor);
            descriptor = std::exchange(other.descriptor, -1);
        }
        return *this;
    }

}
