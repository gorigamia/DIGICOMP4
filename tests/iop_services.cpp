#include "../runtime/src/iop/iop.cpp"
#include "../runtime/src/iop/iop_kernel.cpp"
#include <cassert>
namespace mem { u8* iop_ram = nullptr; }
namespace kernel { double test_time = 0; double emu_time() { return test_time; } }
bool g_log_enabled[LOG_COUNT] = {};
void log_write(int, const char*, ...) {}
[[noreturn]] void fatal(const char*, ...) { abort(); }
double host_time_sec() { return 0; }
const GameIopModule g_game_iop_modules[] = {{"reserved", 0x100000, 0, 0, 0x40000, 0, nullptr, nullptr, 0}};
const unsigned g_game_iop_module_count = 1;
static int sem, stage;
void waiter(IOPContext*) { stage = 1; assert(iop::kern::wait_sema(sem, false) == 0); stage = 3; }
void signaler(IOPContext*) { assert(stage == 1); stage = 2; assert(iop::kern::signal_sema(sem) == 0); }
void video_waiter(IOPContext*) { stage = 4; iop::kern::wait_vblank(false); stage = 5; }
static int ticks, irq_count;
void dma_irq(IOPContext* c) { assert(c->r[4] == 1 && c->r[5] == 0x1234); ++irq_count; }
void timer_tick(IOPContext* c) { assert(c->r[4] == 42); ++ticks; c->r[2] = ticks < 3 ? 720 : 0; }
int main() {
 mem::iop_ram = new u8[0x200000]{};
 u32 first = iop::alloc(0x100, 64); assert(first == 0x58000);
 iop::free(first); assert(iop::alloc(0x100, 64) == first);
 u32 low = iop::alloc(0xa7000, 64); assert(low == 0x58100);
 u32 high = iop::alloc(0x30000, 256); assert(high == 0x140000);
 assert(iop::alloc(0x200000, 16) == 0); assert(iop::alloc(4, 3) == 0);
 iop::free(low); iop::free(high);
 assert(ConvertThreadToFiber(nullptr)); iop::kern::init();
 sem = iop::kern::create_sema(0, 0, 1);
 assert(iop::kern::wait_sema(sem, true) < 0);
 assert(iop::kern::signal_sema(sem) == 0); assert(iop::kern::signal_sema(sem) < 0);
 assert(iop::kern::wait_sema(sem, true) == 0);
 const IOPFuncEntry funcs[] = {{0x100,0x104,waiter},{0x200,0x204,signaler},{0x300,0x304,video_waiter},{0x400,0x404,timer_tick},{0x500,0x504,dma_irq}};
 iop::kern::register_module_funcs(funcs, 5);
 int a = iop::kern::create_thread(0x100, 0, 1, 4096, 0);
 int b = iop::kern::create_thread(0x200, 0, 2, 4096, 0);
 iop::kern::start_thread(a, 0); iop::kern::start_thread(b, 0);
 iop::kern::run_ready(); assert(stage == 3);
 int v = iop::kern::create_thread(0x300, 0, 1, 4096, 0);
 iop::kern::start_thread(v, 0); iop::kern::run_ready(); assert(stage == 4);
 iop::kern::vblank(true); iop::kern::run_ready(); assert(stage == 4);
 iop::kern::vblank(false); iop::kern::run_ready(); assert(stage == 5);
 assert(iop::kern::delete_sema(sem) == 0); assert(iop::kern::signal_sema(sem) < 0);
 using namespace iop::kern;
 queue_irq(0x500,1,0x1234); assert(irq_count == 0);
 run_irqs(); assert(irq_count == 1); run_irqs(); assert(irq_count == 1);
 int timer = alloc_timer(1,32,256); assert(timer > 0);
 assert(setup_timer(timer,1,0,256) == 0);
 assert(timer_callback(timer,720,0x400,42) == 0);
 assert(start_timer(timer) == 0); assert(start_timer(timer) < 0);
 kernel::test_time = .004; run_timers(); assert(ticks == 0);
 kernel::test_time = .005; run_timers(); assert(ticks == 1);
 kernel::test_time = .0101; run_timers(); assert(ticks == 2);
 kernel::test_time = .016; run_timers(); assert(ticks == 3);
 kernel::test_time = .1; run_timers(); assert(ticks == 3);
 assert(start_timer(timer) == 0); assert(stop_timer(timer) == 0);
 kernel::test_time = 1; run_timers(); assert(ticks == 3);
 assert(free_timer(timer) == 0); assert(start_timer(timer) < 0);
 puts("PASS: hardware timer deadlines/rearm/stop/free,  allocator reuse/reservations/bounds, semaphore blocking/wakeup/overflow, vblank edges");
}

// Unused hardware collaborators fail loudly if a unit test reaches them.
namespace sif {
void ee_write(u32,const void*,u32) { abort(); }
void send_cmd_to_ee(u32,const void*,u32,u32,const void*,u32) { abort(); }
void set_ee_pktbuf(u32) { abort(); }
void clear_smflag(u32) { abort(); }
void set_smflag(u32) { abort(); }
void pump() { abort(); }
}
namespace mem { std::string iop_cstr(u32,size_t) { abort(); } }
namespace spu2 { void init() { abort(); } void advance_to(double) { abort(); } }
namespace iop::modules { void register_builtin() { abort(); } void load(const std::string&,const std::string&) { abort(); } }
namespace iop::pad { void vblank() { abort(); } }
