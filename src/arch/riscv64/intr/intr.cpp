
/**
 * @file intr.cpp
 * @brief 中断实现
 * @author Zone.N (Zone.Niuzh@hotmail.com)
 * @version 1.0
 * @date 2021-09-18
 * @copyright MIT LICENSE
 * https://github.com/Simple-XX/SimpleKernel
 * @par change log:
 * <table>
 * <tr><th>Date<th>Author<th>Description
 * <tr><td>2021-09-18<td>digmouse233<td>迁移到 doxygen
 * </table>
 */

#include "cpu.hpp"
#include "stdio.h"
#include "intr.h"
#include "vmm.h"
#include "pmm.h"
#include "task.h"
#include "core.h"

extern "C" void switch_context(CPU::context_t *_old, CPU::context_t *_new);

/**
 * @brief 保存当前上下文并跳转到调度线程
 */
static void switch_sched(void) {
    task_t *old = core_t::get_curr_task();
    // 设置 core 当前线程信息
    switch_context(&old->context,
                   &core_t::cores[CPU::get_curr_core_id()].sched_task->context);
    return;
}

/**
 * @brief 中断处理函数
 * @param  _scause         原因
 * @param  _sepc           值
 * @param  _stval          值
 */
extern "C" void trap_handler(uintptr_t _sepc, uintptr_t _stval,
                             uintptr_t _scause, uintptr_t _sp,
                             uintptr_t _sstatus, CPU::context_t *_context) {
    CPU::DISABLE_INTR();
    // 消除 unused 警告
    (void)_sepc;
    (void)_stval;
    (void)_scause;
    (void)_sp;
    (void)_sstatus;
    (void)_context;
#define DEBUG
#ifdef DEBUG
    info("sepc: 0x%p, stval: 0x%p, scause: 0x%p, sp: 0x%p, sstatus: 0x%p.\n",
         _sepc, _stval, _scause, _sp, _sstatus);
#undef DEBUG
#endif
    if (_scause & CPU::CAUSE_INTR_MASK) {
// 中断
// #define DEBUG
#ifdef DEBUG
        info("intr: %s.\n", INTR::get_instance().get_intr_name(
                                _scause & CPU::CAUSE_CODE_MASK));
#undef DEBUG
#endif
        // 跳转到对应的处理函数
        INTR::get_instance().do_interrupt(_scause & CPU::CAUSE_CODE_MASK);
        // 如果是时钟中断
        if ((_scause & CPU::CAUSE_CODE_MASK) == INTR::INTR_S_TIMER) {
            // 设置 sepc，切换到内核线程
            CPU::WRITE_SEPC(reinterpret_cast<uintptr_t>(&switch_sched));
        }
    }
    else {
// 异常
// 跳转到对应的处理函数
// #define DEBUG
#ifdef DEBUG
        warn("excp: %s.\n",
             INTR::get_instance().excp_name(_scause & CPU::CAUSE_CODE_MASK));
#undef DEBUG
#endif
        INTR::get_instance().do_excp(_scause & CPU::CAUSE_CODE_MASK);
    }
    return;
}

/// 中断处理入口 intr_s.S
extern "C" void trap_entry(void);

/**
 * @brief 缺页读处理
 */
void pg_load_excp(void) {
    uintptr_t addr = CPU::READ_STVAL();
    uintptr_t pa   = 0x0;
    auto      is_mmap =
        VMM::get_instance().get_mmap(VMM::get_instance().get_pgd(), addr, &pa);
    // 如果 is_mmap 为 true，说明已经应映射过了
    if (is_mmap == true) {
        // 直接映射
        VMM::get_instance().mmap(VMM::get_instance().get_pgd(), addr, pa,
                                 VMM_PAGE_READABLE);
    }
    else {
        // 分配一页物理内存进行映射
        pa = PMM::get_instance().alloc_page_kernel();
        VMM::get_instance().mmap(VMM::get_instance().get_pgd(), addr, pa,
                                 VMM_PAGE_READABLE);
    }
    info("pg_load_excp done: 0x%p.\n", addr);
    return;
}

/**
 * @brief 缺页写处理
 * @todo 需要读权限吗？测试发现没有读权限不行，原因未知
 */
void pg_store_excp(void) {
    uintptr_t addr = CPU::READ_STVAL();
    uintptr_t pa   = 0x0;
    auto      is_mmap =
        VMM::get_instance().get_mmap(VMM::get_instance().get_pgd(), addr, &pa);
    // 如果 is_mmap 为 true，说明已经应映射过了
    if (is_mmap == true) {
        // 直接映射
        VMM::get_instance().mmap(VMM::get_instance().get_pgd(), addr, pa,
                                 VMM_PAGE_READABLE | VMM_PAGE_WRITABLE);
    }
    else {
        // 分配一页物理内存进行映射
        pa = PMM::get_instance().alloc_page_kernel();
        VMM::get_instance().mmap(VMM::get_instance().get_pgd(), addr, pa,
                                 VMM_PAGE_READABLE | VMM_PAGE_WRITABLE);
    }
    info("pg_store_excp done: 0x%p.\n", addr);
    return;
}

/**
 * @brief 默认使用的中断处理函数
 */
void handler_default(void) {
    while (1) {
        ;
    }
    return;
}

INTR &INTR::get_instance(void) {
    /// 定义全局 INTR 对象
    static INTR intr;
    return intr;
}

int32_t INTR::init(void) {
    //    // 创建用于保存上下文的空间
    //    CPU::context_t *context = (CPU::context_t
    //    *)kmalloc(sizeof(CPU::context_t));
    //    // 将地址保存在 sscratch 寄存器中
    //    CPU::WRITE_SSCRATCH(reinterpret_cast<uint64_t>(context));
    // 设置 trap vector
    CPU::WRITE_STVEC((uintptr_t)trap_entry);
    // 直接跳转到处理函数
    CPU::STVEC_DIRECT();
    // 内部中断初始化
    CLINT::get_instance().init();
    // 外部中断初始化
    PLIC::get_instance().init();
    // 设置处理函数
    for (auto &i : interrupt_handlers) {
        i = handler_default;
    }
    for (auto &i : excp_handlers) {
        i = handler_default;
    }
    // 注册缺页中断
    register_excp_handler(EXCP_LOAD_PAGE_FAULT, pg_load_excp);
    // 注册缺页中断
    register_excp_handler(EXCP_STORE_PAGE_FAULT, pg_store_excp);
    info("intr init.\n");
    return 0;
}

int32_t INTR::init_other_core(void) {
    // 设置 trap vector
    CPU::WRITE_STVEC((uintptr_t)trap_entry);
    // 直接跳转到处理函数
    CPU::STVEC_DIRECT();
    // 内部中断初始化
    CLINT::get_instance().init_other_core();
    // 外部中断初始化
    PLIC::get_instance().init_other_core();
    info("intr other 0x%X init.\n", CPU::get_curr_core_id());
    return 0;
}

void INTR::register_interrupt_handler(
    uint8_t _no, INTR::interrupt_handler_t _interrupt_handler) {
    spinlock.lock();
    interrupt_handlers[_no] = _interrupt_handler;
    spinlock.unlock();
    return;
}

void INTR::register_excp_handler(uint8_t                   _no,
                                 INTR::interrupt_handler_t _interrupt_handler) {
    spinlock.lock();
    excp_handlers[_no] = _interrupt_handler;
    spinlock.unlock();
    return;
}

void INTR::do_interrupt(uint8_t _no) {
    interrupt_handlers[_no]();
    return;
}

void INTR::do_excp(uint8_t _no) {
    excp_handlers[_no]();
    return;
}

const char *INTR::get_intr_name(uint8_t _no) const {
    return intr_names[_no];
}

const char *INTR::get_excp_name(uint8_t _no) const {
    return excp_names[_no];
}
