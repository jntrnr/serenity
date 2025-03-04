/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <Kernel/FileSystem/File.h>

namespace Kernel {

class AnonymousFile final : public File {
public:
    static NonnullRefPtr<AnonymousFile> create(NonnullRefPtr<AnonymousVMObject> vmobject)
    {
        return adopt(*new AnonymousFile(move(vmobject)));
    }

    virtual ~AnonymousFile() override;

    virtual KResultOr<Region*> mmap(Process&, FileDescription&, VirtualAddress preferred_vaddr, size_t offset, size_t size, int prot, bool shared) override;

private:
    virtual const char* class_name() const override { return "AnonymousFile"; }
    virtual String absolute_path(const FileDescription&) const override { return ":anonymous-file:"; }
    virtual bool can_read(const FileDescription&, size_t) const override { return false; }
    virtual bool can_write(const FileDescription&, size_t) const override { return false; }
    virtual KResultOr<size_t> read(FileDescription&, size_t, UserOrKernelBuffer&, size_t) override { return KResult(-ENOTSUP); }
    virtual KResultOr<size_t> write(FileDescription&, size_t, const UserOrKernelBuffer&, size_t) override { return KResult(-ENOTSUP); }

    explicit AnonymousFile(NonnullRefPtr<AnonymousVMObject>);

    NonnullRefPtr<AnonymousVMObject> m_vmobject;
};

}
