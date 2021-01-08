
// This file is a part of Simple-XX/SimpleKernel
// (https://github.com/Simple-XX/SimpleKernel).
//
// kernel.h for Simple-XX/SimpleKernel.

#ifndef _KERNEL_H_
#define _KERNEL_H_

#include "stdint.h"
#include "console.h"

extern "C" void kernel_main(void);

class KERNEL {
private:
    CONSOLE console;

protected:
public:
    KERNEL(void);
    ~KERNEL(void);
    int32_t init(void);
};

#endif /* _KERNEL_H_ */
