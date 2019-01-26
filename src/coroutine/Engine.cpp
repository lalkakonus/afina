#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char stack_end;
	ctx.Hight = StackBottom;
	ctx.Low = &stack_end;
	size_t size = ctx.Hight - ctx.Low;

	if (std::get<1>(ctx.Stack) < size) {
		delete[] std::get<0>(ctx.Stack);
		std::get<0>(ctx.Stack) = new char[size];
		std::get<1>(ctx.Stack) = size;
	}

	memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
	volatile char stack_end;
	if (&stack_end >= ctx.Low && &stack_end <= ctx.Hight) {
		Restore(ctx);
	}

	auto buffer = std::get<0>(ctx.Stack);
    auto size = ctx.Hight - ctx.Low;
    
    memcpy(ctx.Low, buffer, size);
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context * candidate = alive;
    
    if (candidate && candidate == cur_routine) {
        candidate = candidate->next;
    }
    
    if (candidate) {
        if (cur_routine) {
            if (setjmp(cur_routine->Environment) > 0) {
                return;
            }
            Store(*cur_routine);
        }
        cur_routine = static_cast<context *>(candidate);
        Restore(*candidate);
    }
}

void Engine::sched(void* routine) {
    if (!cur_routine) {
        cur_routine = static_cast<context *>(routine);
        Restore(*cur_routine);
    } else {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
        cur_routine = static_cast<context *>(routine);
        Restore(*cur_routine); 
    }
}

} // namespace Coroutine
} // namespace Afina
