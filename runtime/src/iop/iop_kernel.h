#pragma once
#include "../common.h"
#include "iop.h"
#include "iop_recomp.h"

namespace iop::kern {

void init();
void run_ready();
void run_timers();
void queue_irq(u32 func, u32 arg0, u32 arg1);
void run_irqs();
int alloc_timer(u32 source, u32 bits, u32 prescale);
int setup_timer(u32 id, u32 source, u32 mode, u32 prescale);
int timer_callback(u32 id, u32 ticks, u32 func, u32 arg);
int start_timer(u32 id);
int stop_timer(u32 id);
int free_timer(u32 id);
bool has_ready();
void vblank(bool start);
void queue_rpc_request(RpcServer* s, u32 fno, u32 send_size, u32 client, u32 rec_id, u32 pkt_addr, u32 rpc_id,
                       u32 receive, u32 recv_size);
bool dispatch_user_cmd(u32 cid, u32 pkt_addr);

// thread API used by import implementations
int create_thread(u32 entry, u32 attr, u32 prio, u32 stacksize, u32 option);
int start_thread(int id, u32 arg);
int current_thread();
void delay_current(double seconds);
void sleep_current();
void wait_vblank(bool start);
int create_sema(u32 attr, int initial, int maximum);
int wait_sema(int id, bool poll);
int signal_sema(int id);
int delete_sema(int id);
void wakeup(int id);
void yield_current();
void register_vblank_handler(int edge, int prio, u32 handler, u32 arg);
void register_module_funcs(const IOPFuncEntry* table, unsigned count);
void call_func(IOPContext* c, u32 addr);
IOPContext* irq_ctx();

// RPC loop for recompiled servers (runs inside an IOP thread)
void rpc_register(u32 sd, u32 sid, u32 func, u32 buf, u32 cfunc, u32 cbuf, u32 qd);
void rpc_set_queue(u32 qd, int thid);
[[noreturn]] void rpc_loop(IOPContext* c, u32 qd);

}  // namespace iop::kern
