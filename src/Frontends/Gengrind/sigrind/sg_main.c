
/*--------------------------------------------------------------------*/
/*--- Callgrind                                                    ---*/
/*---                                                       main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Callgrind, a Valgrind tool for call graph
   profiling programs.

   Copyright (C) 2002-2015, Josef Weidendorfer (Josef.Weidendorfer@gmx.de)

   This tool is derived from and contains code from Cachegrind
   Copyright (C) 2002-2015 Nicholas Nethercote (njn@valgrind.org)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

// TODO(cleanup) leftover Callgrind functionality


#include "config.h"

#include "global.h"
#include "callgrind.h"

#include "sigil2_ipc.h"
#include "log_events.h"
#include "Core/PrimitiveEnums.h"

#include "coregrind/pub_core_libcfile.h"
#include "coregrind/pub_core_clientstate.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_transtab.h"       // VG_(discard_translations_safely)


/*------------------------------------------------------------*/
/*--- Instrumentation structures and event queue handling  ---*/
/*------------------------------------------------------------*/

/* Maintain an ordered list of memory events which are outstanding, in
   the sense that no IR has yet been generated to do the relevant
   helper calls.  The BB is scanned top to bottom and memory events
   are added to the end of the list, merging with the most recent
   notified event where possible (Dw immediately following Dr and
   having the same size and EA can be merged).

   This merging is done so that for architectures which have
   load-op-store instructions (x86, amd64), the insn is treated as if
   it makes just one memory reference (a modify), rather than two (a
   read followed by a write at the same address).

   At various points the list will need to be flushed, that is, IR
   generated from it.  That must happen before any possible exit from
   the block (the end, or an IRStmt_Exit).  Flushing also takes place
   when there is no space to add a new event.

   If we require the simulation statistics to be up to date with
   respect to possible memory exceptions, then the list would have to
   be flushed before each memory reference.  That would however lose
   performance by inhibiting event-merging during flushing.

   Flushing the list consists of walking it start to end and emitting
   instrumentation IR for each event, in the order in which they
   appear.  It may be possible to emit a single call for two adjacent
   events in order to reduce the number of helper function calls made.
   For example, it could well be profitable to handle two adjacent Ir
   events with a single helper call.  */

typedef IRExpr IRAtom;

typedef enum _EventTag EventTag;
enum _EventTag {
      Ev_Ir,  // Instruction read
      Ev_Dr,  // Data read
      Ev_Dw,  // Data write
      Ev_Dm,  // Data modify (read then write)
      Ev_Bc,  // branch conditional
      Ev_Bi,  // branch indirect (to unknown destination)
      Ev_G,   // Global bus event
      Ev_Comp // Compute event
};

typedef struct _Event Event;
struct _Event {
   EventTag   tag;
   InstrInfo* inode;
   union 
   {
      struct {
      } Ir;
      struct {
         IRAtom* ea;
         Int     szB;
      } Dr;
      struct {
         IRAtom* ea;
         Int     szB;
      } Dw;
      struct 
      {
         IRAtom* ea;
         Int     szB;
      } Dm;
      struct {
         IRAtom* taken; /* :: Ity_I1 */
      } Bc;
      struct {
         IRAtom* dst;
      } Bi;
      struct {
      } G;
      struct {
         IRExprTag arity;
         IRType op_type;
      } Comp;
   } Ev;
};

/* Up to this many unnotified events are allowed.  Number is
   arbitrary.  Larger numbers allow more event merging to occur, but
   potentially induce more spilling due to extending live ranges of
   address temporaries. */
#define N_EVENTS 16

/* A struct which holds all the running state during instrumentation.
   Mostly to avoid passing loads of parameters everywhere. */
typedef struct {
    /* The current outstanding-memory-event list. */
    Event events[N_EVENTS];
    Int   events_used;

    /* The array of InstrInfo's is part of BB struct. */
    BB* bb;

    /* BB seen before (ie. re-instrumentation) */
    Bool seen_before;

    /* Number InstrInfo bins 'used' so far. */
    UInt ii_index;

    // current offset of guest instructions from BB start
    UInt instr_offset;

    /* The output SB being constructed. */
    IRSB* sbOut;
} ClgState;

/*------------------------------------------------------------*/
/*--- Global variables                                     ---*/
/*------------------------------------------------------------*/

Bool SGL_(is_in_event_collect_func);
SglCommandLineOptions SGL_(clo);

/* for all threads */
CommandLineOptions CLG_(clo);

Statistics CLG_(stat);
Bool CLG_(instrument_state) = True; /* Instrumentation on ? */

/* thread and signal handler specific */
exec_state CLG_(current_state);

/*------------------------------------------------------------*/
/*--- Local declarations                                   ---*/
/*------------------------------------------------------------*/

static void flushEvents ( ClgState* clgs );
static void addEvent_Comp( ClgState* clgs, InstrInfo* inode, IRExprTag arity, IRType op_type );
static void addEvent_G ( ClgState* clgs, InstrInfo* inode );
static void addEvent_Bi ( ClgState* clgs, InstrInfo* inode, IRAtom* whereTo );
static void addEvent_Bc ( ClgState* clgs, InstrInfo* inode, IRAtom* guard );
static void addEvent_D_guarded ( ClgState* clgs, InstrInfo* inode,
                          Int datasize, IRAtom* ea, IRAtom* guard,
                          Bool isWrite );
static void addEvent_Dw ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea );
static void addEvent_Dr ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea );
static void addEvent_Ir ( ClgState* clgs, InstrInfo* inode );
static InstrInfo* next_InstrInfo ( ClgState* clgs, Addr instr_addr, UInt instr_size );

/* Inititalization functions */
static void init_Event ( Event* ev );
/* Helper functions */
static IRAtom* get_Event_dea ( Event* ev );
static Int get_Event_dszB ( Event* ev );
static void showEvent ( Event* ev );
static Addr IRConst2Addr(IRConst* con);
static void addConstMemStoreStmt( IRSB* bbOut, UWord addr, UInt val, IRType hWordTy);



/*------------------------------------------------------------*/
/*--- Instrumentation                                      ---*/
/*------------------------------------------------------------*/

/* add helper call to setup_bbcc, with pointer to BB struct as argument
 *
 * precondition for setup_bbcc:
 * - jmps_passed has number of cond.jumps passed in last executed BB
 * - current_bbcc has a pointer to the BBCC of the last executed BB
 *   Thus, if bbcc_jmpkind is != -1 (JmpNone),
 *     current_bbcc->bb->jmp_addr
 *   gives the address of the jump source.
 *
 * the setup does 1 thing:
 * - trace call:
 *   * Unwind own call stack, i.e sync our ESP with real ESP
 *     This is for ESP manipulation (longjmps, C++ exec handling) and RET
 *   * For CALLs or JMPs crossing objects, record call arg +
 *     push are on own call stack
 */
static void addBBSetupCall(ClgState* clgs)
{
   IRDirty* di;
   IRExpr  *arg1, **argv;

   arg1 = mkIRExpr_HWord( (HWord)clgs->bb );
   argv = mkIRExprVec_1(arg1);
   di = unsafeIRDirty_0_N( 1, "setup_bbcc",
			      VG_(fnptr_to_fnentry)( & CLG_(setup_bbcc) ),
			      argv);
   addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
}

static IRSB* CLG_(instrument)( VgCallbackClosure* closure,
                        IRSB* sbIn,
			const VexGuestLayout* layout,
			const VexGuestExtents* vge,
                        const VexArchInfo* archinfo_host,
			IRType gWordTy, IRType hWordTy )
{
   Int        i;
   IRStmt*    st;
   Addr       origAddr;
   InstrInfo* curr_inode = NULL;
   ClgState   clgs;
   UInt       cJumps = 0;
   IRTypeEnv* tyenv = sbIn->tyenv;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   // No instrumentation if it is switched off
   if (! CLG_(instrument_state)) {
       CLG_DEBUG(5, "instrument(BB %#lx) [Instrumentation OFF]\n",
		 (Addr)closure->readdr);
       return sbIn;
   }

   CLG_DEBUG(3, "+ instrument(BB %#lx)\n", (Addr)closure->readdr);

   /* Set up SB for instrumented IR */
   clgs.sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( clgs.sbOut, sbIn->stmts[i] );
      i++;
   }

   // Get the first statement, and origAddr from it
   CLG_ASSERT(sbIn->stmts_used >0);
   CLG_ASSERT(i < sbIn->stmts_used);
   st = sbIn->stmts[i];
   CLG_ASSERT(Ist_IMark == st->tag);

   origAddr = st->Ist.IMark.addr + st->Ist.IMark.delta;
   CLG_ASSERT(origAddr == st->Ist.IMark.addr 
                          + st->Ist.IMark.delta);  // XXX: check no overflow

   /* Get BB struct (creating if necessary).
    * JS: The hash table is keyed with orig_addr_noredir -- important!
    * JW: Why? If it is because of different chasing of the redirection,
    *     this is not needed, as chasing is switched off in callgrind
    */
   clgs.bb = CLG_(get_bb)(origAddr, sbIn, &(clgs.seen_before));

   addBBSetupCall(&clgs);

   // Set up running state
   clgs.events_used = 0;
   clgs.ii_index = 0;
   clgs.instr_offset = 0;

   for (/*use current i*/; i < sbIn->stmts_used; i++) 
   {
      st = sbIn->stmts[i];
      CLG_ASSERT(isFlatIRStmt(st));

      if (st->tag == Ist_IMark)
      {
         Addr   cia   = st->Ist.IMark.addr + st->Ist.IMark.delta;
         UInt   isize = st->Ist.IMark.len;
         CLG_ASSERT(clgs.instr_offset == cia - origAddr);
         // If Vex fails to decode an instruction, the size will be zero.
         // Pretend otherwise.
         if (isize == 0) isize = VG_MIN_INSTR_SZB;
         // Sanity-check size.
         tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
            || VG_CLREQ_SZB == isize );

         // Init the inode, record it as the current one.
         // Subsequent Dr/Dw/Dm events from the same instruction will
         // also use it.
         curr_inode = next_InstrInfo (&clgs, cia, isize);
      }

      switch (st->tag) 
      {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
             break;
         case Ist_IMark: 
         {
            if (SGL_(clo).gen_instr == True)
               addEvent_Ir( &clgs, curr_inode );
            break;
         }
         case Ist_WrTmp: 
         {
            IRExpr* data = st->Ist.WrTmp.data;
            switch (data->tag)
            {
               case Iex_Load:
               {
                  IRExpr* aexpr = data->Iex.Load.addr;
                  // Note also, endianness info is ignored.  I guess
                  // that's not interesting.
                  if (SGL_(clo).gen_mem == True)
                     addEvent_Dr( &clgs, curr_inode, sizeofIRType(data->Iex.Load.ty), aexpr );
                  break;
               }
               case Iex_Unop:
               case Iex_Binop:
               case Iex_Triop:
               case Iex_Qop:
                  if (SGL_(clo).gen_comp == True)
                  {
                     IRType op_type = typeOfIRExpr(sbIn->tyenv, data);
                     /* SIMD and decimal floating point ops are unsupported.
                      * See VEX/pub/libvex_ir.h */
                     if (op_type < Ity_D32 || op_type == Ity_F128)
                        addEvent_Comp( &clgs, curr_inode, data->tag, op_type );
                  }
                  break;
               default:
                  /*don't care*/
                  break;
            }
            break;
         }
         case Ist_Store: 
         {
            IRExpr* data  = st->Ist.Store.data;
            IRExpr* aexpr = st->Ist.Store.addr;
            if (SGL_(clo).gen_mem == True)
               addEvent_Dw( &clgs, curr_inode, sizeofIRType(typeOfIRExpr(sbIn->tyenv, data)), aexpr );
            break;
         }
         case Ist_StoreG: 
         {
            IRStoreG* sg   = st->Ist.StoreG.details;
            IRExpr*   data = sg->data;
            IRExpr*   addr = sg->addr;
            IRType    type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
            if (SGL_(clo).gen_mem == True) {
               addEvent_D_guarded( &clgs, curr_inode,
                                   sizeofIRType(type),
                                   addr, sg->guard, True/*isWrite*/ );
            }
            break;
         }
         case Ist_LoadG: 
         {
            IRLoadG* lg       = st->Ist.LoadG.details;
            IRType   type     = Ity_INVALID; /* loaded type */
            IRType   typeWide = Ity_INVALID; /* after implicit widening */
            IRExpr*  addr     = lg->addr;
            typeOfIRLoadGOp(lg->cvt, &typeWide, &type);
            tl_assert(type != Ity_INVALID);
            if (SGL_(clo).gen_mem == True) {
               addEvent_D_guarded( &clgs, curr_inode,
                                   sizeofIRType(type), addr, lg->guard,
                                   False/*!isWrite*/ );
            }
            break;
         }
         case Ist_Dirty: 
         {
            Int      dataSize;
            IRDirty* d = st->Ist.Dirty.details;
            if (d->mFx != Ifx_None) 
            {
               /* This dirty helper accesses memory.  Collect the details. */
               tl_assert(d->mAddr != NULL);
               tl_assert(d->mSize != 0);
               dataSize = d->mSize;

               if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
               {
                  if (SGL_(clo).gen_mem == True)
                     addEvent_Dr( &clgs, curr_inode, dataSize, d->mAddr );
               }
               if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
               {
                  if (SGL_(clo).gen_mem == True)
                     addEvent_Dw( &clgs, curr_inode, dataSize, d->mAddr );
               }
            }
            else 
            {
               tl_assert(d->mAddr == NULL);
               tl_assert(d->mSize == 0);
            }
            break;
         }
         case Ist_CAS: 
         {
            /* We treat it as a read and a write of the location.  I
               think that is the same behaviour as it was before IRCAS
               was introduced, since prior to that point, the Vex
               front ends would translate a lock-prefixed instruction
               into a (normal) read followed by a (normal) write. */
            Int    dataSize;
            IRCAS* cas = st->Ist.CAS.details;
            CLG_ASSERT(cas->addr && isIRAtom(cas->addr));
            CLG_ASSERT(cas->dataLo);
            dataSize = sizeofIRType(typeOfIRExpr(sbIn->tyenv, cas->dataLo));
            if (cas->dataHi != NULL)
               dataSize *= 2; /* since this is a doubleword-cas */
            if (SGL_(clo).gen_mem == True) {
               addEvent_Dr( &clgs, curr_inode, dataSize, cas->addr );
               addEvent_Dw( &clgs, curr_inode, dataSize, cas->addr );
            }
            addEvent_G(  &clgs, curr_inode );
            break;
         }
         case Ist_LLSC:
            if (st->Ist.LLSC.storedata == NULL) {
               /* LL */
               IRType dataTy = typeOfIRTemp(sbIn->tyenv, st->Ist.LLSC.result);
               if (SGL_(clo).gen_mem == True)
                  addEvent_Dr( &clgs, curr_inode, sizeofIRType(dataTy), st->Ist.LLSC.addr );
               /* flush events before LL, should help SC to succeed */
               flushEvents( &clgs );
            } else {
               /* SC */
               IRType dataTy = typeOfIRExpr(sbIn->tyenv, st->Ist.LLSC.storedata);
               if (SGL_(clo).gen_mem == True)
                  addEvent_Dw( &clgs, curr_inode, sizeofIRType(dataTy), st->Ist.LLSC.addr );
               /* I don't know whether the global-bus-lock cost should
                  be attributed to the LL or the SC, but it doesn't
                  really matter since they always have to be used in
                  pairs anyway.  Hence put it (quite arbitrarily) on
                  the SC. */
               addEvent_G(  &clgs, curr_inode );
            }
            break;
         case Ist_Exit: {
            Bool guest_exit, inverted;

            /* VEX code generation sometimes inverts conditional branches.
             * As Callgrind counts (conditional) jumps, it has to correct
             * inversions. The heuristic is the following:
             * (1) Callgrind switches off SB chasing and unrolling, and
             *     therefore it assumes that a candidate for inversion only is
             *     the last conditional branch in an SB.
             * (2) inversion is assumed if the branch jumps to the address of
             *     the next guest instruction in memory.
             * This heuristic is precalculated in CLG_(collectBlockInfo)().
             *
             * Branching behavior is also used for branch prediction. Note that
             * above heuristic is different from what Cachegrind does.
             * Cachegrind uses (2) for all branches.
             */
            if (cJumps+1 == clgs.bb->cjmp_count)
                inverted = clgs.bb->cjmp_inverted;
            else
                inverted = False;

            // call branch predictor only if this is a branch in guest code
            guest_exit = (st->Ist.Exit.jk == Ijk_Boring) ||
                         (st->Ist.Exit.jk == Ijk_Call) ||
                         (st->Ist.Exit.jk == Ijk_Ret);

            if (guest_exit) {
                /* Stuff to widen the guard expression to a host word, so
                   we can pass it to the branch predictor simulation
                   functions easily. */
                IRType   tyW    = hWordTy;
                IROp     widen  = tyW==Ity_I32  ? Iop_1Uto32  : Iop_1Uto64;
                IROp     opXOR  = tyW==Ity_I32  ? Iop_Xor32   : Iop_Xor64;
                IRTemp   guard1 = newIRTemp(clgs.sbOut->tyenv, Ity_I1);
                IRTemp   guardW = newIRTemp(clgs.sbOut->tyenv, tyW);
                IRTemp   guard  = newIRTemp(clgs.sbOut->tyenv, tyW);
                IRExpr*  one    = tyW==Ity_I32 ? IRExpr_Const(IRConst_U32(1))
                                               : IRExpr_Const(IRConst_U64(1));

                /* Widen the guard expression. */
                addStmtToIRSB( clgs.sbOut,
                               IRStmt_WrTmp( guard1, st->Ist.Exit.guard ));
                addStmtToIRSB( clgs.sbOut,
                               IRStmt_WrTmp( guardW,
                                             IRExpr_Unop(widen,
                                                         IRExpr_RdTmp(guard1))) );
                /* If the exit is inverted, invert the sense of the guard. */
                addStmtToIRSB(
                        clgs.sbOut,
                        IRStmt_WrTmp(
                                guard,
                                inverted ? IRExpr_Binop(opXOR, IRExpr_RdTmp(guardW), one)
                                    : IRExpr_RdTmp(guardW)
                                    ));
                /* And post the event. */
                addEvent_Bc( &clgs, curr_inode, IRExpr_RdTmp(guard) );
            }

            /* We may never reach the next statement, so need to flush
               all outstanding transactions now. */
            flushEvents( &clgs );

            CLG_ASSERT(clgs.ii_index>0);
            if (!clgs.seen_before) {
               ClgJumpKind jk;

               if (st->Ist.Exit.jk == Ijk_Call) jk = jk_Call;
               else if (st->Ist.Exit.jk == Ijk_Ret) jk = jk_Return;
               else {
                 if (IRConst2Addr(st->Ist.Exit.dst) == origAddr + curr_inode->instr_offset + curr_inode->instr_size)
                    jk = jk_None;
                 else
                    jk = jk_Jump;
               }

               clgs.bb->jmp[cJumps].instr = clgs.ii_index-1;
               clgs.bb->jmp[cJumps].jmpkind = jk;
            }

            /* Update global variable jmps_passed before the jump
             * A correction is needed if VEX inverted the last jump condition
             */
            UInt val = inverted ? cJumps+1 : cJumps;
            addConstMemStoreStmt( clgs.sbOut,
                (UWord) &CLG_(current_state).jmps_passed,
                val, hWordTy);

            cJumps++;

            break;
         }
         default:
            tl_assert(0);
            break;
      } //end switch


      /* Copy the original statement */
      addStmtToIRSB( clgs.sbOut, st );

      CLG_DEBUGIF(5) 
      {
         VG_(printf)("   pass  ");
	      ppIRStmt(st);
	      VG_(printf)("\n");
      }
   } //end foreach statement

   /* Deal with branches to unknown destinations.  Except ignore ones
      which are function returns as we assume the return stack
      predictor never mispredicts. */
   if ((sbIn->jumpkind == Ijk_Boring) || (sbIn->jumpkind == Ijk_Call)) {
      if (0) { ppIRExpr( sbIn->next ); VG_(printf)("\n"); }
      switch (sbIn->next->tag) {
         case Iex_Const:
            break; /* boring - branch to known address */
         case Iex_RdTmp:
            /* looks like an indirect branch (branch to unknown) */
            addEvent_Bi( &clgs, curr_inode, sbIn->next );
            break;
         default:
            /* shouldn't happen - if the incoming IR is properly
               flattened, should only have tmp and const cases to
               consider. */
            tl_assert(0);
      }
   }

   /* At the end of the bb.  Flush outstandings. */
   flushEvents( &clgs );

   /* Update global variable jmps_passed at end of SB.
    * As CLG_(current_state).jmps_passed is reset to 0 in setup_bbcc,
    * this can be omitted if there is no conditional jump in this SB.
    * A correction is needed if VEX inverted the last jump condition
    */
   if (cJumps>0) {
      UInt jmps_passed = cJumps;
      if (clgs.bb->cjmp_inverted) jmps_passed--;
      addConstMemStoreStmt( clgs.sbOut,
			    (UWord) &CLG_(current_state).jmps_passed,
			    jmps_passed, hWordTy);
   }
   CLG_ASSERT(clgs.bb->cjmp_count == cJumps);
   CLG_ASSERT(clgs.bb->instr_count == clgs.ii_index);

   /* Info for final exit from BB */
   {
     ClgJumpKind jk;

     if      (sbIn->jumpkind == Ijk_Call) jk = jk_Call;
     else if (sbIn->jumpkind == Ijk_Ret)  jk = jk_Return;
     else {
       jk = jk_Jump;
       if ((sbIn->next->tag == Iex_Const) &&
	   (IRConst2Addr(sbIn->next->Iex.Const.con) ==
	    origAddr + clgs.instr_offset))
	 jk = jk_None;
     }
     clgs.bb->jmp[cJumps].jmpkind = jk;
     /* Instruction index of the call/ret at BB end
      * (it is wrong for fall-through, but does not matter) */
     clgs.bb->jmp[cJumps].instr = clgs.ii_index-1;
   }

   /* swap information of last exit with final exit if inverted */
   if (clgs.bb->cjmp_inverted) {
     ClgJumpKind jk;
     UInt instr;

     jk = clgs.bb->jmp[cJumps].jmpkind;
     clgs.bb->jmp[cJumps].jmpkind = clgs.bb->jmp[cJumps-1].jmpkind;
     clgs.bb->jmp[cJumps-1].jmpkind = jk;
     instr = clgs.bb->jmp[cJumps].instr;
     clgs.bb->jmp[cJumps].instr = clgs.bb->jmp[cJumps-1].instr;
     clgs.bb->jmp[cJumps-1].instr = instr;
   }

   if (clgs.seen_before) {
       CLG_ASSERT(clgs.bb->instr_len == clgs.instr_offset);
   }
   else {
       clgs.bb->instr_len = clgs.instr_offset;
   }

   if (cJumps>0) {
       CLG_DEBUG(3, "                     [ ");
       for (i=0;(UInt)i<cJumps;i++)
	   CLG_DEBUG(3, "%u ", clgs.bb->jmp[i].instr);
       CLG_DEBUG(3, "], last inverted: %s \n",
		 clgs.bb->cjmp_inverted ? "yes":"no");
   }

  return clgs.sbOut;
}





/* Generate code for all outstanding memory events, and mark the queue
   empty.  Code is generated into cgs->sbOut, and this activity
   'consumes' slots in cgs->bb. */
static void flushEvents ( ClgState* clgs )
{
	Int        i, regparms, inew;
	const HChar* helperName;
	void*      helperAddr;
	IRExpr**   argv;
	IRExpr*    i_node_expr;
	IRDirty*   di;
	Event*     ev;
	Event*     ev2;
	Event*     ev3;

	for(i = 0; i < clgs->events_used; i = inew) 
	{
		helperName = NULL;
		helperAddr = NULL;
		argv       = NULL;
		regparms   = 0;

		/* generate IR to notify event i and possibly the ones
		immediately following it. */
		tl_assert(i >= 0 && i < clgs->events_used);

		ev  = &clgs->events[i];
		ev2 = ( i < clgs->events_used-1 ? &clgs->events[i+1] : NULL );
		ev3 = ( i < clgs->events_used-2 ? &clgs->events[i+2] : NULL );

		CLG_DEBUGIF(5) 
		{
			VG_(printf)("  flush ");
			showEvent( ev );
		}

		i_node_expr = mkIRExpr_HWord( (HWord)ev->inode );

		/* Decide on helper fn to call and args to pass it, and advance
		 * i appropriately.
		 * Dm events have same effect as Dw events */
		switch (ev->tag) 
		{
		case Ev_Ir:
			/* Merge an Ir with a following Dr. */
			if (ev2 && ev2->tag == Ev_Dr) 
			{
				tl_assert(ev2->inode == ev->inode);	/* Why is this true?  It's because we're merging an Ir
													with a following Dr.  The Ir derives from the
													instruction's IMark and the Dr from data
													references which follow it.  In short it holds
													because each insn starts with an IMark, hence an
													Ev_Ir, and so these Dr must pertain to the
													immediately preceding Ir.  Same applies to analogous
													assertions in the subsequent cases. */
				helperName = "log_1I1Dr";
				helperAddr = SGL_(log_1I1Dr);
				argv = mkIRExprVec_3( 
						i_node_expr,
						get_Event_dea(ev2),
						mkIRExpr_HWord(get_Event_dszB(ev2)) 
						);
				regparms = 3;
				inew = i+2;
			}
			/* Merge an Ir with a following Dw/Dm. */
			else if (ev2 && (ev2->tag == Ev_Dw || ev2->tag == Ev_Dm)) 
			{
				tl_assert(ev2->inode == ev->inode);
				helperName = "log_1I1Dw";
				helperAddr = SGL_(log_1I1Dw);
				argv = mkIRExprVec_3( 
						i_node_expr,
						get_Event_dea(ev2),
						mkIRExpr_HWord(get_Event_dszB(ev2)) 
						);
				regparms = 3;
				inew = i+2;
			}
			/* Merge an Ir with two following Irs. */
			else if (ev2 && ev3 && ev2->tag == Ev_Ir && ev3->tag == Ev_Ir) 
			{
				helperName = "log_3I0D";
				helperAddr = SGL_(log_3I0D);
				argv = mkIRExprVec_3( 
						i_node_expr,
						mkIRExpr_HWord((HWord)ev2->inode),
						mkIRExpr_HWord((HWord)ev3->inode) 
						);
				regparms = 3;
				inew = i+3;
			}
			/* Merge an Ir with one following Ir. */
			else
			if (ev2 && ev2->tag == Ev_Ir) {
				helperName = "log_2I0D";
				helperAddr = SGL_(log_2I0D);
				argv = mkIRExprVec_2(
						i_node_expr,
						mkIRExpr_HWord((HWord)ev2->inode) 
						);
				regparms = 2;
				inew = i+2;
			}
			/* No merging possible; emit as-is. */
			else {
				helperName = "log_1I0D";
				helperAddr = SGL_(log_1I0D);
				argv = mkIRExprVec_1( i_node_expr );
				regparms = 1;
				inew = i+1;
			}
			break;
		case Ev_Dr:
			/* Data read or modify */
			helperName = "log_0I1Dr";
			helperAddr = SGL_(log_0I1Dr);
			argv = mkIRExprVec_3( 
					i_node_expr,
					get_Event_dea(ev),
					mkIRExpr_HWord(get_Event_dszB(ev)) 
					);
			regparms = 3;
			inew = i+1;
			break;
		case Ev_Dw:
		case Ev_Dm:
		   /* Data write */
		   helperName = "log_0I1Dw";
		   helperAddr = SGL_(log_0I1Dw);
		   argv = mkIRExprVec_3( 
				   i_node_expr,
		   		   get_Event_dea(ev),
		   		   mkIRExpr_HWord( get_Event_dszB(ev)) 
				   );
		   regparms = 3;
		   inew = i+1;
		   break;
		case Ev_Bc:
			/* Conditional branch */
			helperName = "log_cond_branch";
			helperAddr = SGL_(log_cond_branch);
			argv = mkIRExprVec_2( i_node_expr, ev->Ev.Bc.taken );
			regparms = 2;
			inew = i+1;
			break;
		case Ev_Bi:
			/* Branch to an unknown destination */
			helperName = "log_ind_branch";
			helperAddr = SGL_(log_ind_branch);
			argv = mkIRExprVec_2( i_node_expr, ev->Ev.Bi.dst );
			regparms = 2;
			inew = i+1;
			break;
		case Ev_G:
			/* Global bus event (CAS, LOCK-prefix, LL-SC, etc) */
			helperName = "log_global_event";
			helperAddr = SGL_(log_global_event);
			argv = mkIRExprVec_1( i_node_expr );
			regparms = 1;
			inew = i+1;
			break;
		case Ev_Comp:
		   /* Compute */
		   helperName = "log_comp_event";
		   helperAddr = SGL_(log_comp_event);
		   argv = mkIRExprVec_3( 
				   i_node_expr,
		   		   mkIRExpr_HWord(ev->Ev.Comp.op_type),
		   		   mkIRExpr_HWord(ev->Ev.Comp.arity)
				   );
		   regparms = 3;
		   inew = i+1;
		   break;
		default:
			tl_assert(0);
		}

		CLG_DEBUGIF(5) {
		if (inew > i+1) {
			VG_(printf)("   merge ");
			showEvent( ev2 );
		}
		if (inew > i+2) {
			VG_(printf)("   merge ");
			showEvent( ev3 );
		}
		if (helperAddr)
			VG_(printf)("   call  %s (%p)\n",
			helperName, helperAddr);
		}
		
		/* helper could be unset depending on the simulator used */
		if (helperAddr == 0) continue;
		
		/* Add the helper. */
		tl_assert(helperName);
		tl_assert(helperAddr);
		tl_assert(argv);
		di = unsafeIRDirty_0_N( regparms,
		  	      helperName, VG_(fnptr_to_fnentry)( helperAddr ),
		  	      argv );
		addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
	}

	clgs->events_used = 0;
}

static void addEvent_Ir ( ClgState* clgs, InstrInfo* inode )
{
   Event* evt;
   tl_assert(clgs->seen_before || (inode->eventset == 0));

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag      = Ev_Ir;
   evt->inode    = inode;
   clgs->events_used++;
}

static
void addEvent_Dr ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea )
{
   Event* evt;
   tl_assert(isIRAtom(ea));
   tl_assert(datasize >= 1);
   
   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Dr;
   evt->inode     = inode;
   evt->Ev.Dr.szB = datasize;
   evt->Ev.Dr.ea  = ea;
   clgs->events_used++;
}

static
void addEvent_Dw ( ClgState* clgs, InstrInfo* inode, Int datasize, IRAtom* ea )
{
   Event* lastEvt;
   Event* evt;
   tl_assert(isIRAtom(ea));
   tl_assert(datasize >= 1);
  
   /* Is it possible to merge this write with the preceding read? */
   lastEvt = &clgs->events[clgs->events_used-1];
   if (clgs->events_used > 0
       && lastEvt->tag       == Ev_Dr
       && lastEvt->Ev.Dr.szB == datasize
       && lastEvt->inode     == inode
       && eqIRAtom(lastEvt->Ev.Dr.ea, ea))
   {
      lastEvt->tag   = Ev_Dm;
      return;
   }

   /* No.  Add as normal. */
   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Dw;
   evt->inode     = inode;
   evt->Ev.Dw.szB = datasize;
   evt->Ev.Dw.ea  = ea;
   clgs->events_used++;
}

static
void addEvent_D_guarded ( ClgState* clgs, InstrInfo* inode,
                          Int datasize, IRAtom* ea, IRAtom* guard,
                          Bool isWrite )
{
   tl_assert(isIRAtom(ea));
   tl_assert(guard);
   tl_assert(isIRAtom(guard));
   tl_assert(datasize >= 1);
 
   /* Adding guarded memory actions and merging them with the existing
      queue is too complex.  Simply flush the queue and add this
      action immediately.  Since guarded loads and stores are pretty
      rare, this is not thought likely to cause any noticeable
      performance loss as a result of the loss of event-merging
      opportunities. */
   tl_assert(clgs->events_used >= 0);
   flushEvents(clgs);
   tl_assert(clgs->events_used == 0);
   /* Same as case Ev_Dw / case Ev_Dr in flushEvents, except with guard */
   IRExpr*      i_node_expr;
   const HChar* helperName;
   void*        helperAddr;
   IRExpr**     argv;
   Int          regparms;
   IRDirty*     di;
   i_node_expr = mkIRExpr_HWord( (HWord)inode );
   helperName  = isWrite ? "log_0I1Dw"
                         : "log_0I1Dr";
   helperAddr  = isWrite ? SGL_(log_0I1Dw)
                         : SGL_(log_0I1Dr);
   argv        = mkIRExprVec_3( i_node_expr,
                                ea, mkIRExpr_HWord( datasize ) );
   regparms    = 3;
   di          = unsafeIRDirty_0_N(
                    regparms, 
                    helperName, VG_(fnptr_to_fnentry)( helperAddr ), 
                    argv );
   di->guard = guard;
   addStmtToIRSB( clgs->sbOut, IRStmt_Dirty(di) );
}

static
void addEvent_Bc ( ClgState* clgs, InstrInfo* inode, IRAtom* guard )
{
   Event* evt;
   tl_assert(isIRAtom(guard));
   tl_assert(typeOfIRExpr(clgs->sbOut->tyenv, guard)
             == (sizeof(HWord)==4 ? Ity_I32 : Ity_I64));
   if (!CLG_(clo).simulate_branch) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag         = Ev_Bc;
   evt->inode       = inode;
   evt->Ev.Bc.taken = guard;
   clgs->events_used++;
}

static
void addEvent_Bi ( ClgState* clgs, InstrInfo* inode, IRAtom* whereTo )
{
   Event* evt;
   tl_assert(isIRAtom(whereTo));
   tl_assert(typeOfIRExpr(clgs->sbOut->tyenv, whereTo)
             == (sizeof(HWord)==4 ? Ity_I32 : Ity_I64));
   if (!CLG_(clo).simulate_branch) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_Bi;
   evt->inode     = inode;
   evt->Ev.Bi.dst = whereTo;
   clgs->events_used++;
}

static
void addEvent_G ( ClgState* clgs, InstrInfo* inode )
{
   Event* evt;
   if (!CLG_(clo).collect_bus) return;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);
   evt->tag       = Ev_G;
   evt->inode     = inode;
   clgs->events_used++;
}

static
void addEvent_Comp( ClgState* clgs, InstrInfo* inode, IRExprTag arity, IRType op_type )
{
   Event* evt;

   if (clgs->events_used == N_EVENTS)
      flushEvents(clgs);
   tl_assert(clgs->events_used >= 0 && clgs->events_used < N_EVENTS);
   evt = &clgs->events[clgs->events_used];
   init_Event(evt);

   evt->tag       = Ev_Comp;
   evt->inode     = inode;
   evt->Ev.Comp.arity = arity;
   evt->Ev.Comp.op_type = op_type;
   clgs->events_used++;
}

/* Initialise or check (if already seen before) an InstrInfo for next insn.
   We only can set instr_offset/instr_size here. The required event set and
   resulting cost offset depend on events (Ir/Dr/Dw/Dm) in guest
   instructions. The event set is extended as required on flush of the event
   queue (when Dm events were determined), cost offsets are determined at
   end of BB instrumentation. */
static
InstrInfo* next_InstrInfo ( ClgState* clgs, Addr instr_addr, UInt instr_size )
{
   InstrInfo* ii;
   tl_assert(clgs->ii_index >= 0);
   tl_assert(clgs->ii_index < clgs->bb->instr_count);
   ii = &clgs->bb->instr[ clgs->ii_index ];

   if (clgs->seen_before) {
       CLG_ASSERT(ii->instr_offset == clgs->instr_offset);
       CLG_ASSERT(ii->instr_size == instr_size);
   }
   else {
       ii->instr_offset = clgs->instr_offset;
       ii->instr_size = instr_size;
	   ii->instr_addr = instr_addr;
       ii->cost_offset = 0;
       ii->eventset = 0;
   }

   clgs->ii_index++;
   clgs->instr_offset += instr_size;
   CLG_(stat).distinct_instrs++;

   return ii;
}

/*--------------------------------------------------------------------*/
/*--- Discarding BB info                                           ---*/
/*--------------------------------------------------------------------*/

// Called when a translation is removed from the translation cache for
// any reason at all: to free up space, because the guest code was
// unmapped or modified, or for any arbitrary reason.
static
void clg_discard_superblock_info ( Addr orig_addr, VexGuestExtents vge )
{
    tl_assert(vge.n_used > 0);

   if (0)
      VG_(printf)( "discard_superblock_info: %p, %p, %llu\n",
                   (void*)orig_addr,
                   (void*)vge.base[0], (ULong)vge.len[0]);

   // Get BB info, remove from table, free BB info.  Simple!
   // When created, the BB is keyed by the first instruction address,
   // (not orig_addr, but eventually redirected address). Thus, we
   // use the first instruction address in vge.
   CLG_(delete_bb)(vge.base[0]);
}


/*------------------------------------------------------------*/
/*--- CLG_(fini)() and related function                     ---*/
/*------------------------------------------------------------*/



static void zero_thread_cost(thread_info* t)
{
  Int i;

  for(i = 0; i < CLG_(current_call_stack).sp; i++) {
    if (!CLG_(current_call_stack).entry[i].jcc) continue;

    /* reset call counters to current for active calls */
    CLG_(current_call_stack).entry[i].jcc->call_counter = 0;
  }

  CLG_(forall_bbccs)(CLG_(zero_bbcc));

}

void CLG_(zero_all_cost)(Bool only_current_thread)
{
  if (VG_(clo_verbosity) > 1)
    VG_(message)(Vg_DebugMsg, "  Zeroing costs...\n");

  if (only_current_thread)
    zero_thread_cost(CLG_(get_current_thread)());
  else
    CLG_(forall_threads)(zero_thread_cost);

  if (VG_(clo_verbosity) > 1)
    VG_(message)(Vg_DebugMsg, "  ...done\n");
}

static
void unwind_thread(thread_info* t)
{
  /* unwind signal handlers */
  while(CLG_(current_state).sig !=0)
    CLG_(post_signal)(CLG_(current_tid),CLG_(current_state).sig);

  /* unwind regular call stack */
  while(CLG_(current_call_stack).sp>0)
    CLG_(pop_call_stack)();

  /* reset context and function stack for context generation */
  CLG_(init_exec_state)( &CLG_(current_state) );
  CLG_(current_fn_stack).top = CLG_(current_fn_stack).bottom;
}

void CLG_(set_instrument_state)(const HChar* reason, Bool state)
{
  if (CLG_(instrument_state) == state) {
    CLG_DEBUG(2, "%s: instrumentation already %s\n",
	     reason, state ? "ON" : "OFF");
    return;
  }
  CLG_(instrument_state) = state;
  CLG_DEBUG(2, "%s: Switching instrumentation %s ...\n",
	   reason, state ? "ON" : "OFF");

  VG_(discard_translations_safely)( (Addr)0x1000, ~(SizeT)0xfff, "callgrind");

  /* reset internal state: call stacks, simulator */
  CLG_(forall_threads)(unwind_thread);

  if (VG_(clo_verbosity) > 1)
    VG_(message)(Vg_DebugMsg, "%s: instrumentation switched %s\n",
		 reason, state ? "ON" : "OFF");
}


/* Dump current state */
static void dump_state_togdb(void)
{
    thread_info** th;
    int t;
    Int orig_tid = CLG_(current_tid);

    VG_(gdb_printf)("instrumentation: %s\n",
		    CLG_(instrument_state) ? "on":"off");
    if (!CLG_(instrument_state)) return;

    VG_(gdb_printf)("executed-bbs: %llu\n", CLG_(stat).bb_executions);
    VG_(gdb_printf)("executed-calls: %llu\n", CLG_(stat).call_counter);
    VG_(gdb_printf)("distinct-bbs: %d\n", CLG_(stat).distinct_bbs);
    VG_(gdb_printf)("distinct-calls: %d\n", CLG_(stat).distinct_jccs);
    VG_(gdb_printf)("distinct-functions: %d\n", CLG_(stat).distinct_fns);
    VG_(gdb_printf)("distinct-contexts: %d\n", CLG_(stat).distinct_contexts);

    /* threads */
    th = CLG_(get_threads)();
    VG_(gdb_printf)("threads:");
    for(t=1;t<VG_N_THREADS;t++) {
	if (!th[t]) continue;
	VG_(gdb_printf)(" %d", t);
    }
    VG_(gdb_printf)("\n");
    VG_(gdb_printf)("current-tid: %d\n", orig_tid);
}

  
static void print_monitor_help ( void )
{
   VG_(gdb_printf) ("\n");
   VG_(gdb_printf) ("callgrind monitor commands:\n");
   VG_(gdb_printf) ("  dump [<dump_hint>]\n");
   VG_(gdb_printf) ("        dump counters\n");
   VG_(gdb_printf) ("  zero\n");
   VG_(gdb_printf) ("        zero counters\n");
   VG_(gdb_printf) ("  status\n");
   VG_(gdb_printf) ("        print status\n");
   VG_(gdb_printf) ("  instrumentation [on|off]\n");
   VG_(gdb_printf) ("        get/set (if on/off given) instrumentation state\n");
   VG_(gdb_printf) ("\n");
}

/* return True if request recognised, False otherwise */
static Bool handle_gdb_monitor_command (ThreadId tid, const HChar *req)
{
   HChar* wcmd;
   HChar s[VG_(strlen(req)) + 1]; /* copy for strtok_r */
   HChar *ssaveptr;

   VG_(strcpy) (s, req);

   wcmd = VG_(strtok_r) (s, " ", &ssaveptr);
   switch (VG_(keyword_id) ("help dump zero status instrumentation", 
                            wcmd, kwd_report_duplicated_matches)) {
   case -2: /* multiple matches */
      return True;
   case -1: /* not found */
      return False;
   case  0: /* help */
      print_monitor_help();
      return True;
   case  1: { /* dump */
      return True;
   }
   case  2: { /* zero */
      CLG_(zero_all_cost)(False);
      return True;
   }

   case 3: { /* status */
     HChar* arg = VG_(strtok_r) (0, " ", &ssaveptr);
     if (arg && (VG_(strcmp)(arg, "internal") == 0)) {
       /* internal interface to callgrind_control */
       dump_state_togdb();
       return True;
     }

     if (!CLG_(instrument_state)) {
       VG_(gdb_printf)("No status available as instrumentation is switched off\n");
     } else {
       // Status information to be improved ...
       thread_info** th = CLG_(get_threads)();
       Int t, tcount = 0;
       for(t=1;t<VG_N_THREADS;t++)
	 if (th[t]) tcount++;
       VG_(gdb_printf)("%d thread(s) running.\n", tcount);
     }
     return True;
   }

   case 4: { /* instrumentation */
     HChar* arg = VG_(strtok_r) (0, " ", &ssaveptr);
     if (!arg) {
       VG_(gdb_printf)("instrumentation: %s\n",
		       CLG_(instrument_state) ? "on":"off");
     }
     else
       CLG_(set_instrument_state)("Command", VG_(strcmp)(arg,"off")!=0);
     return True;
   }

   default: 
      tl_assert(0);
      return False;
   }
}

static
Bool CLG_(handle_client_request)(ThreadId tid, UWord *args, UWord *ret)
{
   if (!VG_IS_TOOL_USERREQ('C','T',args[0])
       && VG_USERREQ__GDB_MONITOR_COMMAND   != args[0])
      return False;

   switch(args[0]) 
   {
   case VG_USERREQ__DUMP_STATS:     
      *ret = 0;                 /* meaningless */
      break;

   case VG_USERREQ__DUMP_STATS_AT:
     {
       const HChar *arg = (HChar*)args[1];
       HChar buf[30 + VG_(strlen)(arg)];    // large enough
       VG_(sprintf)(buf,"Client Request: %s", arg);
       *ret = 0;                 /* meaningless */
     }
     break;

   case VG_USERREQ__ZERO_STATS:
     CLG_(zero_all_cost)(True);
      *ret = 0;                 /* meaningless */
      break;

   case VG_USERREQ__TOGGLE_COLLECT:
     CLG_(current_state).collect = !CLG_(current_state).collect;
     CLG_DEBUG(2, "Client Request: toggled collection state to %s\n",
	      CLG_(current_state).collect ? "ON" : "OFF");
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__START_INSTRUMENTATION:
     CLG_(set_instrument_state)("Client Request", True);
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__STOP_INSTRUMENTATION:
     CLG_(set_instrument_state)("Client Request", False);
     *ret = 0;                 /* meaningless */
     break;

   case VG_USERREQ__GDB_MONITOR_COMMAND: {
      Bool handled = handle_gdb_monitor_command (tid, (HChar*)args[1]);
      if (handled)
         *ret = 1;
      else
         *ret = 0;
      return handled;
   }

   /*******************************************
    * Synchronixation API intercepts 
	*
	* FIXME default OpenMP behavior modeled after
	* KS need for SynchroTrace, reassess if this
	* is appropriate */
										 
   case VG_USERREQ__SIGIL_PTHREAD_CREATE_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_CREATE_LEAVE:
      /* enable and log once the thread has been CREATED and waiting */
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_CREATE, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_PTHREAD_JOIN_ENTER:
      /* log when the thread join is ENTERED and disable */
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_JOIN, args[1], UNUSED_SYNC_DATA);
      }
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_JOIN_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      break;

   case VG_USERREQ__SIGIL_GOMP_LOCK_ENTER:
   case VG_USERREQ__SIGIL_GOMP_SETLOCK_ENTER:
   case VG_USERREQ__SIGIL_GOMP_CRITSTART_ENTER:
   case VG_USERREQ__SIGIL_GOMP_CRITNAMESTART_ENTER:
   case VG_USERREQ__SIGIL_GOMP_ATOMICSTART_ENTER:
   case VG_USERREQ__SIGIL_PTHREAD_LOCK_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_GOMP_SETLOCK_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_LOCK_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_CRITSTART_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_CRITNAMESTART_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_ATOMICSTART_LEAVE:
   case VG_USERREQ__SIGIL_PTHREAD_LOCK_LEAVE:
      /* enable and log once the lock has been acquired */
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_LOCK, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_GOMP_UNLOCK_ENTER:
   case VG_USERREQ__SIGIL_GOMP_UNSETLOCK_ENTER:
   case VG_USERREQ__SIGIL_GOMP_CRITEND_ENTER:
   case VG_USERREQ__SIGIL_GOMP_CRITNAMEEND_ENTER:
   case VG_USERREQ__SIGIL_GOMP_ATOMICEND_ENTER:
   case VG_USERREQ__SIGIL_PTHREAD_UNLOCK_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_GOMP_UNLOCK_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_UNSETLOCK_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_CRITEND_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_CRITNAMEEND_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_ATOMICEND_LEAVE:
   case VG_USERREQ__SIGIL_PTHREAD_UNLOCK_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_UNLOCK, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_GOMP_BARRIER_ENTER:
   case VG_USERREQ__SIGIL_GOMP_TEAMBARRIERWAIT_ENTER:
   case VG_USERREQ__SIGIL_GOMP_TEAMBARRIERWAITFINAL_ENTER:
   case VG_USERREQ__SIGIL_PTHREAD_BARRIER_ENTER:
      /* log once the barrier is ENTERED and waiting and disable */
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_BARRIER, args[1], UNUSED_SYNC_DATA);
      }
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_GOMP_BARRIER_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_TEAMBARRIERWAIT_LEAVE:
   case VG_USERREQ__SIGIL_GOMP_TEAMBARRIERWAITFINAL_LEAVE:
   case VG_USERREQ__SIGIL_PTHREAD_BARRIER_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      break;

   case VG_USERREQ__SIGIL_PTHREAD_CONDWAIT_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_CONDWAIT_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_CONDWAIT, args[1], args[2]);
      }
      break;

   case VG_USERREQ__SIGIL_PTHREAD_CONDSIG_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_CONDSIG_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_CONDSIG, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_PTHREAD_CONDBROAD_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_CONDBROAD_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_CONDBROAD, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_PTHREAD_SPINLOCK_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_SPINLOCK_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_SPINLOCK, args[1], UNUSED_SYNC_DATA);
      }
      break;

   case VG_USERREQ__SIGIL_PTHREAD_SPINUNLOCK_ENTER:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = True;
      break;
   case VG_USERREQ__SIGIL_PTHREAD_SPINUNLOCK_LEAVE:
      SGL_(thread_in_synccall)[SGL_(active_tid)] = False;
      if ( EVENT_GENERATION_ENABLED )
      {
         SGL_(log_sync)((UChar)PRISM_SYNC_SPINUNLOCK, args[1], UNUSED_SYNC_DATA);
      }
      break;

   default:
      return False;
   }


   return True;
}


/* Syscall Timing */

/* struct timeval syscalltime[VG_N_THREADS]; */
#if CLG_MICROSYSTIME
ULong *syscalltime;
#else
UInt *syscalltime;
#endif


static
void clg_print_stats(void)
{
}

static void finish(void)
{
  CLG_DEBUG(0, "finish()\n");

  /* pop all remaining items from CallStack for correct sum
   */
  CLG_(forall_threads)(unwind_thread);

  /* finish IPC with Sigil2 */
  SGL_(term_IPC)();
  SGL_(end_logging)();
}


void CLG_(fini)(Int exitcode)
{
  finish();
}


/*--------------------------------------------------------------------*/
/*--- Setup                                                        ---*/
/*--------------------------------------------------------------------*/

static void clg_start_client_code_callback ( ThreadId tid, ULong blocks_done )
{
   static ULong last_blocks_done = 0;

   if (0)
      VG_(printf)("%d R %llu\n", (Int)tid, blocks_done);

   /* throttle calls to CLG_(run_thread) by number of BBs executed */
   if (blocks_done - last_blocks_done < 5000) return;
   last_blocks_done = blocks_done;

   CLG_(run_thread)( tid );
}

static
void CLG_(post_clo_init)(void)
{
   SGL_(init_IPC)(); // initialize interface to Sigil
   SGL_(is_in_event_collect_func) = False;

   if (SGL_(clo).collect_func == NULL &&
       SGL_(clo).start_collect_func == NULL)
   {
      VG_(umsg)("*********************************************\n");
      VG_(umsg)("Beginning event generation from program start\n");
      VG_(umsg)("*********************************************\n");
      SGL_(is_in_event_collect_func) = True;
   }

   if (SGL_(clo).gen_cf == True)
      VG_(umsg)("WARNING: Control Flow events unsupported\n");
   if (SGL_(clo).gen_bb == True)
      VG_(umsg)("WARNING: Basic Block context events unsupported\n");

   if (VG_(clo_vex_control).iropt_register_updates_default
       != VexRegUpdSpAtMemAccess) {
      CLG_DEBUG(1, " Using user specified value for "
                "--vex-iropt-register-updates\n");
   } else {
      CLG_DEBUG(1, 
                " Using default --vex-iropt-register-updates="
                "sp-at-mem-access\n");
   }

   if (VG_(clo_px_file_backed) != VexRegUpdSpAtMemAccess) {
      CLG_DEBUG(1, " Using user specified value for "
                "--px-file-backed\n");
   } else {
      CLG_DEBUG(1, 
                " Using default --px-file-backed="
                "sp-at-mem-access\n");
   }

   if (VG_(clo_vex_control).iropt_unroll_thresh != 0) {
      VG_(message)(Vg_UserMsg, 
                   "callgrind only works with --vex-iropt-unroll-thresh=0\n"
                   "=> resetting it back to 0\n");
      VG_(clo_vex_control).iropt_unroll_thresh = 0;   // cannot be overriden.
   }
   if (VG_(clo_vex_control).guest_chase_thresh != 0) {
      VG_(message)(Vg_UserMsg,
                   "callgrind only works with --vex-guest-chase-thresh=0\n"
                   "=> resetting it back to 0\n");
      VG_(clo_vex_control).guest_chase_thresh = 0; // cannot be overriden.
   }
   
   CLG_DEBUG(1, "  dump threads: %s\n", CLG_(clo).separate_threads ? "Yes":"No");
   CLG_DEBUG(1, "  call sep. : %d\n", CLG_(clo).separate_callers);
   CLG_DEBUG(1, "  rec. sep. : %d\n", CLG_(clo).separate_recursions);

   if (!CLG_(clo).dump_line && !CLG_(clo).dump_instr && !CLG_(clo).dump_bb) {
       VG_(message)(Vg_UserMsg, "Using source line as position.\n");
       CLG_(clo).dump_line = True;
   }

   /* initialize hash tables */
   CLG_(init_obj_table)();
   CLG_(init_cxt_table)();
   CLG_(init_bb_hash)();

   CLG_(init_threads)();
   CLG_(run_thread)(1);

   CLG_(instrument_state) = CLG_(clo).instrument_atstart;
}

static
void CLG_(pre_clo_init)(void)
{
    VG_(details_name)            ("Sigrind");
    VG_(details_version)         (NULL);
    VG_(details_description)     ("");
    VG_(details_copyright_author)("Copyright (C) 2015-2017, "
				  "by Michael Lui et al.");
    VG_(details_bug_reports_to)  (VG_BUGS_TO);
    VG_(details_avg_translation_sizeB) ( 500 );

    VG_(clo_vex_control).iropt_register_updates_default
       = VG_(clo_px_file_backed)
       = VexRegUpdSpAtMemAccess; // overridable by the user.

    VG_(clo_vex_control).iropt_unroll_thresh = 0;   // cannot be overriden.
    VG_(clo_vex_control).guest_chase_thresh = 0;    // cannot be overriden.

    VG_(basic_tool_funcs)        (CLG_(post_clo_init),
                                  CLG_(instrument),
                                  CLG_(fini));

    VG_(needs_superblock_discards)(clg_discard_superblock_info);


    VG_(needs_command_line_options)(CLG_(process_cmd_line_option),
				    CLG_(print_usage),
				    CLG_(print_debug_usage));

    VG_(needs_client_requests)(CLG_(handle_client_request));
    VG_(needs_print_stats)    (clg_print_stats);

    VG_(track_start_client_code)  ( & clg_start_client_code_callback );
    VG_(track_pre_deliver_signal) ( & CLG_(pre_signal) );
    VG_(track_post_deliver_signal)( & CLG_(post_signal) );

    /* Track syscalls */
    /* XXX MDL20170226
     * Right now syscalls are not being monitored.
     * There hasn't been a convincing case made that memory reads/writes
     * from syscalls are significant enough to warrant the extra monitoring.
     * If required, the following callbacks can be used to get addt'l info
     * every time a syscall writes to an address, and also in general when
     * a syscall is invoked.
     *
     * VG_(track_post_mem_write) (vgcore_memwrite_callback)
     * VG_(needs_syscall_wrapp)  (pre_syscall_callback,
     *                            post_syscall_callback)
     *
     * For more info look in ../include/pub_tool_tooliface.h
     * The other core event callbacks offered by Valgrind probably aren't
     * relelvant for us. */

    /* Defaults */
    SGL_(set_clo_defaults)();
    CLG_(set_clo_defaults)();
}

VG_DETERMINE_INTERFACE_VERSION(CLG_(pre_clo_init))

/*------------------------------------------------------------*/
/*--- Initialization and helpers                           ---*/
/*------------------------------------------------------------*/


static void init_Event ( Event* ev ) {
   VG_(memset)(ev, 0, sizeof(Event));
}

static IRAtom* get_Event_dea ( Event* ev ) {
   switch (ev->tag) {
      case Ev_Dr: return ev->Ev.Dr.ea;
      case Ev_Dw: return ev->Ev.Dw.ea;
      case Ev_Dm: return ev->Ev.Dm.ea;
      default:    tl_assert(0);
   }
}

static Int get_Event_dszB ( Event* ev ) {
   switch (ev->tag) {
      case Ev_Dr: return ev->Ev.Dr.szB;
      case Ev_Dw: return ev->Ev.Dw.szB;
      case Ev_Dm: return ev->Ev.Dm.szB;
      default:    tl_assert(0);
   }
}

#if defined(VG_BIGENDIAN)
# define CLGEndness Iend_BE
#elif defined(VG_LITTLEENDIAN)
# define CLGEndness Iend_LE
#else
# error "Unknown endianness"
#endif

static
Addr IRConst2Addr(IRConst* con)
{
    Addr addr;

    if (sizeof(Addr) == 4) {
	CLG_ASSERT( con->tag == Ico_U32 );
	addr = con->Ico.U32;
    }
    else if (sizeof(Addr) == 8) {
	CLG_ASSERT( con->tag == Ico_U64 );
	addr = con->Ico.U64;
    }
    else
	VG_(tool_panic)("Callgrind: invalid Addr type");

    return addr;
}

/* First pass over a BB to instrument, counting instructions and jumps
 * This is needed for the size of the BB struct to allocate
 *
 * Called from CLG_(get_bb)
 */
void CLG_(collectBlockInfo)(IRSB* sbIn,
			    /*INOUT*/ UInt* instrs,
			    /*INOUT*/ UInt* cjmps,
			    /*INOUT*/ Bool* cjmp_inverted)
{
    Int i;
    IRStmt* st;
    Addr instrAddr =0, jumpDst;
    UInt instrLen = 0;
    Bool toNextInstr = False;

    // Ist_Exit has to be ignored in preamble code, before first IMark:
    // preamble code is added by VEX for self modifying code, and has
    // nothing to do with client code
    Bool inPreamble = True;

    if (!sbIn) return;

    for (i = 0; i < sbIn->stmts_used; i++) {
	  st = sbIn->stmts[i];
	  if (Ist_IMark == st->tag) {
	      inPreamble = False;

	      instrAddr = st->Ist.IMark.addr;
	      instrLen  = st->Ist.IMark.len;

	      (*instrs)++;
	      toNextInstr = False;
	  }
	  if (inPreamble) continue;
	  if (Ist_Exit == st->tag) {
	      jumpDst = IRConst2Addr(st->Ist.Exit.dst);
	      toNextInstr =  (jumpDst == instrAddr + instrLen);

	      (*cjmps)++;
	  }
    }

    /* if the last instructions of BB conditionally jumps to next instruction
     * (= first instruction of next BB in memory), this is a inverted by VEX.
     */
    *cjmp_inverted = toNextInstr;
}

static
void addConstMemStoreStmt( IRSB* bbOut, UWord addr, UInt val, IRType hWordTy)
{
    addStmtToIRSB( bbOut,
		   IRStmt_Store(CLGEndness,
				IRExpr_Const(hWordTy == Ity_I32 ?
					     IRConst_U32( addr ) :
					     IRConst_U64( addr )),
				IRExpr_Const(IRConst_U32(val)) ));
}   



static void showEvent ( Event* ev )
{
   switch (ev->tag) {
      case Ev_Ir:
	 VG_(printf)("Ir (InstrInfo %p) at +%u\n",
		     ev->inode, ev->inode->instr_offset);
	 break;
      case Ev_Dr:
	 VG_(printf)("Dr (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dr.szB);
	 ppIRExpr(ev->Ev.Dr.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Dw:
	 VG_(printf)("Dw (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dw.szB);
	 ppIRExpr(ev->Ev.Dw.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Dm:
	 VG_(printf)("Dm (InstrInfo %p) at +%u %d EA=",
		     ev->inode, ev->inode->instr_offset, ev->Ev.Dm.szB);
	 ppIRExpr(ev->Ev.Dm.ea);
	 VG_(printf)("\n");
	 break;
      case Ev_Bc:
         VG_(printf)("Bc %p   GA=", ev->inode);
         ppIRExpr(ev->Ev.Bc.taken);
         VG_(printf)("\n");
         break;
      case Ev_Bi:
         VG_(printf)("Bi %p  DST=", ev->inode);
         ppIRExpr(ev->Ev.Bi.dst);
         VG_(printf)("\n");
         break;
      case Ev_G:
         VG_(printf)("G  %p\n", ev->inode);
         break;
      default:
	 tl_assert(0);
	 break;
   }
}

/*--------------------------------------------------------------------*/
/*--- end                                                   main.c ---*/
/*--------------------------------------------------------------------*/
