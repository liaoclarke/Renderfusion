//===-- AMDGPU.h - MachineFunction passes hw codegen --------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef AMDGPU_H
#define AMDGPU_H

#include "AMDGPUTargetMachine.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class FunctionPass;
class AMDGPUTargetMachine;

// R600 Passes
FunctionPass* createR600KernelParametersPass(const TargetData* TD);
FunctionPass *createR600ExpandSpecialInstrsPass(TargetMachine &tm);

// SI Passes
FunctionPass *createSIAssignInterpRegsPass(TargetMachine &tm);
FunctionPass *createSILowerFlowControlPass(TargetMachine &tm);
FunctionPass *createSICodeEmitterPass(formatted_raw_ostream &OS);
FunctionPass *createSILowerLiteralConstantsPass(TargetMachine &tm);

// Passes common to R600 and SI
FunctionPass *createAMDGPUConvertToISAPass(TargetMachine &tm);

} // End namespace llvm

namespace ShaderType {
  enum Type {
    PIXEL = 0,
    VERTEX = 1,
    GEOMETRY = 2,
    COMPUTE = 3
  };
}

#endif // AMDGPU_H
