/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MIR.h"
#include "MIRGraph.h"
#include "LIR.h"
#include "IonSpewer.h"
#include "LIR-inl.h"

using namespace js;
using namespace js::ion;

LIRGraph::LIRGraph(MIRGraph *mir)
  : numVirtualRegisters_(0),
    numInstructions_(1), // First id is 1.
    localSlotCount_(0),
    argumentSlotCount_(0),
    entrySnapshot_(NULL),
    osrBlock_(NULL),
    mir_(*mir)
{
}

bool
LIRGraph::addConstantToPool(const Value &v, uint32 *index)
{
    *index = constantPool_.length();
    return constantPool_.append(v);
}

bool
LIRGraph::noteNeedsSafepoint(LInstruction *ins)
{
    // Instructions with safepoints must be in linear order.
    JS_ASSERT_IF(safepoints_.length(), safepoints_[safepoints_.length() - 1]->id() < ins->id());
    if (!ins->isCall() && !nonCallSafepoints_.append(ins))
        return false;
    return safepoints_.append(ins);
}

Label *
LBlock::label()
{
    return begin()->toLabel()->label();
}

uint32
LBlock::firstId()
{
    if (phis_.length()) {
        return phis_[0]->id();
    } else {
        for (LInstructionIterator i(instructions_.begin()); i != instructions_.end(); i++) {
            if (i->id())
                return i->id();
        }
    }
    return 0;
}
uint32
LBlock::lastId()
{
    LInstruction *last = *instructions_.rbegin();
    JS_ASSERT(last->id());
    if (last->numDefs())
        return last->getDef(last->numDefs() - 1)->virtualRegister();
    return last->id();
}

LMoveGroup *
LBlock::getEntryMoveGroup()
{
    if (entryMoveGroup_)
        return entryMoveGroup_;
    entryMoveGroup_ = new LMoveGroup;
    JS_ASSERT(begin()->isLabel());
    insertAfter(*begin(), entryMoveGroup_);
    return entryMoveGroup_;
}

LMoveGroup *
LBlock::getExitMoveGroup()
{
    if (exitMoveGroup_)
        return exitMoveGroup_;
    exitMoveGroup_ = new LMoveGroup;
    insertBefore(*rbegin(), exitMoveGroup_);
    return exitMoveGroup_;
}

static size_t
TotalOperandCount(MResumePoint *mir)
{
    size_t accum = mir->numOperands();
    while ((mir = mir->caller()))
        accum += mir->numOperands();
    return accum;
}

LSnapshot::LSnapshot(MResumePoint *mir, BailoutKind kind)
  : numSlots_(TotalOperandCount(mir) * BOX_PIECES),
    slots_(NULL),
    mir_(mir),
    snapshotOffset_(INVALID_SNAPSHOT_OFFSET),
    bailoutId_(INVALID_BAILOUT_ID),
    bailoutKind_(kind)
{ }

bool
LSnapshot::init(MIRGenerator *gen)
{
    slots_ = gen->allocate<LAllocation>(numSlots_);
    return !!slots_;
}

LSnapshot *
LSnapshot::New(MIRGenerator *gen, MResumePoint *mir, BailoutKind kind)
{
    LSnapshot *snapshot = new LSnapshot(mir, kind);
    if (!snapshot->init(gen))
        return NULL;

    IonSpew(IonSpew_Snapshots, "Generating LIR snapshot %p from MIR (%p)",
            (void *)snapshot, (void *)mir);

    return snapshot;
}

bool
LPhi::init(MIRGenerator *gen)
{
    inputs_ = gen->allocate<LAllocation>(numInputs_);
    return !!inputs_;
}

LPhi::LPhi(MPhi *mir)
  : numInputs_(mir->numOperands())
{
}

LPhi *
LPhi::New(MIRGenerator *gen, MPhi *ins)
{
    LPhi *phi = new LPhi(ins);
    if (!phi->init(gen))
        return NULL;
    return phi;
}

void
LInstruction::printName(FILE *fp, Opcode op)
{
    static const char *names[] =
    {
#define LIROP(x) #x,
        LIR_OPCODE_LIST(LIROP)
#undef LIROP
    };
    const char *name = names[op];
    size_t len = strlen(name);
    for (size_t i = 0; i < len; i++)
        fprintf(fp, "%c", tolower(name[i]));
}

void
LInstruction::printName(FILE *fp)
{
    printName(fp, op());
}

static const char *TypeChars[] =
{
    "i",            // INTEGER
    "o",            // OBJECT
    "f",            // DOUBLE
#ifdef JS_NUNBOX32
    "t",            // TYPE
    "d"             // PAYLOAD
#elif JS_PUNBOX64
    "x"             // BOX
#endif
};

static void
PrintDefinition(FILE *fp, const LDefinition &def)
{
    fprintf(fp, "[%s", TypeChars[def.type()]);
    if (def.virtualRegister())
        fprintf(fp, ":%d", def.virtualRegister());
    if (def.policy() == LDefinition::PRESET) {
        fprintf(fp, " (");
        LAllocation::PrintAllocation(fp, def.output());
        fprintf(fp, ")");
    } else if (def.policy() == LDefinition::MUST_REUSE_INPUT) {
        fprintf(fp, " (!)");
    } else if (def.policy() == LDefinition::PASSTHROUGH) {
        fprintf(fp, " (-)");
    }
    fprintf(fp, "]");
}

static void
PrintUse(FILE *fp, const LUse *use)
{
    fprintf(fp, "v%d:", use->virtualRegister());
    if (use->policy() == LUse::ANY) {
        fprintf(fp, "*");
    } else if (use->policy() == LUse::REGISTER) {
        fprintf(fp, "r");
    } else {
        // Unfortunately, we don't know here whether the virtual register is a
        // float or a double. Should we steal a bit in LUse for help? For now,
        // nothing defines any fixed xmm registers.
        fprintf(fp, "%s", Registers::GetName(Registers::Code(use->registerCode())));
    }
}

void
LAllocation::PrintAllocation(FILE *fp, const LAllocation *a)
{
    switch (a->kind()) {
      case LAllocation::CONSTANT_VALUE:
      case LAllocation::CONSTANT_INDEX:
        fprintf(fp, "c");
        break;
      case LAllocation::GPR:
        fprintf(fp, "=%s", a->toGeneralReg()->reg().name());
        break;
      case LAllocation::FPU:
        fprintf(fp, "=%s", a->toFloatReg()->reg().name());
        break;
      case LAllocation::STACK_SLOT:
        fprintf(fp, "stack:i%d", a->toStackSlot()->slot());
        break;
      case LAllocation::DOUBLE_SLOT:
        fprintf(fp, "stack:d%d", a->toStackSlot()->slot());
        break;
      case LAllocation::ARGUMENT:
        fprintf(fp, "arg:%d", a->toArgument()->index());
        break;
      case LAllocation::USE:
        PrintUse(fp, a->toUse());
        break;
      default:
        JS_NOT_REACHED("what?");
        break;
    }
}

void
LInstruction::printOperands(FILE *fp)
{
    for (size_t i = 0; i < numOperands(); i++) {
        fprintf(fp, " (");
        LAllocation::PrintAllocation(fp, getOperand(i));
        fprintf(fp, ")");
        if (i != numOperands() - 1)
            fprintf(fp, ",");
    }
}

void
LInstruction::assignSnapshot(LSnapshot *snapshot)
{
    JS_ASSERT(!snapshot_);
    snapshot_ = snapshot;

#ifdef DEBUG
    if (IonSpewEnabled(IonSpew_Snapshots)) {
        IonSpewHeader(IonSpew_Snapshots);
        fprintf(IonSpewFile, "Assigning snapshot %p to instruction %p (",
                (void *)snapshot, (void *)this);
        printName(IonSpewFile);
        fprintf(IonSpewFile, ")\n");
    }
#endif
}

void
LInstruction::print(FILE *fp)
{
    printName(fp);

    fprintf(fp, " (");
    for (size_t i = 0; i < numDefs(); i++) {
        PrintDefinition(fp, *getDef(i));
        if (i != numDefs() - 1)
            fprintf(fp, ", ");
    }
    fprintf(fp, ")");

    printInfo(fp);

    if (numTemps()) {
        fprintf(fp, " t=(");
        for (size_t i = 0; i < numTemps(); i++) {
            PrintDefinition(fp, *getTemp(i));
            if (i != numTemps() - 1)
                fprintf(fp, ", ");
        }
        fprintf(fp, ")");
    }
}

void
LInstruction::initSafepoint()
{
    JS_ASSERT(!safepoint_);
    safepoint_ = new LSafepoint();
    JS_ASSERT(safepoint_);
}

void
LMoveGroup::printOperands(FILE *fp)
{
    for (size_t i = 0; i < numMoves(); i++) {
        const LMove &move = getMove(i);
        fprintf(fp, "[");
        LAllocation::PrintAllocation(fp, move.from());
        fprintf(fp, " -> ");
        LAllocation::PrintAllocation(fp, move.to());
        fprintf(fp, "]");
        if (i != numMoves() - 1)
            fprintf(fp, ", ");
    }
}

Label *
LTestVAndBranch::ifTrue()
{
    return ifTrue_->lir()->label();
}

Label *
LTestVAndBranch::ifFalse()
{
    return ifFalse_->lir()->label();
}

