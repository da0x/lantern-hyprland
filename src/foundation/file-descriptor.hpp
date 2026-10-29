// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace foundation {

    // Owns a file descriptor and closes it once. Moves, never copies.
    class FileDescriptor {
      public:
        FileDescriptor() = default;
        explicit FileDescriptor(int descriptor);
        ~FileDescriptor();

        FileDescriptor(FileDescriptor&& other) noexcept;
        FileDescriptor& operator=(FileDescriptor&& other) noexcept;
        FileDescriptor(const FileDescriptor&)            = delete;
        FileDescriptor& operator=(const FileDescriptor&) = delete;

        [[nodiscard]] int  get() const { return descriptor; }
        [[nodiscard]] bool valid() const { return descriptor >= 0; }

      private:
        int descriptor = -1;
    };

}
