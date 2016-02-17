// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_SRC_FXCRT_FXCRT_WINDOWS_H_
#define CORE_SRC_FXCRT_FXCRT_WINDOWS_H_

#include "core/src/fxcrt/extension.h"

#if _FXM_PLATFORM_ == _FXM_PLATFORM_WINDOWS_
class CFXCRT_FileAccess_Win64 : public IFXCRT_FileAccess {
 public:
  CFXCRT_FileAccess_Win64();
  ~CFXCRT_FileAccess_Win64() override;

  // IFXCRT_FileAccess
  FX_BOOL Open(const CFX_ByteStringC& fileName, FX_DWORD dwMode) override;
  FX_BOOL Open(const CFX_WideStringC& fileName, FX_DWORD dwMode) override;
  void Close() override;
  void Release() override;
  FX_FILESIZE GetSize() const override;
  FX_FILESIZE GetPosition() const override;
  FX_FILESIZE SetPosition(FX_FILESIZE pos) override;
  size_t Read(void* pBuffer, size_t szBuffer) override;
  size_t Write(const void* pBuffer, size_t szBuffer) override;
  size_t ReadPos(void* pBuffer, size_t szBuffer, FX_FILESIZE pos) override;
  size_t WritePos(const void* pBuffer,
                  size_t szBuffer,
                  FX_FILESIZE pos) override;
  FX_BOOL Flush() override;
  FX_BOOL Truncate(FX_FILESIZE szFile) override;

 protected:
  void* m_hFile;
};
#endif

#endif  // CORE_SRC_FXCRT_FXCRT_WINDOWS_H_
