/*******************************************************************************
 HARPtools by Chad D. Kersey, Summer 2011
*******************************************************************************/

#include <iostream>
#include  <iomanip>

// #define USE_DEBUG 7
// #define PRINT_ACTIVE_THREADS

#include "include/types.h"
#include "include/util.h"
#include "include/archdef.h"
#include "include/mem.h"
#include "include/enc.h"
#include "include/core.h"
#include "include/debug.h"

#ifdef EMU_INSTRUMENTATION
#include "include/qsim-harp.h"
#endif

using namespace Harp;
using namespace std;

#ifdef EMU_INSTRUMENTATION
void Harp::reg_doRead(Word cpuId, Word regNum) {
  Harp::OSDomain::osDomain->do_reg(cpuId, regNum, 8, true);
}

void Harp::reg_doWrite(Word cpuId, Word regNum) {
  Harp::OSDomain::osDomain->do_reg(cpuId, regNum, 8, false);
}
#endif

Core::Core(const ArchDef &a, Decoder &d, MemoryUnit &mem, Word id):
  a(a), iDec(d), mem(mem), steps(0)
{

  cache_simulator = new Vcache_simX;
  for (unsigned i = 0; i < a.getNWarps(); ++i)
    w.push_back(Warp(this, i));

  w[0].activeThreads = 1;
  w[0].spawned = true;
}

bool Core::interrupt(Word r0) {
  w[0].interrupt(r0);
}

void Core::step()
{
    this->fetch();
}

void Core::fetch()
{
  ++steps;

  #ifdef PRINT_ACTIVE_THREADS
  cout << endl << "Threads:";
  #endif


  for (unsigned i = 0; i < w.size(); ++i) {
    if (w[i].activeThreads) {

      trace_inst_t trace_inst;
      trace_inst.valid_inst         = false;
      trace_inst.pc                 = 0;
      trace_inst.wid                = i;
      trace_inst.rs1                = -1;
      trace_inst.rs2                = -1;
      trace_inst.rd                 = -1;
      trace_inst.is_lw              = false;
      trace_inst.is_sw              = false;
      trace_inst.mem_addresses      = new unsigned[a.getNThds()];
      trace_inst.mem_stall_cycles   = 0;
      trace_inst.fetch_stall_cycles = 0;
      trace_inst.stall_warp         = false;


      D(3, "Core step stepping warp " << i << '[' << w[i].activeThreads << ']');
      w[i].step(&trace_inst);
      D(3, "Now " << w[i].activeThreads << " active threads in " << i);
    
      D(-1, "********************************");
      D(-1, "*** valid: " << trace_inst.valid_inst << " pc: " << hex << trace_inst.pc << dec << " rs1..rs2..rd " << trace_inst.rs1 << ".." << trace_inst.rs2 << ".." << trace_inst.rd << "\n");
      D(-1, "********************************");

    }
   
    #ifdef PRINT_ACTIVE_THREADS
    for (unsigned j = 0; j < w[i].tmask.size(); ++j) {
      if (w[i].activeThreads > j && w[i].tmask[j]) cout << " 1";
      else cout << " 0";
      if (j != w[i].tmask.size()-1 || i != w.size()-1) cout << ',';
    }
    #endif
  }
  #ifdef PRINT_ACTIVE_THREADS
  cout << endl;
  #endif
}

void Core::decode()
{

}

void Core::scheduler()
{

}

void Core::gpr_read()
{

}

void Core::execute_unit()
{

}

void Core::load_store()
{

}

bool Core::running() const {
  for (unsigned i = 0; i < w.size(); ++i)
    if (w[i].running()) return true;
  return false;
}

void Core::printStats() const {
  unsigned long insts = 0;
  for (unsigned i = 0; i < w.size(); ++i)
    insts += w[i].insts;

  cout << "Total steps: " << steps << endl;
  cout << "Total insts: " << insts << endl;
  for (unsigned i = 0; i < w.size(); ++i) {
    cout << "=== Warp " << i << " ===" << endl;
    w[i].printStats();
  }
}

Warp::Warp(Core *c, Word id) : 
  core(c), pc(0x80000000), interruptEnable(true),
  supervisorMode(true), activeThreads(0), reg(0), pred(0),
  shadowReg(core->a.getNRegs()), shadowPReg(core->a.getNPRegs()), id(id),
  spawned(false), steps(0), insts(0), loads(0), stores(0)
{
  D(3, "Creating a new thread with PC: " << hex << this->pc << '\n');
  /* Build the register file. */
  Word regNum(0);
  for (Word j = 0; j < core->a.getNThds(); ++j) {
    reg.push_back(vector<Reg<Word> >(0));
    for (Word i = 0; i < core->a.getNRegs(); ++i) {
      reg[j].push_back(Reg<Word>(id, regNum++));
    }

    pred.push_back(vector<Reg<bool> >(0));
    for (Word i = 0; i < core->a.getNPRegs(); ++i) {
      pred[j].push_back(Reg<bool>(id, regNum++));
    }

    bool act = false;
    if (j == 0) act = true;
    tmask.push_back(act);
    shadowTmask.push_back(act);
  }

  Word csrNum(0);
  for (Word i = 0; i < (1<<12); i++)
  {
    csr.push_back(Reg<uint16_t>(id, regNum++));
  }

  /* Set initial register contents. */
  reg[0][0] = (core->a.getNThds()<<(core->a.getWordSize()*8 / 2)) | id;
}

void Warp::step(trace_inst_t * trace_inst) {
  Size fetchPos(0), decPos, wordSize(core->a.getWordSize());
  vector<Byte> fetchBuffer(wordSize);

  if (activeThreads == 0) return;

  ++steps;
  
  D(3, "in step pc=0x" << hex << pc);

  // std::cout << "pc: " << hex << pc << "\n";

  trace_inst->pc = pc;

  /* Fetch and decode. */
  if (wordSize < sizeof(pc)) pc &= ((1ll<<(wordSize*8))-1);
  Instruction *inst;
  bool fetchMore;

  fetchMore = false;
  unsigned fetchSize(wordSize - (pc+fetchPos)%wordSize);
  fetchBuffer.resize(fetchPos + fetchSize);
  Word fetched = core->mem.fetch(pc + fetchPos, supervisorMode);
  writeWord(fetchBuffer, fetchPos, fetchSize, fetched);
  decPos = 0;
  inst = core->iDec.decode(fetchBuffer, decPos, trace_inst);

  D(3, "Fetched at 0x" << hex << pc);
  D(3, "0x" << hex << pc << ": " << *inst);

  // Update pc
  pc += decPos;

  // Execute

  inst->executeOn(*this);

 
  // At Debug Level 3, print debug info after each instruction.
  #ifdef USE_DEBUG
    if (USE_DEBUG >= 3) {
      D(3, "Register state:");
      for (unsigned i = 0; i < reg[0].size(); ++i) {
        D_RAW("  %r" << setfill(' ') << setw(2) << dec << i << ':');
        for (unsigned j = 0; j < reg.size(); ++j) 
          D_RAW(' ' << setfill('0') << setw(8) << hex << reg[j][i] << setfill(' ') << ' ');
        D_RAW('(' << shadowReg[i] << ')' << endl);
      }


      D(3, "Thread mask:");
      D_RAW("  ");
      for (unsigned i = 0; i < tmask.size(); ++i) D_RAW(tmask[i] << ' ');
      D_RAW(endl);
      D_RAW(endl);
      D_RAW(endl);
    }
  #endif

  // Clean up.
  delete inst;
}

bool Warp::interrupt(Word r0) {
  if (!interruptEnable) return false;

#ifdef EMU_INSTRUMENTATION
  Harp::OSDomain::osDomain->do_int(0, r0);
#endif

  shadowActiveThreads = activeThreads;
  shadowTmask = tmask;
  shadowInterruptEnable = interruptEnable; /* For traps. */
  shadowSupervisorMode = supervisorMode;
  
  for (Word i = 0; i < reg[0].size(); ++i) shadowReg[i] = reg[0][i];
  for (Word i = 0; i < pred[0].size(); ++i) shadowPReg[i] = pred[0][i];
  for (Word i = 0; i < reg.size(); ++i) tmask[i] = 1;

  shadowPc = pc;
  activeThreads = 1;
  interruptEnable = false;
  supervisorMode = true;
  reg[0][0] = r0;
  pc = core->interruptEntry;

  return true;
}

void Warp::printStats() const {
  cerr << "Steps : " << steps << endl
       << "Insts : " << insts << endl
       << "Loads : " << loads << endl
       << "Stores: " << stores << endl;

  unsigned const grade = reg[0][28];

  if (grade == 1) cerr << "GRADE: PASSED\n";
  else              cerr << "GRADE: FAILED "  << (grade >> 1) << "\n";
}